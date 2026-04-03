import can
import threading
import serial

# Use unified interface API
bus = can.interface.Bus(
    channel="/dev/ttyACM5",
    interface="slcan",
    bitrate=250000
)


def keyboard_listener():
    while True:
        user_input = input()

        if user_input == "1":
            print("sending message")

            tx_msg = can.Message(
                arbitration_id=0x0816FAFC,
                data=[0x01, 0x01, 0x40, 0x02, 0x00, 0x00, 0x00, 0x00],
                is_extended_id=True
            )

            try:
                bus.send(tx_msg)
                print("TX:", tx_msg)
            except can.CanError as e:
                print("Send failed:", e)


# Start keyboard thread
threading.Thread(target=keyboard_listener, daemon=True).start()

print("Listening on CAN bus... Press 1 + Enter to send\n")

try:
    while True:
        msg = bus.recv(timeout=1)
        if msg:
            print("RX:", msg)

finally:
    print("Closing the bus")
    bus.shutdown()
