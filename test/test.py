#!/usr/bin/env python3
"""
Python-CAN examples using SLCAN interface
Works with ESP32 SLCAN device on macOS
"""

import can
import time
import sys

# ============================================================================
# Configuration
# ============================================================================
SERIAL_PORT = '/dev/cu.usbserial-0001'  # Change this to your ESP32 port
BITRATE = 250000  # 500 kbit/s
CHANNEL = SERIAL_PORT
INTERFACE = 'slcan'

# ============================================================================
# Example 1: CAN Dump - Listen to all CAN messages
# ============================================================================
def example_candump(duration=10):
    """
    Similar to Linux candump command
    Listens and prints all CAN messages
    """
    print(f"=== CAN DUMP ===")
    print(f"Listening on {SERIAL_PORT} for {duration} seconds...")
    print(f"Press Ctrl+C to stop\n")
    
    try:
        # Create SLCAN bus
        bus = can.interface.Bus(
            channel=CHANNEL,
            interface=INTERFACE,
            bitrate=BITRATE
        )
        
        print("Interface opened successfully!")
        print("Waiting for messages...\n")
        
        start_time = time.time()
        msg_count = 0
        
        while time.time() - start_time < duration:
            # Receive message with timeout
            msg = bus.recv(timeout=1.0)
            
            if msg is not None:
                msg_count += 1
                
                # Format similar to candump output
                timestamp = msg.timestamp if msg.timestamp else time.time()
                can_id = f"{msg.arbitration_id:03X}" if not msg.is_extended_id else f"{msg.arbitration_id:08X}"
                
                # Format data bytes
                data_str = ' '.join([f'{b:02X}' for b in msg.data])
                
                # Print message
                id_type = "Extended" if msg.is_extended_id else "Standard"
                rtr_flag = "RTR" if msg.is_remote_frame else ""
                
                print(f"  ({timestamp:.6f})  ID: 0x{can_id} [{len(msg.data)}] {data_str}  {rtr_flag}")
        
        print(f"\n✓ Received {msg_count} messages in {duration} seconds")
        bus.shutdown()
        
    except can.CanError as e:
        print(f"✗ CAN Error: {e}")
    except KeyboardInterrupt:
        print("\n✓ Stopped by user")
    finally:
        try:
            bus.shutdown()
        except:
            pass

# ============================================================================
# Example 2: CAN Send - Send CAN messages
# ============================================================================
def example_cansend():
    """
    Similar to Linux cansend command
    Sends CAN messages
    """
    print(f"=== CAN SEND ===")
    print(f"Sending messages on {SERIAL_PORT}...\n")
    
    try:
        # Create SLCAN bus
        bus = can.interface.Bus(
            channel=CHANNEL,
            interface=INTERFACE,
            bitrate=BITRATE
        )
        
        print("Interface opened successfully!\n")
        
        # Define test messages
        messages = [
            # Standard ID messages
            can.Message(
                arbitration_id=0x123,
                data=[0xDE, 0xAD, 0xBE, 0xEF],
                is_extended_id=False
            ),
            can.Message(
                arbitration_id=0x456,
                data=[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88],
                is_extended_id=False
            ),
            # Extended ID message
            can.Message(
                arbitration_id=0x12345678,
                data=[0xCA, 0xFE, 0xBA, 0xBE],
                is_extended_id=True
            ),
            # RTR (Remote Transmission Request)
            can.Message(
                arbitration_id=0x100,
                data=[],
                is_extended_id=False,
                is_remote_frame=True,
                dlc=4
            ),
        ]
        
        # Send each message
        for i, msg in enumerate(messages, 1):
            try:
                bus.send(msg)
                
                id_str = f"0x{msg.arbitration_id:03X}" if not msg.is_extended_id else f"0x{msg.arbitration_id:08X}"
                data_str = ' '.join([f'{b:02X}' for b in msg.data])
                msg_type = "Extended" if msg.is_extended_id else "Standard"
                rtr = " (RTR)" if msg.is_remote_frame else ""
                
                print(f"✓ Sent message {i}: ID={id_str} [{len(msg.data)}] {data_str} {msg_type}{rtr}")
                time.sleep(0.5)
                
            except can.CanError as e:
                print(f"✗ Failed to send message {i}: {e}")
        
        print(f"\n✓ All messages sent!")
        bus.shutdown()
        
    except can.CanError as e:
        print(f"✗ CAN Error: {e}")
    except KeyboardInterrupt:
        print("\n✓ Stopped by user")
    finally:
        try:
            bus.shutdown()
        except:
            pass

# ============================================================================
# Example 3: CAN Player - Replay recorded messages
# ============================================================================
def example_canplayer():
    """
    Send periodic CAN messages (like a simulator)
    """
    print(f"=== CAN PLAYER ===")
    print(f"Sending periodic messages on {SERIAL_PORT}...")
    print(f"Press Ctrl+C to stop\n")
    
    try:
        bus = can.interface.Bus(
            channel=CHANNEL,
            interface=INTERFACE,
            bitrate=BITRATE
        )
        
        print("Interface opened successfully!\n")
        
        # Periodic tasks
        task1 = bus.send_periodic(
            can.Message(
                arbitration_id=0x100,
                data=[0x01, 0x02, 0x03, 0x04]
            ),
            period=1.0  # Send every 1 second
        )
        
        task2 = bus.send_periodic(
            can.Message(
                arbitration_id=0x200,
                data=[0xAA, 0xBB, 0xCC, 0xDD]
            ),
            period=0.5  # Send every 500ms
        )
        
        print("✓ Periodic messages started:")
        print("  - ID 0x100: Every 1.0 second")
        print("  - ID 0x200: Every 0.5 seconds")
        print("\nPress Ctrl+C to stop...\n")
        
        # Keep running
        while True:
            time.sleep(1)
        
    except KeyboardInterrupt:
        print("\n✓ Stopped by user")
        task1.stop()
        task2.stop()
        bus.shutdown()

