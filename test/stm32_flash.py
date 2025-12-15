#!/usr/bin/env python3
"""
STM32H7 CAN Bootloader Flash Tool
Flashes firmware via CAN using Open Bootloader protocol
"""

import can
import time
import sys
import glob

class STM32H7CANFlasher:
    def __init__(self, bus):
        self.bus = bus
        self.APP_ADDRESS = 0x08020000
        
        # Command IDs
        self.CMD_GET = 0x00
        self.CMD_GET_VERSION = 0x01
        self.CMD_GET_ID = 0x02
        self.CMD_READ_MEMORY = 0x11
        self.CMD_GO = 0x21
        self.CMD_WRITE_MEMORY = 0x31
        self.CMD_ERASE = 0x44
        
        # Response IDs
        self.ACK_ID = 0x79
        self.NACK_ID = 0x1F
        
    def wait_for_ack(self, id, timeout=2.0):
        """Wait for ACK response"""
        start = time.time()
        while time.time() - start < timeout:
            msg = self.bus.recv(timeout=0.01)
            if msg and msg.arbitration_id == id:
                if msg.data[0] ==  self.ACK_ID:
                    return True
                elif msg.data[0] ==  self.NACK_ID:
                    return False
        return False
    
    def detect_bootloader(self):
        """Detect bootloader by sending 0x79"""
        print("Detecting STM32 bootloader...")
        
        # Send 0x79 to initiate communication
        msg = can.Message(arbitration_id=self.ACK_ID, data=[0x00], is_extended_id=False)
        self.bus.send(msg)
        time.sleep(0.1)
        
        # May receive NACK (0x1F) first time - that's OK
        response = self.bus.recv(timeout=1.0)
        if response:
            if response.arbitration_id == self.NACK_ID:
                print("✓ Bootloader detected (NACK received - normal for first contact)")
                return True
            elif response.arbitration_id == self.ACK_ID:
                print("✓ Bootloader detected (ACK received)")
                return True
        
        print("✗ No bootloader response")
        return False
    
    def get_bootloader_version(self):
        """Get bootloader version"""
        print("Getting bootloader version...")
        
        # Send command: ID=0x01, Data=0x00
        msg = can.Message(arbitration_id=self.CMD_GET_VERSION, data=[0x00], is_extended_id=False)
        self.bus.send(msg)
        time.sleep(0.1)
        
        if self.wait_for_ack(self.CMD_GET_VERSION):
            # Read version data
            response = self.bus.recv(timeout=1.0)
            if response:
                print(f"✓ Bootloader version: 0x{response.data[0]:02X}")
                return True
        
        print("✗ Failed to get version")
        return False
    
    def get_chip_id(self):
        """Get chip ID"""
        print("Getting chip ID...")
        
        # Send command: ID=0x02, Data=0x02
        msg = can.Message(arbitration_id=self.CMD_GET_ID, data=[0x02], is_extended_id=False)
        self.bus.send(msg)
        time.sleep(0.1)
        
        if self.wait_for_ack(self.CMD_GET_ID):
            response = self.bus.recv(timeout=1.0)
            if response and len(response.data) >= 2:
                chip_id = (response.data[0] << 8) | response.data[1]
                print(f"✓ Chip ID: 0x{chip_id:04X}")
                return True
        
        print("✗ Failed to get chip ID")
        return False
    
    def erase_flash(self, start_sector=1, num_sectors=15):
        """Erase flash sectors (skip sector 0 - bootloader)"""
        print(f"Erasing sectors {start_sector} to {start_sector + num_sectors - 1}...")
        
        for sector in range(start_sector, start_sector + num_sectors):
            print(f"  Erasing sector {sector}...", end='')
            
            # Step 1: Send erase command with [0, 1]
            msg = can.Message(
                arbitration_id=self.CMD_ERASE, 
                data=[0x00, 0x01],
                is_extended_id=False
            )
            self.bus.send(msg)
            time.sleep(0.05)
            
            # Wait for first ACK
            if not self.wait_for_ack(self.CMD_ERASE,timeout=1.0):
                print(" ✗ Failed (no first ACK)")
                return False
            
            # Wait for second ACK
            if not self.wait_for_ack(self.CMD_ERASE,timeout=1.0):
                print(" ✗ Failed (no second ACK)")
                return False
            
            # Step 2: Send sector number [0, sector]
            msg = can.Message(
                arbitration_id=self.CMD_ERASE,
                data=[0x00, sector],
                is_extended_id=False
            )
            self.bus.send(msg)
            
            # Wait for ACK (erase takes time)
            if not self.wait_for_ack(self.CMD_ERASE,timeout=10.0):
                print(" ✗ Failed")
                return False
            
            print(" ✓")
            time.sleep(0.1)
        
        print(f"✓ Successfully erased {num_sectors} sectors")
        return True
    
    def write_memory(self, address, data):
        """Write data to memory (max 256 bytes)"""
        if len(data) > 256:
            print("✗ Data too large (max 256 bytes)")
            return False
        
        # Step 1: Send write command with address and size
        # ID=0x31, DLC=5, Data=[addr0, addr1, addr2, addr3, size-1]
        addr_bytes = [
            (address >> 24) & 0xFF,
            (address >> 16) & 0xFF,
            (address >> 8) & 0xFF,
            address & 0xFF,
            len(data) - 1  # Size (0xFF = 256 bytes)
        ]
        
        msg = can.Message(
            arbitration_id=self.CMD_WRITE_MEMORY,
            data=addr_bytes,
            is_extended_id=False
        )
        self.bus.send(msg)
        time.sleep(0.00001)
        
        # Wait for ACK
        if not self.wait_for_ack(self.CMD_WRITE_MEMORY):
            return False
        
        # Step 2: Send data in chunks of 8 bytes
        for i in range(0, len(data), 8):
            chunk = data[i:i+8]
            msg = can.Message(
                arbitration_id=self.CMD_WRITE_MEMORY,
                data=list(chunk),
                is_extended_id=False
            )
            self.bus.send(msg)
            # time.sleep(0.00001)
        
        # Wait for final ACK after all data received
        if not self.wait_for_ack(self.CMD_WRITE_MEMORY,timeout=2.0):
            return False
        
        return True
    
    def read_memory(self, address, size):
        """Read memory"""
        print(f"Reading {size} bytes from 0x{address:08X}...")
        
        # Send read command with address and size
        addr_bytes = [
            (address >> 24) & 0xFF,
            (address >> 16) & 0xFF,
            (address >> 8) & 0xFF,
            address & 0xFF,
            size - 1
        ]
        
        msg = can.Message(
            arbitration_id=self.CMD_READ_MEMORY,
            data=addr_bytes,
            is_extended_id=False
        )
        self.bus.send(msg)
        time.sleep(0.1)
        
        if not self.wait_for_ack(self.CMD_READ_MEMORY):
            print("✗ Read command failed")
            return None
        
        # Receive data
        data = []
        while len(data) < size:
            response = self.bus.recv(timeout=1.0)
            if response and response.arbitration_id == self.CMD_READ_MEMORY:
                data.extend(response.data[:min(8, size - len(data))])
        
        print(f"✓ Read {len(data)} bytes")
        return bytes(data)
    
    def jump_to_application(self):
        """Jump to application at 0x08020000"""
        print(f"Jumping to application at 0x{self.APP_ADDRESS:08X}...")
        
        # Send GO command with address
        addr_bytes = [
            (self.APP_ADDRESS >> 24) & 0xFF,
            (self.APP_ADDRESS >> 16) & 0xFF,
            (self.APP_ADDRESS >> 8) & 0xFF,
            self.APP_ADDRESS & 0xFF,
        ]
        
        msg = can.Message(
            arbitration_id=self.CMD_GO,
            data=addr_bytes,
            is_extended_id=False
        )
        self.bus.send(msg)
        time.sleep(0.1)
        
        if self.wait_for_ack(self.CMD_GO):
            print("✓ Jumped to application")
            return True
        else:
            print("✗ Jump failed")
            return False
    
    def flash_firmware(self, firmware_file):
        """Complete firmware flashing sequence"""
        print("=" * 60)
        print(f"STM32H7 CAN Bootloader - Flashing {firmware_file}")
        print("=" * 60)
        
        # Read firmware
        try:
            with open(firmware_file, 'rb') as f:
                firmware = f.read()
            print(f"✓ Firmware loaded: {len(firmware)} bytes\n")
        except Exception as e:
            print(f"✗ Failed to read firmware: {e}")
            return False
        
        # Detect bootloader
        while not self.detect_bootloader():
            time.sleep(0.01)

        if not self.detect_bootloader():
            return False
        print()
        
        # Get version and chip ID (optional but useful)
        self.get_bootloader_version()
        self.get_chip_id()
        print()
        
        # Erase flash (sectors 1-15, keep sector 0)
        if not self.erase_flash(start_sector=1, num_sectors=15):
            return False
        print()
        
        # Write firmware
        print("Writing firmware...")
        chunk_size = 256
        address = self.APP_ADDRESS
        
        for i in range(0, len(firmware), chunk_size):
            chunk = firmware[i:i+chunk_size]
            progress = (i * 100) // len(firmware)
            
            print(f"  0x{address:08X}: {progress:3d}% ", end='', flush=True)
            
            if self.write_memory(address, chunk):
                print("✓")
            else:
                print("✗ Write failed!")
                return False
            
            address += len(chunk)
        
        print("✓ Firmware written successfully\n")
        
        # Jump to application
        time.sleep(0.5)
        return self.jump_to_application()


