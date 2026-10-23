#!/usr/bin/env python3
"""Record one window of IMU samples from a board running the IMU probe firmware, as CSV
(slice 0004).

The firmware answers an `R` byte by recording a window, then sends
`RBIMU <rate_hz> <accel_range_g> <gyro_range_dps> <count> <bytes>`, the samples as 16-byte
records (uint32 timestamp in us, six int16 device-frame counts of full scale,
little-endian), and `RBEND`. This script asks, checks the reply and that no sample was
missed, writes the CSV, and prints a one-line JSON summary for scripts/imu-probe.mjs.

Usage: python3 scripts/capture_imu.py --port /dev/ttyACM0 --out take.csv
Requires pyserial (PlatformIO installs it) and Pillow (capture_screen's). Run from the repo root.
"""
import argparse
import csv
import json
import struct
import sys
from collections import namedtuple

from capture_screen import open_port

RECORD_REQUEST = b"R"
HEADER_TAG = b"RBIMU"
END_LINE = b"RBEND"
PROBE_TAG = b"[probe]"
FAILURE_TAG = PROBE_TAG + b" FAILED"
RESET_BANNER = b"rst:0x"
RECORD = struct.Struct("<I6h")
# Must match FULL_SCALE_COUNTS in src/imu/imu_decode.h, which packed the counts.
FULL_SCALE_COUNTS = 32768
SATURATED_COUNTS = (-32768, 32767)
# The request is sent right after the input buffer is cleared, so only the boot log or a
# stray line can come first; more than this means the board is not running the probe.
MAX_LINES_BEFORE_HEADER = 20
# A sample is missed when the next one comes this many periods late. The firmware polls the
# chip's data-ready flag, so ordinary jitter stays well under one period.
MAX_GAP_PERIODS = 1.5
TIMESTAMP_MODULUS = 2**32
CSV_HEADER = ["t_us", "ax", "ay", "az", "gx", "gy", "gz", "accel_saturated", "gyro_saturated"]
# Recording takes 5 s and the StickC's 115200-baud bridge about 7 s to send 80 KB.
DEFAULT_TIMEOUT_S = 20.0

Window = namedtuple("Window", "rate_hz accel_range_g gyro_range_dps records")


class WindowError(Exception):
    """The board's reply is not a whole, gap-free window."""


def read_window(stream):
    """Reads one reply from `stream` (anything with readline and read). Log lines ahead of
    the header are skipped."""
    for _ in range(MAX_LINES_BEFORE_HEADER):
        line = stream.readline()
        if not line:
            break
        if line.startswith(HEADER_TAG):
            return _read_body(stream, line)
        if line.startswith(FAILURE_TAG):
            raise WindowError(f"the board reported {line.strip().decode(errors='replace')}")
        if line.startswith(RESET_BANNER):
            raise WindowError(
                "the board reset when the port opened; open_port must leave DTR and RTS high"
            )
    raise WindowError("no RBIMU header from the board; is it running the IMU probe firmware?")


def _read_body(stream, header):
    fields = header.split()
    if len(fields) != 6 or not all(field.isdigit() for field in fields[1:]):
        raise WindowError(f"malformed header {header!r}")
    rate_hz, accel_range_g, gyro_range_dps, count, declared = (int(field) for field in fields[1:])
    if declared != count * RECORD.size:
        raise WindowError(f"the board sends {declared} bytes for {count} samples, expected {RECORD.size} bytes each")
    body = stream.read(declared)
    if len(body) != declared:
        raise WindowError(f"the window ended early: got {len(body)} of {declared} bytes{_board_note(body, 'the board sent no explanation, so bytes were lost on the link')}")
    trailer = stream.readline()
    if trailer.strip() != END_LINE:
        raise WindowError(
            f"expected RBEND after the samples, got {trailer.strip()[:40]!r}: either a log line was "
            f"written into the window, or bytes were dropped{_board_note(body + trailer)}"
        )
    records = [RECORD.unpack_from(body, offset) for offset in range(0, declared, RECORD.size)]
    return Window(rate_hz, accel_range_g, gyro_range_dps, records)


def _board_note(received, silence="the board sent no explanation of its own"):
    """What the board said about a broken window, if anything: it reports a short write
    in-line, where the host is still counting body bytes."""
    start = received.rfind(PROBE_TAG)
    if start < 0:
        return f"; {silence}"
    # Split outside the f-string: a backslash inside one is a SyntaxError before Python 3.12.
    note = received[start:].split(b"\n")[0].decode(errors="replace")
    return f"; the board said: {note}"


def check_gaps(timestamps, rate_hz):
    """Refuses a window with a missed sample: analysing it would read the gap as stillness."""
    limit_us = MAX_GAP_PERIODS * 1_000_000 / rate_hz
    gaps = []
    for index in range(1, len(timestamps)):
        # Modular, so the 32-bit microsecond counter wrapping mid-window is not a gap.
        interval = (timestamps[index] - timestamps[index - 1]) % TIMESTAMP_MODULUS
        if interval > limit_us:
            gaps.append((interval, index))
    if gaps:
        largest, at = max(gaps)
        raise WindowError(
            f"{len(gaps)} gap(s) over {MAX_GAP_PERIODS} sample periods; largest {largest} us at sample {at}"
        )


def to_rows(window):
    """Each record in g and dps, with a flag for a sensor reading at either end of its range."""
    accel_per_count = window.accel_range_g / FULL_SCALE_COUNTS
    gyro_per_count = window.gyro_range_dps / FULL_SCALE_COUNTS
    rows = []
    for timestamp, *counts in window.records:
        accel, gyro = counts[:3], counts[3:]
        rows.append(
            [timestamp]
            + [count * accel_per_count for count in accel]
            + [count * gyro_per_count for count in gyro]
            + [int(any(count in SATURATED_COUNTS for count in accel)),
               int(any(count in SATURATED_COUNTS for count in gyro))]
        )
    return rows


def write_csv(path, window):
    with open(path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(CSV_HEADER)
        writer.writerows(to_rows(window))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--out", required=True, help="CSV file to write")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S, help="seconds to wait for a reply")
    args = parser.parse_args()

    port = open_port(args.port, args.timeout)
    try:
        port.reset_input_buffer()
        port.write(RECORD_REQUEST)
        window = read_window(port)
    except WindowError as error:
        sys.exit(f"capture_imu: {error}")
    finally:
        port.close()
    try:
        check_gaps([record[0] for record in window.records], window.rate_hz)
    except WindowError as error:
        sys.exit(f"capture_imu: {error}")
    write_csv(args.out, window)
    print(json.dumps({
        "path": args.out,
        "rateHz": window.rate_hz,
        "accelRangeG": window.accel_range_g,
        "gyroRangeDps": window.gyro_range_dps,
        "samples": len(window.records),
    }))


if __name__ == "__main__":
    main()
