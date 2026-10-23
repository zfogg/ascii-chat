"""Check that terminal backpressure measurements cannot pass without receive samples."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import unittest

from network_terminal_backpressure import receive_intervals, receive_times


class ReceiveMeasurements(unittest.TestCase):
    def test_measures_callbacks_without_lag_warnings(self):
        text = "\n".join([
            "[12:00:00.000000] [DEBUG] CALLING_DISPLAY_RENDER: frame_data=0x1",
            "[12:00:00.016667] [DEBUG] CALLING_DISPLAY_RENDER: frame_data=0x2",
            "[12:00:00.033334] [DEBUG] CALLING_DISPLAY_RENDER: frame_data=0x3",
        ])
        samples = receive_times(text)
        intervals = receive_intervals(samples[:1], samples[1:])
        self.assertEqual(len(intervals), 2)
        self.assertAlmostEqual(max(intervals), 16.667)

    def test_missing_samples_fail(self):
        self.assertEqual(receive_times("[12:00:00.000000] unrelated log"), [])
        for previous, samples in [([], [10, 20]), ([10], []), ([10], [20])]:
            with self.subTest(previous=previous, samples=samples):
                with self.assertRaises(AssertionError):
                    receive_intervals(previous, samples)

    def test_stall_is_measured(self):
        self.assertEqual(max(receive_intervals([10], [2010, 2027])), 2000)

    def test_midnight_rollover(self):
        self.assertEqual(receive_intervals([86399990], [10, 30]), [20, 20])


if __name__ == "__main__":
    unittest.main()