# ============================================================================
# Example 4: CAN Filter - Listen to specific IDs only
# ============================================================================
def example_canfilter(filter_ids=[0x123, 0x456]):
    """
    Listen only to specific CAN IDs (filtered reception)
    """
    print(f"=== CAN FILTER ===")
    print(f"Listening only to IDs: {[hex(id) for id in filter_ids]}")
    print(f"Duration: 10 seconds\n")
    
    try:
        # Create filters
        filters = [{"can_id": id, "can_mask": 0x7FF} for id in filter_ids]
        
        bus = can.interface.Bus(
            channel=CHANNEL,
            interface=INTERFACE,
            bitrate=BITRATE,
            can_filters=filters
        )
        
        print("Interface opened with filters!\n")
        
        start_time = time.time()
        msg_count = 0
        
        while time.time() - start_time < 10:
            msg = bus.recv(timeout=1.0)
            
            if msg is not None:
                msg_count += 1
                data_str = ' '.join([f'{b:02X}' for b in msg.data])
                print(f"  Received: ID=0x{msg.arbitration_id:03X} [{len(msg.data)}] {data_str}")
        
        print(f"\n✓ Received {msg_count} filtered messages")
        bus.shutdown()
        
    except can.CanError as e:
        print(f"✗ CAN Error: {e}")
    except KeyboardInterrupt:
        print("\n✓ Stopped by user")

# ============================================================================
# Example 5: Interactive CAN Terminal
# ============================================================================
def example_interactive():
    """
    Interactive CAN terminal - send and receive simultaneously
    """
    print(f"=== INTERACTIVE CAN TERMINAL ===")
    print(f"Port: {SERIAL_PORT}\n")
    
    try:
        bus = can.interface.Bus(
            channel=CHANNEL,
            interface=INTERFACE,
            bitrate=BITRATE
        )
        
        print("Interface opened successfully!")
        print("\nCommands:")
        print("  send <id> <data>  - Send message (e.g., 'send 123 DEADBEEF')")
        print("  listen            - Start listening for messages")
        print("  quit              - Exit\n")
        
        listening = False
        
        while True:
            # Check for received messages if listening
            if listening:
                msg = bus.recv(timeout=0.1)
                if msg is not None:
                    data_str = ' '.join([f'{b:02X}' for b in msg.data])
                    print(f"  RX: ID=0x{msg.arbitration_id:03X} [{len(msg.data)}] {data_str}")
            
            # Non-blocking input check
            import select
            if select.select([sys.stdin], [], [], 0.1)[0]:
                cmd = input().strip().lower()
                
                if cmd == 'quit':
                    break
                elif cmd == 'listen':
                    listening = not listening
                    print(f"✓ Listening: {'ON' if listening else 'OFF'}")
                elif cmd.startswith('send '):
                    try:
                        parts = cmd.split()
                        can_id = int(parts[1], 16)
                        data_hex = parts[2] if len(parts) > 2 else ""
                        data_bytes = bytes.fromhex(data_hex)
                        
                        msg = can.Message(
                            arbitration_id=can_id,
                            data=data_bytes,
                            is_extended_id=(can_id > 0x7FF)
                        )
                        bus.send(msg)
                        print(f"✓ Sent: ID=0x{can_id:03X} [{len(data_bytes)}] {data_hex}")
                    except Exception as e:
                        print(f"✗ Error: {e}")
                else:
                    print("✗ Unknown command")
        
        bus.shutdown()
        print("\n✓ Terminal closed")
        
    except can.CanError as e:
        print(f"✗ CAN Error: {e}")
    except KeyboardInterrupt:
        print("\n✓ Stopped by user")

# ============================================================================
# Main Menu
# ============================================================================
def main():
    print("=" * 60)
    print("Python-CAN SLCAN Examples for macOS")
    print("=" * 60)
    print(f"\nConfigured port: {SERIAL_PORT}")
    print(f"Bitrate: {BITRATE} bit/s\n")
    
    print("Select example:")
    print("  1. CAN Dump (listen to all messages)")
    print("  2. CAN Send (send test messages)")
    print("  3. CAN Player (periodic messages)")
    print("  4. CAN Filter (filtered reception)")
    print("  5. Interactive Terminal")
    print("  q. Quit\n")
    
    choice = input("Choice: ").strip()
    print()
    
    if choice == '1':
        example_candump()
    elif choice == '2':
        example_cansend()
    elif choice == '3':
        example_canplayer()
    elif choice == '4':
        example_canfilter()
    elif choice == '5':
        example_interactive()
    elif choice.lower() == 'q':
        print("Goodbye!")
    else:
        print("Invalid choice")

if __name__ == "__main__":
    # First, check if we can find the port
    import glob
    
    ports = glob.glob('/dev/cu.usbserial-*') + glob.glob('/dev/cu.SLAB_USBtoUART*') + \
            glob.glob('/dev/cu.wchusbserial*') + glob.glob('/dev/cu.usbmodem*')
    
    if ports:
        print("Found ESP32 ports:")
        for port in ports:
            print(f"  {port}")
        
        if len(ports) == 1:
            SERIAL_PORT = ports[0]
            CHANNEL = SERIAL_PORT
            print(f"\nAuto-selected: {SERIAL_PORT}\n")
        else:
            print("\nUpdate SERIAL_PORT variable in the script with your port")
            print("Or set it now:")
            custom_port = input(f"Port [{SERIAL_PORT}]: ").strip()
            if custom_port:
                SERIAL_PORT = custom_port
                CHANNEL = SERIAL_PORT
    
    main()