def find_esp32_port():
    """Find ESP32 port on macOS"""
    patterns = [
        '/dev/cu.usbserial-*',
        '/dev/cu.SLAB_USBtoUART*',
        '/dev/cu.wchusbserial*',
        '/dev/cu.usbmodem*',
    ]
    
    ports = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    
    return ports


def main():
    print("\n" + "=" * 60)
    print("STM32H7 CAN Bootloader Flash Tool")
    print("=" * 60 + "\n")
    
    # Check arguments
    if len(sys.argv) < 2:
        print("Usage: python stm32_flash.py <firmware.bin> [serial_port]")
        print("\nExample:")
        print("  python stm32_flash.py application.bin")
        print("  python stm32_flash.py application.bin /dev/cu.usbserial-0001")
        sys.exit(1)
    
    firmware_file = sys.argv[1]
    
    # Find or use specified port
    if len(sys.argv) >= 3:
        port = sys.argv[2]
        print(f"Using specified port: {port}")
    else:
        ports = find_esp32_port()
        
        if not ports:
            print("✗ No serial port found!")
            print("\nAvailable ports:")
            import subprocess
            subprocess.run(['ls', '/dev/cu.*'])
            sys.exit(1)
        
        if len(ports) == 1:
            port = ports[0]
            print(f"✓ Auto-detected port: {port}")
        else:
            print("Multiple ports found:")
            for i, p in enumerate(ports, 1):
                print(f"  {i}. {p}")
            choice = int(input("Select port: ")) - 1
            port = ports[choice]
    
    print(f"Firmware: {firmware_file}")
    print(f"Port: {port}")
    print(f"Bitrate: 500000\n")
    
    try:
        # Open CAN bus
        print("Opening CAN bus...")
        bus = can.Bus(channel=port, interface='slcan', bitrate=250000)
        print("✓ CAN bus opened\n")
        
        # Create flasher
        flasher = STM32H7CANFlasher(bus)
        
        # Flash firmware
        success = flasher.flash_firmware(firmware_file)
        
        # Close bus
        bus.shutdown()
        
        print("\n" + "=" * 60)
        if success:
            print("✓ FLASHING COMPLETED SUCCESSFULLY!")
        else:
            print("✗ FLASHING FAILED!")
        print("=" * 60 + "\n")
        
        sys.exit(0 if success else 1)
        
    except KeyboardInterrupt:
        print("\n\n✗ Interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()