#!/usr/bin/env python3
"""Reset a board through its serial port and stream what it prints (slice 0003).

The device checks need the boot log, so they cannot attach to a board that is already
running. Opening the port with DTR and RTS high, as pyserial and `pio device monitor` do,
does not reset it (esp32-dragon-radar slice 0011), so this script resets it explicitly
with esptool's hard-reset sequence, then prints each line to stdout until it is killed.

Usage: python3 scripts/serial_reset_monitor.py --port /dev/ttyACM0
Requires pyserial (PlatformIO installs it).
"""
import argparse
import sys
import time

import serial

CONSOLE_BAUD = 115200
# esptool's hard reset: RTS asserted with DTR released holds the chip's EN low, through
# the auto-reset transistors on a USB-UART board and in the S3's USB-Serial-JTAG alike.
RESET_HOLD_S = 0.1
READ_TIMEOUT_S = 0.5


def open_without_reset(path):
    port = serial.Serial()
    port.port = path
    port.baudrate = CONSOLE_BAUD
    port.timeout = READ_TIMEOUT_S
    port.dtr = True
    port.rts = True
    port.open()
    return port


def hard_reset(port):
    port.dtr = False
    port.rts = True
    time.sleep(RESET_HOLD_S)
    port.rts = False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyACM0")
    args = parser.parse_args()

    port = open_without_reset(args.port)
    try:
        hard_reset(port)
        while True:
            line = port.readline()
            if line:
                sys.stdout.write(line.decode("utf-8", errors="replace").rstrip("\r\n") + "\n")
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        port.close()


if __name__ == "__main__":
    main()
