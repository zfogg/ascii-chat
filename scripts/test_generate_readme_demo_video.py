"""Run with python -m unittest discover -s scripts -p test_generate_readme_demo_video.py."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).with_name("generate-readme-demo-video.py")
SPEC = importlib.util.spec_from_file_location("readme_demo", SCRIPT)
demo = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = demo
SPEC.loader.exec_module(demo)


class ReadmeDemoTests(unittest.TestCase):
    def test_encoded_audio_rejects_clipping_and_silence(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(ffmpeg="ffmpeg", output=Path(directory) / "demo.mp4", work_dir=Path(directory))
            for peak, rms, valid in [(-4.5, -17, True), (0, -17, False), (-3, -70, False)]:
                result = SimpleNamespace(stderr=f"Overall\nPeak level dB: {peak}\nRMS level dB: {rms}\n")
                with patch.object(demo.subprocess, "run", return_value=result):
                    if valid:
                        self.assertEqual(demo.audio_quality(args), {"peak_dbfs": peak, "rms_dbfs": rms})
                    else:
                        with self.assertRaises(RuntimeError):
                            demo.audio_quality(args)

    def test_terminal_sync_frames_do_not_publish_partial_updates(self):
        class Sink:
            def __init__(self): self.text = ""
            def feed(self, value): self.text += value
        sink = Sink()
        replay = demo.TerminalReplay(sink)
        replay.feed("\x1b[?2026hfirst half of a changing frame")
        self.assertEqual(sink.text, "")
        replay.feed("; second half\x1b[?202")
        self.assertEqual(sink.text, "")
        replay.feed("6l")
        self.assertEqual(sink.text, "first half of a changing frame; second half")

    def test_timestamps_and_invalid_values(self):
        for value, expected in [("0:00", 0), ("0:28", 28), ("1:02:03.5", 3723.5), ("7.25", 7.25)]:
            self.assertEqual(demo.timestamp(value), expected)
        for value in ["-1", "1:60", "1::2", "nan", "inf", "1:2:3:4", "abc"]:
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                demo.timestamp(value)

    def test_tempo_chain_preserves_pitch_across_extreme_speeds(self):
        for speed in [.1, .5, .75, 1, 1.1, 2, 3, 10]:
            product = 1.0
            for step in demo.atempo(speed).split(","):
                factor = float(step.split("=")[1])
                self.assertGreaterEqual(factor, .5)
                self.assertLessEqual(factor, 2)
                product *= factor
            self.assertAlmostEqual(product, speed)

    def test_invalid_speed_and_boolean(self):
        for value in ["0", "-1", "nan", "inf"]:
            with self.assertRaises(argparse.ArgumentTypeError):
                demo.positive(value)
        for value in ["true", "yes", "1", "on"]:
            self.assertTrue(demo.boolean(value))
        for value in ["false", "no", "0", "off"]:
            self.assertFalse(demo.boolean(value))
        with self.assertRaises(argparse.ArgumentTypeError):
            demo.boolean("maybe")

    def test_plan_all_cli_options_and_pixel_cell_mapping(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--dry-run", "--width", "1600", "--height", "900",
                                 "--speed", "1.1", "--media-file-start", "0:28", "--opt-text-overlay=true", "--optimize=false",
                                 "--binary", "unused", "--media-file", "unused", "--face-file", "unused", "--font", "unused",
                                 "--ffmpeg", "unused", "--ffprobe", "unused", "--output", "unused.mp4", "--work-dir", "unused",
                                 "--reuse-capture"], capture_output=True, text=True, check=True)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["terminal"], [200, 56])
        self.assertEqual(plan["output_pixels"], [1600, 900])
        self.assertEqual(plan["fps"], 33)
        self.assertEqual(plan["media_start"], 28)
        self.assertTrue(plan["overlay"])
        self.assertFalse(plan["optimize"])
        baseline = 3 + 6 + sum(c.seconds for c in demo.chapters() + demo.tui_chapters())
        self.assertAlmostEqual(plan["seconds"], baseline / 1.1)

    def test_bad_dimensions_and_output_fail_before_side_effects(self):
        for flags in [["--width", "1279"], ["--height", "400"], ["--output", "out.gif"]]:
            result = subprocess.run([sys.executable, str(SCRIPT), "--dry-run", *flags], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)

    def test_exact_effect_durations_and_restored_state(self):
        chapters = demo.chapters()
        self.assertEqual(sum(c.seconds for c in chapters if c.name.startswith("filter-")), 3)
        self.assertEqual(sum(c.seconds for c in chapters if c.name in ("flip-x", "flip-xy", "flip-y", "unflip")), 3)
        self.assertEqual(sum(c.seconds for c in chapters if c.name in ("foreground", "background", "half-block")), 3)
        self.assertEqual(chapters[-1].name, "face-follow")
        # Simulate the actual mutually exclusive animation keys, checking the
        # labels describe the state that is on screen, not just the last key.
        animation = None
        flip_x = flip_y = False
        filter_index = render_mode = 0
        zalgo = face = False
        for chapter in chapters:
            for key in chapter.keys:
                if key in "123":
                    animation = None if animation == key else key
                elif key == "x": flip_x = not flip_x
                elif key == "y": flip_y = not flip_y
                elif key == "f": filter_index = (filter_index + 1) % 13
                elif key == "r": render_mode = (render_mode + 1) % 3
                elif key == "z": zalgo = not zalgo
                elif key == "4": face = not face
            if chapter.name == "waveform": self.assertEqual(animation, "2")
            if chapter.name == "fft": self.assertEqual(animation, "3")
            if chapter.name == "matrix": self.assertEqual(animation, "1")
            if chapter.name == "zalgo": self.assertTrue(zalgo)
        self.assertEqual((animation, flip_x, flip_y, filter_index, render_mode, zalgo, face),
                         (None, False, False, 0, 0, False, True))


if __name__ == "__main__":
    unittest.main()
