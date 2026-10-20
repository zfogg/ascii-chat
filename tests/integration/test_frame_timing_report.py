"""Check frame associations and latency boundaries in diagnostic reports."""
import unittest

from frame_timing_report import summarize


class FrameTimingReportTest(unittest.TestCase):
    def test_joins_matching_frame_and_keeps_read_wait_separate(self):
        report = summarize("\n".join([
            "CAMERA_COPY: request_ns=1000100 sample_ns=11000000 complete_ns=12000000 device_age_ms=17.5",
            "FRAME_PRESENT: id=3 acquired_ns=1000000 read_ms=11.0",
            "TERMINAL_ACK: id=3 capture_to_terminal_ms=50 response_ms=20",
        ]))
        self.assertEqual(report["joined.sample_arrival_to_terminal_ms"]["median"], 40)
        self.assertEqual(report["joined.device_timestamp_to_terminal_ms"]["median"], 57.5)
        self.assertEqual(report["TERMINAL_ACK.capture_to_terminal_ms"]["median"], 50)

    def test_does_not_match_unrelated_capture_or_unknown_device_clock(self):
        report = summarize("\n".join([
            "CAMERA_COPY: request_ns=10000000 sample_ns=12000000 device_age_ms=-1",
            "FRAME_PRESENT: id=1 acquired_ns=1000000",
            "TERMINAL_ACK: id=1 capture_to_terminal_ms=50",
            "FRAME_PRESENT: id=2 acquired_ns=9999999",
            "TERMINAL_ACK: id=2 capture_to_terminal_ms=50",
            "TERMINAL_ACK: id=999 capture_to_terminal_ms=50",
        ]))
        self.assertEqual(report["joined.sample_arrival_to_terminal_ms"]["count"], 1)
        self.assertNotIn("joined.device_timestamp_to_terminal_ms", report)


if __name__ == "__main__":
    unittest.main()
