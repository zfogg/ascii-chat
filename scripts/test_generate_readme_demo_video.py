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
    def test_shortcut_overlays_have_prefix_and_fit_the_readme_canvas(self):
        args = demo.parser().parse_args([])
        raster = demo.Rasterizer(args)
        chapters = demo.tui_chapters()
        for chapter in chapters[:5]:
            self.assertTrue(chapter.label.startswith('Keyboard shortcut: "'), chapter.label)
            self.assertLess(raster.label_font.getlength(chapter.label) + 36, args.width)
        for chapter in chapters[5:]:
            self.assertFalse(chapter.label.startswith('Keyboard shortcut: '))

    def test_unchanged_chapters_reuse_video_without_running_ffmpeg(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with gzip.open(root / 'main.cast.jsonl.gz', 'wt', encoding='utf-8'):
                pass
            movie = root / 'intro.mp4'
            movie.write_bytes(b'cached movie')
            args = SimpleNamespace(work_dir=root, cols=104, rows=40)
            with patch.object(demo.subprocess, 'Popen', side_effect=AssertionError('Cached chapter re-encoded')):
                self.assertEqual(demo.render_capture(args, 'main', [{'name': 'intro', 'seconds': 7}], None, {'intro'}), [movie])

    def test_wider_tui_fits_entire_grid_without_cropping_its_corners(self):
        from PIL import ImageChops, Image
        args = demo.parser().parse_args([])
        raster = demo.Rasterizer(args)
        screen, stream = demo.replay_terminal(110, 40)
        stream.feed("\x1b[1;1HM\x1b[1;110HM\x1b[40;1HM\x1b[40;110HM")
        image = raster.draw(screen, "", fit_grid=True)
        self.assertEqual(image.size, (838, 640))
        for x, y in ((0, 0), (808, 0), (0, 600), (808, 600)):
            corner = image.crop((x, y, x + 30, y + 40))
            self.assertIsNotNone(ImageChops.difference(corner, Image.new("RGB", corner.size, "#0c0c0c")).getbbox())

    def test_media_fit_preserves_all_source_corners_with_padding(self):
        from PIL import Image, ImageDraw
        with tempfile.TemporaryDirectory() as directory:
            source, fitted = Path(directory) / "source.png", Path(directory) / "fitted.png"
            image = Image.new("RGB", (320, 180), "#333333")
            draw = ImageDraw.Draw(image)
            colors = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0))
            for (x, y), color in zip(((0, 0), (300, 0), (0, 160), (300, 160)), colors):
                draw.rectangle((x, y, x + 19, y + 19), fill=color)
            image.save(source)
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(source), "-vf", demo.media_fit_filter(104, 40), str(fitted)], check=True)
            image = Image.open(fitted).convert("RGB")
            self.assertEqual(image.size, (832, 640))
            self.assertEqual(image.getpixel((400, 0)), (0, 0, 0))
            for point, color in zip(((10, 96), (822, 96), (10, 544), (822, 544)), colors):
                self.assertEqual(image.getpixel(point), color)

    def test_full_grid_corners_fit_and_oversized_grids_are_rejected(self):
        from PIL import ImageChops, Image
        args = demo.parser().parse_args([])
        raster = demo.Rasterizer(args)
        screen, stream = demo.replay_terminal(104, 40)
        stream.feed("\x1b[1;1HM\x1b[1;104HM\x1b[40;1HM\x1b[40;104HM")
        image = raster.draw(screen, "")
        for x, y in ((3, 0), (827, 0), (3, 624), (827, 624)):
            corner = image.crop((x, y, x + 8, y + 16))
            self.assertIsNotNone(ImageChops.difference(corner, Image.new("RGB", corner.size, "#0c0c0c")).getbbox())
        screen.resize(lines=41, columns=104)
        with self.assertRaisesRegex(ValueError, "refusing to crop"):
            raster.draw(screen, "")

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
        self.assertEqual(plan["terminal"], [200, 56])
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
        baseline = 14 + 15 * 1.8 + sum(c.seconds for c in demo.chapters(1.8) + demo.tui_chapters(1.8))
        self.assertAlmostEqual(plan["seconds"], baseline / 1.1)
        self.assertEqual([c['seconds'] for c in plan['call']], [1.5, 1.5, 1.5, 9.5])

    def test_call_audio_inserts_silence_and_resumes_music_at_the_same_sample(self):
        import array
        import math
        import shutil
        if not shutil.which('ffmpeg'):
            self.skipTest('FFmpeg unavailable')
        args = SimpleNamespace(segment_duration_scale=0, speed=1)
        # Input 1 is the soundtrack; use an independent graph to exercise the
        # exact production edit and verify the encoded sample boundary.
        graph = demo.soundtrack_filter(args, 3, 1, 1).replace('[1:a]', '[0:a]')
        raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                                      'sine=frequency=440:sample_rate=44100:duration=2',
                                      '-filter_complex', graph, '-map', '[a]', '-t', '3',
                                      '-f', 'f32le', '-ac', '2', '-'])
        samples = array.array('f', raw)
        def rms(start, end):
            portion = samples[int(start * 88200):int(end * 88200)]
            return math.sqrt(sum(x*x for x in portion) / len(portion))
        self.assertGreater(rms(.2, .7), .01)
        self.assertEqual(rms(1.1, 1.9), 0)
        self.assertGreater(rms(2.1, 2.4), .001)

    def test_call_uses_four_different_sources_and_fourteen_second_join_sequence(self):
        self.assertEqual(len(set(demo.CALL_SOURCES)), 4)
        self.assertEqual(sum(c.seconds for c in demo.call_chapters()), 14)
        self.assertEqual([c.name for c in demo.call_chapters()], ['call-1', 'call-2', 'call-3', 'call-4'])

    def test_call_keeps_four_muted_native_clients_connected_to_one_server(self):
        terminals = []
        class FakeTerminal:
            def __init__(self, command, cols, rows, capture, env):
                self.command, self.closed = command, False
                self.start = demo.time.monotonic()
                self.markers = []
                terminals.append(self)
            def wait_log(self, path, marker):
                self.markers.append(marker)
            def wait(self, seconds):
                pass
            def alive(self):
                return not self.closed
            def close(self):
                self.closed = True
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(binary=Path('ascii-chat'), work_dir=Path(directory), cols=104, rows=40, art_cell_scale=1)
            with patch.object(demo, 'Terminal', FakeTerminal):
                evidence = demo.record_call(args, [Path(f'person-{i}.mp4') for i in range(4)])
            self.assertEqual(len(terminals), 5)
            self.assertIn('server', terminals[0].command)
            self.assertEqual(terminals[0].markers[1:], [f'now {i} clients' for i in range(1, 5)])
            for client in terminals[1:]:
                self.assertIn('client', client.command)
                self.assertIn('--audio=false', client.command)
                self.assertNotIn('--render-mode', client.command)
                self.assertNotIn('--color-mode', client.command)
                self.assertNotIn('--color-filter', client.command)
                self.assertNotIn('--utf8', client.command)
                self.assertEqual(client.command[client.command.index('--volume') + 1], '0')
                self.assertEqual(client.command[client.command.index('--microphone-volume') + 1], '0')
                self.assertIn('Connected', client.markers)
            self.assertTrue(all(t.closed for t in terminals))
            self.assertTrue(evidence['muted'])
            self.assertEqual(evidence['participants'], 4)

    def test_call_media_requires_exactly_four_existing_files(self):
        result = subprocess.run([sys.executable, str(SCRIPT), '--dry-run', '--call-media-file', 'unused'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('exactly four', result.stderr)

    def test_longer_tours_keep_opening_at_normal_speed_and_end_on_live_video(self):
        for scale, duration in [(1.8, 110.05), (2.0, 121.5), (2.2, 132.95)]:
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
            self.assertEqual(plan["terminal"], [104, 40])
            self.assertEqual(plan["ui_terminal"], [110, 40])
            self.assertEqual(plan["art_cell_scale"], 1)

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
        self.assertEqual(chapters[-1].name, "zalgo")
        self.assertNotIn("face-follow", [c.name for c in chapters])
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
                elif key == "w": face = not face
            if chapter.name == "waveform": self.assertEqual(animation, "2")
            if chapter.name.startswith("filter-"):
                names = ("none", "black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow")
                self.assertEqual(chapter.name, "filter-" + names[filter_index])
                self.assertFalse(flip_x or flip_y)
            if chapter.name == "fft": self.assertEqual(animation, "3")
            if chapter.name == "matrix": self.assertEqual(animation, "1")
            if chapter.name == "zalgo": self.assertTrue(zalgo)
        self.assertTrue(zalgo)
        zalgo = not zalgo  # The recording closes the effect with z.
        self.assertEqual((animation, flip_x, flip_y, filter_index, render_mode, zalgo, face),
                         (None, False, False, 0, 0, False, False))

    def test_overlay_stays_above_hovered_video_player_controls(self):
        from PIL import ImageChops, Image
        for width, height in ((688, 448), (838, 640), (1600, 900)):
            args = demo.parser().parse_args(['--opt-text-overlay', '--width', str(width), '--height', str(height)])
            screen, _ = demo.replay_terminal(width // 8, height // 16)
            image = demo.Rasterizer(args).draw(screen, 'Keyboard shortcut: "=" / Live statistics')
            box = ImageChops.difference(image, Image.new('RGB', image.size, '#0c0c0c')).getbbox()
            self.assertGreaterEqual((height - box[3]) * min(1, 838 / width), 80)
            self.assertGreater(box[1], height // 2)

    def test_legacy_face_is_omitted_and_the_outro_uses_clean_intro_footage(self):
        evidence = {'main': [
            {'name': 'intro', 'start': 2, 'end': 9, 'seconds': 7},
            {'name': 'face-follow', 'start': 40, 'end': 45, 'seconds': 5},
            {'name': 'outro', 'start': 45, 'end': 48.6, 'seconds': 3.6}]}
        demo.omit_legacy_face(evidence)
        self.assertEqual([c['name'] for c in evidence['main']], ['intro', 'outro'])
        self.assertEqual(evidence['outro_capture'], 'intro')
        self.assertEqual(evidence['main'][-1]['end'], 9)
        self.assertAlmostEqual(evidence['main'][-1]['start'], 5.4)

    def test_legacy_capture_migration_keeps_binary_and_grid_validation(self):
        current = {'recipe': 10, 'binary_sha256': 'abc', 'cols': 104, 'chapters': [{'name': 'intro'}]}
        saved = {**current, 'recipe': 9, 'face': 'legacy', 'face_sha256': 'old',
                 'chapters': [{'name': 'intro', 'label': ''}, {'name': 'face-follow'}]}
        self.assertTrue(demo.compatible_fingerprint(saved, current))
        self.assertFalse(demo.compatible_fingerprint(saved, {**current, 'cols': 80}))
        self.assertFalse(demo.compatible_fingerprint(saved, {**current, 'binary_sha256': 'different'}))

    def test_fresh_capture_ends_zalgo_and_keeps_ok_go_for_the_outro_and_tuis(self):
        terminals = []
        class FakeTerminal:
            def __init__(self, command, cols, rows, capture):
                self.command, self.sent = command, []
                self.start = demo.time.monotonic()
                terminals.append(self)
            def wait_log(self, path, marker):
                return 0
            def wait(self, seconds):
                pass
            def send(self, keys):
                self.sent.append(keys)
            def resize(self, cols, rows):
                pass
            def close(self):
                pass
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(binary=Path('ascii-chat'), work_dir=Path(directory), cols=104, rows=40,
                                   ui_cols=110, ui_rows=40, art_cell_scale=1, segment_duration_scale=1.8)
            with patch.object(demo, 'Terminal', FakeTerminal):
                evidence = demo.record(args, demo.chapters(1.8))
            self.assertEqual(terminals[0].sent[-1], 'z')
            self.assertNotIn('w', terminals[0].sent)
            self.assertFalse(any('face-' in value for t in terminals for value in t.command))
            self.assertTrue(any(value.endswith('performance.mkv') for value in terminals[1].command))
            self.assertNotIn('face-follow', [c['name'] for c in evidence['main']])


if __name__ == "__main__":
    unittest.main()
