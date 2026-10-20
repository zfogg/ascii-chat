"""Run with python -m unittest discover -s scripts -p test_generate_readme_demo_video.py."""

import argparse
import codecs
import gzip
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
    def test_zalgo_audit_checks_all_frames_including_corruption_after_midpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(work_dir=Path(directory), cols=8, rows=3)
            evidence = {"main": [{"name": "zalgo", "start": 0, "end": 1}]}
            good = "\x1b[?2026h\x1b[H" + "\r\n".join(c * 8 + "\u0334" for c in "ABC") + "\x1b[?2026l"
            def write(last):
                with gzip.open(args.work_dir / "main.cast.jsonl.gz", "wt", encoding="utf-8") as out:
                    for index, frame in enumerate([good, good, last]):
                        out.write(json.dumps([index / 3, "out", frame]) + "\n")
            write(good)
            self.assertEqual(demo.verify_zalgo_capture(args, evidence)["decorated_frames_checked"], 3)
            write(good.replace("A", "\ufffd", 1))
            with self.assertRaisesRegex(RuntimeError, "replacement characters"):
                demo.verify_zalgo_capture(args, evidence)

    def test_pipe_reads_preserve_utf8_characters_split_between_buffers(self):
        chunks = iter([b"M\xcc", b"\xb4\xcc", b"\x81\r\n"])
        class Pipe:
            def ReadFile(self, handle, buffer, size, count, overlapped):
                chunk = next(chunks, b"")
                buffer.raw = chunk + b"\0" * (size - len(chunk))
                count._obj.value = len(chunk)
                return bool(chunk)
        child = demo.WindowsTerminal.__new__(demo.WindowsTerminal)
        child.k, child.output = Pipe(), None
        child.decoder = codecs.getincrementaldecoder("utf-8")("strict")
        text = ""
        try:
            while True:
                text += child.read()
        except EOFError:
            pass
        self.assertEqual(text, "M\u0334\u0301\r\n")

    def test_zalgo_marks_never_move_or_replace_base_glyph_pixels(self):
        from PIL import ImageChops
        args = demo.parser().parse_args([])
        raster = demo.Rasterizer(args)
        for scale in (1, 4):
            for base in "MWNkol;,:0123":
                for marks in ("\u0334\u0301\u0325", "\u0335\u0303\u0310\u0324\u0329", "\u0338\u0330\u030a"):
                    self.assertIsNone(ImageChops.subtract(raster.mask(base, scale), raster.mask(base + marks, scale)).getbbox())

    def test_static_screen_does_not_lose_trailing_text(self):
        screen, replay = demo.replay_terminal(40, 3)
        replay.feed("Terminal too small\r\nResize to continue")
        self.assertEqual(screen.display[1].strip(), "Resize to continue")

    def test_zalgo_at_right_margin_does_not_wrap_or_skip_rows(self):
        import unicodedata
        screen, stream = demo.replay_terminal(8, 3)
        rows = ["A" * 8 + "\u0334\u0325\u0308", "B" * 8 + "\u0301\u0336", "C" * 8 + "\u0324"]
        stream.feed("\x1b[?2026h" + "\r\n".join(rows) + "\x1b[?2026l")
        plain = ["".join(c for c in unicodedata.normalize("NFD", row) if not unicodedata.combining(c)) for row in screen.display]
        self.assertEqual(plain, ["AAAAAAAA", "BBBBBBBB", "CCCCCCCC"])
        self.assertEqual((screen.cursor.x, screen.cursor.y), (8, 2))

    def test_native_rep_runs_preserve_full_rows_and_wrap(self):
        screen, stream = demo.replay_terminal(8, 3)
        stream.feed("\x1b[?2026hABCDEFGH\r\n12\x1b[6b\r\nZ\x1b[7b\x1b[?2026l")
        self.assertEqual(screen.display, ["ABCDEFGH", "12222222", "ZZZZZZZZ"])

    def test_rep_split_across_reads_and_current_rendition(self):
        screen, stream = demo.replay_terminal(8, 2)
        for text in ("\x1b[?2026h\x1b[31mX\x1b[32m\x1b[", "7", "b\x1b[?2026l"):
            stream.feed(text)
        self.assertEqual(screen.display[0], "XXXXXXXX")
        self.assertEqual(screen.buffer[0][0].fg, "red")
        self.assertEqual(screen.buffer[0][7].fg, "green")
        self.assertEqual(screen.cursor.x, 8)

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
                                 "--segment-duration-scale", "1.8", "--output-fps", "16.5", "--target-size-mb", "9.5", "--reuse-capture"], capture_output=True, text=True, check=True)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["terminal"], [50, 14])
        self.assertEqual(plan["ui_terminal"], [200, 56])
        self.assertEqual(plan["output_pixels"], [1600, 900])
        self.assertEqual(plan["fps"], 33)
        self.assertEqual(plan["output_fps"], 16.5)
        self.assertAlmostEqual(plan["test_patterns"][0]["seconds"], 10.8)
        self.assertAlmostEqual(plan["test_patterns"][1]["seconds"], 10.8)
        self.assertIsNone(plan["target_size_mb"])
        self.assertEqual(plan["media_start"], 28)
        self.assertTrue(plan["overlay"])
        self.assertFalse(plan["optimize"])
        baseline = 15 * 1.8 + sum(c.seconds for c in demo.chapters(1.8) + demo.tui_chapters(1.8))
        self.assertAlmostEqual(plan["seconds"], baseline / 1.1)

    def test_longer_tours_keep_opening_at_normal_speed_and_end_on_live_video(self):
        for scale, duration in [(1.8, 115.45), (2.0, 127.5), (2.2, 139.55)]:
            chapters = demo.chapters(scale)
            self.assertEqual(chapters[0].name, "intro")
            self.assertEqual(chapters[0].seconds, 7)
            self.assertEqual(chapters[0].label, "")
            self.assertNotIn("fast", [c.name for c in chapters])
            self.assertNotIn("unflip", [c.name for c in chapters])
            self.assertAlmostEqual(15 * scale + sum(c.seconds for c in chapters + demo.tui_chapters(scale)), duration)
            self.assertEqual(demo.tui_chapters(scale)[-1].name, "outro")
            self.assertEqual(demo.tui_chapters(scale)[-1].keys, "")

    def test_optimization_defaults_automatically_budget_for_free_github_uploads(self):
        for flags, optimized in [([], True), (["--optimize=false"], False)]:
            result = subprocess.run([sys.executable, str(SCRIPT), "--dry-run", *flags], capture_output=True, text=True, check=True)
            plan = json.loads(result.stdout)
            self.assertEqual(plan["optimize"], optimized)
            self.assertEqual(plan["target_size_mb"], 9.5 if optimized else None)
            self.assertEqual(plan["terminal"], [26, 10])
            self.assertEqual(plan["ui_terminal"], [104, 40])
            self.assertEqual(plan["art_cell_scale"], 4)

    def test_bad_dimensions_and_output_fail_before_side_effects(self):
        for flags in [["--width", "1279"], ["--height", "400"], ["--output", "out.gif"], ["--output-fps", "34"]]:
            result = subprocess.run([sys.executable, str(SCRIPT), "--dry-run", *flags], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)

    def test_exact_effect_durations_and_restored_state(self):
        chapters = demo.chapters()
        self.assertEqual(sum(c.seconds for c in chapters if c.name.startswith("filter-")), 9)
        self.assertFalse({"filter-black", "filter-white", "filter-rainbow"} & {c.name for c in chapters})
        self.assertFalse(any("Default rendering" in c.label for c in chapters))
        self.assertEqual(sum(c.seconds for c in chapters if c.name in ("flip-x", "flip-xy", "flip-y")), 2.25)
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
            if chapter.name.startswith("filter-"):
                names = ("none", "black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow")
                self.assertEqual(chapter.name, "filter-" + names[filter_index])
                self.assertFalse(flip_x or flip_y)
            if chapter.name == "fft": self.assertEqual(animation, "3")
            if chapter.name == "matrix": self.assertEqual(animation, "1")
            if chapter.name == "zalgo": self.assertTrue(zalgo)
        self.assertEqual((animation, flip_x, flip_y, filter_index, render_mode, zalgo, face),
                         (None, False, False, 0, 0, False, True))


if __name__ == "__main__":
    unittest.main()
