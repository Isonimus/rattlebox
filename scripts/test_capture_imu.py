"""Tests for capture_imu.py (slice 0004). Run: python3 -m unittest discover -s scripts"""
import csv
import io
import os
import struct
import tempfile
import unittest

from capture_imu import WindowError, check_gaps, read_window, to_rows, write_csv

RATE_HZ = 1000
PERIOD_US = 1000
# Written out rather than imported, so a wrong record layout in the reader cannot agree
# with itself: uint32 timestamp, then six int16 counts, little-endian.
RECORD = struct.Struct("<I6h")


def records_at(start_us, count, period_us=PERIOD_US):
    return [(start_us + i * period_us, 100 * i, -100 * i, 2048, 7, -7, 0) for i in range(count)]


def window_stream(records, declared_count=None, declared_bytes=None, trailer=b"RBEND\n", preamble=b"",
                  body_bytes=None):
    """The bytes the probe firmware sends for one window, optionally with log lines ahead of it."""
    body = b"".join(RECORD.pack(*record) for record in records) if body_bytes is None else body_bytes
    count = len(records) if declared_count is None else declared_count
    size = len(body) if declared_bytes is None else declared_bytes
    header = f"RBIMU {RATE_HZ} 16 2000 {count} {size}\n".encode()
    return io.BytesIO(preamble + header + body + trailer)


class ReadWindowTest(unittest.TestCase):
    def test_reads_the_window_after_skipping_log_lines(self):
        records = records_at(5000, 4)
        window = read_window(window_stream(records, preamble=b"[boot] imu OK: x\r\n[probe] ignored 1 byte(s)\n"))
        self.assertEqual((window.rate_hz, window.accel_range_g, window.gyro_range_dps), (1000, 16, 2000))
        self.assertEqual(window.records, records)

    def test_counts_are_signed(self):
        records = [(1, -32768, 32767, -1, 1, -2, 2)]
        self.assertEqual(read_window(window_stream(records)).records, records)

    def test_a_byte_count_that_is_not_16_per_sample_is_refused(self):
        with self.assertRaisesRegex(WindowError, "16 bytes"):
            read_window(window_stream(records_at(0, 2), declared_bytes=30))

    def test_a_window_that_ends_early_is_refused(self):
        body = b"".join(RECORD.pack(*record) for record in records_at(0, 2))
        stream = window_stream(records_at(0, 3), declared_bytes=48, body_bytes=body, trailer=b"")
        with self.assertRaisesRegex(WindowError, "32 of 48 bytes"):
            read_window(stream)

    def test_a_short_window_quotes_what_the_board_said_about_it(self):
        # The board reports a short write in-line, where the host is still counting body
        # bytes; here what arrived, note included, is less than declared.
        body = b"".join(RECORD.pack(*record) for record in records_at(0, 10))
        note = b"\n[probe] only 20 of 160 bytes sent; window abandoned\n"
        stream = window_stream(records_at(0, 10), body_bytes=body[:20] + note, declared_bytes=160, trailer=b"")
        with self.assertRaisesRegex(WindowError, r"ended early.*board said: \[probe\] only 20 of 160 bytes sent"):
            read_window(stream)

    def test_a_note_that_fills_the_window_is_quoted_from_across_the_end(self):
        # Here the note runs past the declared length, so it straddles body and trailer.
        body = b"".join(RECORD.pack(*record) for record in records_at(0, 3))
        note = b"\n[probe] only 20 of 48 bytes sent; window abandoned\n"
        stream = window_stream(records_at(0, 3), body_bytes=body[:20] + note, declared_bytes=48, trailer=b"")
        with self.assertRaisesRegex(WindowError, r"RBEND.*board said: \[probe\] only 20 of 48 bytes sent; window abandoned"):
            read_window(stream)

    def test_bytes_lost_with_no_note_are_named_as_a_link_loss(self):
        body = b"".join(RECORD.pack(*record) for record in records_at(0, 3))
        stream = window_stream(records_at(0, 3), body_bytes=body[:40], declared_bytes=48, trailer=b"")
        with self.assertRaisesRegex(WindowError, "lost on the link"):
            read_window(stream)

    def test_a_window_without_its_end_line_is_refused(self):
        with self.assertRaisesRegex(WindowError, "RBEND"):
            read_window(window_stream(records_at(0, 2), trailer=b"[probe] only 20 of 32 bytes sent\n"))

    def test_a_failure_the_board_reports_is_raised_with_its_reason(self):
        stream = io.BytesIO(b"[probe] FAILED: IMU bus error after 12 samples\n")
        with self.assertRaisesRegex(WindowError, "IMU bus error after 12 samples"):
            read_window(stream)

    def test_a_board_that_never_answers_is_refused(self):
        stream = io.BytesIO(b"[imu] 1 0 0 1 0 0 0\n" * 30)
        with self.assertRaisesRegex(WindowError, "no RBIMU header"):
            read_window(stream)

    def test_a_reset_on_open_is_named(self):
        stream = io.BytesIO(b"rst:0x1 (POWERON_RESET),boot:0x13\n")
        with self.assertRaisesRegex(WindowError, "reset"):
            read_window(stream)


class CheckGapsTest(unittest.TestCase):
    def test_jitter_under_one_and_a_half_periods_passes(self):
        timestamps = [0, 1000, 2400, 3000, 4100]
        check_gaps(timestamps, RATE_HZ)

    def test_a_missed_sample_is_refused_with_where_and_how_many(self):
        timestamps = [0, 1000, 2000, 4000, 5000, 7100]
        with self.assertRaisesRegex(WindowError, r"2 gap\(s\).*largest 2100 us at sample 5"):
            check_gaps(timestamps, RATE_HZ)

    def test_a_timestamp_that_goes_backwards_is_refused(self):
        with self.assertRaisesRegex(WindowError, "gap"):
            check_gaps([5000, 4000], RATE_HZ)

    def test_the_microsecond_counter_wrapping_is_not_a_gap(self):
        top = 2**32
        check_gaps([top - 1500, top - 500, 500, 1500], RATE_HZ)


class RowsTest(unittest.TestCase):
    def test_counts_scale_to_g_and_dps(self):
        window = read_window(window_stream([(10, 2048, -4096, 0, 16384, -32, 0)]))
        (row,) = to_rows(window)
        self.assertEqual(row[:7], [10, 1.0, -2.0, 0.0, 1000.0, -1.953125, 0.0])
        self.assertEqual(row[7:], [0, 0])

    def test_either_end_of_the_range_is_saturated(self):
        window = read_window(window_stream([
            (0, 32767, 0, 0, 0, 0, 0),
            (1, 0, -32768, 0, 0, 0, 0),
            (2, 0, 0, 0, 0, 0, 32767),
            (3, 32766, -32767, 0, -32767, 0, 0),
        ]))
        flags = [row[7:] for row in to_rows(window)]
        self.assertEqual(flags, [[1, 0], [1, 0], [0, 1], [0, 0]])

    def test_the_csv_has_a_header_and_one_row_per_sample(self):
        window = read_window(window_stream(records_at(0, 3)))
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "take.csv")
            write_csv(path, window)
            with open(path, newline="") as handle:
                rows = list(csv.reader(handle))
        self.assertEqual(rows[0], ["t_us", "ax", "ay", "az", "gx", "gy", "gz", "accel_saturated", "gyro_saturated"])
        self.assertEqual(len(rows), 4)
        self.assertEqual(rows[2][:2], ["1000", "0.048828125"])


if __name__ == "__main__":
    unittest.main()
