#!/usr/bin/env python3
"""Reset a board through its serial port and stream what it prints (slice 0003).

The device checks need the boot log, so they cannot attach to a board that is already
running. Opening the port with DTR and RTS high, as pyserial and `pio device monitor` do,
does not reset it (esp32-dragon-radar slice 0011), so this script resets it explicitly
with esptool's hard-reset sequence, then prints each line to stdout until it is killed.

With --send-stdin, every byte read from stdin is written to the board, so a host script can
send the product's one-byte commands (slice 0011). With --no-reset, it attaches to the board
as it runs: a session picked up again after a screen capture, which holds the port meanwhile.

Usage: python3 scripts/serial_reset_monitor.py --port /dev/ttyACM0 [--send-stdin] [--no-reset]
Requires pyserial (PlatformIO installs it).
"""
import argparse
import os
import sys
import threading
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


def forward_stdin(port):
    """Write each byte from stdin to the board until stdin closes.

    A write that fails ends the whole monitor: a thread that died alone would leave it reading
    as if healthy while every later command went nowhere.
    """
    while True:
        chunk = sys.stdin.buffer.read1(1)
        if not chunk:
            return
        try:
            port.write(chunk)
        except serial.SerialException as error:
            sys.stderr.write(f"serial_reset_monitor: could not send to the board: {error}\n")
            sys.stderr.flush()
            os._exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--send-stdin", action="store_true", help="write stdin's bytes to the board")
    parser.add_argument("--no-reset", action="store_true", help="attach to the running board")
    args = parser.parse_args()

    port = open_without_reset(args.port)
    try:
        if not args.no_reset:
            hard_reset(port)
        if args.send_stdin:
            # A daemon: the monitor ends when it is killed, whatever stdin is doing.
            threading.Thread(target=forward_stdin, args=(port,), daemon=True).start()
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
