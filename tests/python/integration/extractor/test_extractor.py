import pytest
import psutil
"""Exercise yt-dlp argument handling and separate stream playback end to end."""
from ascii_chat_tests.media import ppm_image, write_pcm_wav
from ascii_chat_tests.cli import clean_environment
from ascii_chat_tests.cli import cli_command
from ascii_chat_tests.terminal import CaptureScreen, CaptureStream, Terminal, TerminalProcess

import functools
import http.server
import os
import re
import queue
import random
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.parse



pytestmark = [pytest.mark.integration, pytest.mark.media]


class MediaExtractor(unittest.TestCase):
    """Exercise extractor behavior with local media and a fake yt-dlp."""
    @pytest.fixture(autouse=True)
    def setup_probes(self, binary, yt_dlp_probe, extractor_audio_probe):
        self.binary = str(binary)
        self.fixture = str(yt_dlp_probe)
        self.audio_probe = str(extractor_audio_probe)

    def setUp(self):
        """Create isolated media, extractor, and HTTP fixtures."""
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        shutil.copy2(self.fixture, self.tools / ("yt-dlp.exe" if os.name == "nt" else "yt-dlp"))
        image = self.root / "video.ppm"
        image.write_bytes(ppm_image(64, 32, bytes([50, 140, 220]) * (64 * 32)))
        write_pcm_wav(self.root / "audio.wav", b"\x00\x10" * 48000, sample_rate=48000)
        self.requests = []
        self.request_headers = []
        self.require_auth = False
        requests = self.requests
        request_headers = self.request_headers
        fixture = self
        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                """Record each media request before serving it."""
                requests.append(self.path)
                request_headers.append(dict(self.headers))
                if self.path.startswith("http://"):
                    self.path = urllib.parse.urlsplit(self.path).path
                if fixture.require_auth and (self.headers.get("X-Test") != "fixture" or
                                             "session=fixture" not in self.headers.get("Cookie", "")):
                    self.send_error(403)
                    return
                super().do_GET()
            def log_message(self, format, *args):
                """Keep the test output quiet."""
                pass
        handler = functools.partial(Handler, directory=str(self.root))
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"
        self.trace = self.root / "trace.txt"
        self.env = clean_environment()
        self.env.update(PATH=str(self.tools) + os.pathsep + os.environ.get("PATH", ""),
                        ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none", ASCII_CHAT_DISABLE_FIRST_USE="1",
                        FAKE_YTDLP_TRACE=str(self.trace), FAKE_YTDLP_VIDEO=self.base + "/video.ppm",
                        FAKE_YTDLP_AUDIO=self.base + "/audio.wav",
                        FAKE_YTDLP_COOKIE="session=fixture; Domain=127.0.0.1; Path=/")

    def tearDown(self):
        """Stop the HTTP fixture and remove its temporary files."""
        self.server.shutdown()
        self.thread.join(timeout=5)
        self.server.server_close()
        self.tmp.cleanup()

    def run_mirror(self, mode, extra_options=""):
        """Run a snapshot and inspect the extractor arguments."""
        source = self.base + "/watch?first=1&second=2"
        env = dict(self.env, FAKE_YTDLP_MODE=mode)
        cmd = cli_command(self.binary, "--log-file", str(self.root / "log.txt"),
               "mirror", source, "--snapshot", "--snapshot-delay", "0", "--splash-screen=false",
               "--audio=false", "--width", "40", "--height", "12",
               "--yt-dlp-options=--add-header 'X-Test: \"quoted\" value' --paths 'C:\\some path\\' " + extra_options)
        result = subprocess.run(cmd, env=env, cwd=self.root, capture_output=True, timeout=45)
        calls = self.trace.read_text().split("--END--\n") if self.trace.exists() else []
        calls = [c.splitlines() for c in calls if c.strip()]
        self.assertEqual(len(calls), 1, f"calls={calls}, exit={result.returncode}, "
                         f"stdout={result.stdout[-4000:]!r}, stderr={result.stderr[-4000:]!r}, "
                         f"log={(self.root / 'log.txt').read_text(errors='replace')[-4000:] if (self.root / 'log.txt').exists() else 'missing'}")
        args = calls[0]
        self.assertIn(source, args)
        self.assertIn('X-Test: "quoted" value', args)
        self.assertIn("C:\\some path\\", args)
        self.assertIn("b/bv[vcodec^=avc1]+ba/bv+ba", args)
        if extra_options:
            tokens = shlex.split(extra_options)
            index = args.index(tokens[0], args.index("b/bv[vcodec^=avc1]+ba/bv+ba") + 1)
            self.assertEqual(args[index:index + len(tokens)], tokens)
        return result

    @pytest.mark.terminal
    def test_youtube_startup_transitions_directly_to_media(self):
        """Exercise the reported URL using deterministic local extractor output."""
        url = "https://www.youtube.com/watch?v=M1b0Ka2gAak"
        env = dict(self.env, FAKE_YTDLP_MODE="combined")
        term = Terminal(cli_command(self.binary, "mirror", url, "--audio=false", "--palette", "custom",
                                    "--palette-chars", "QQ", "--color-mode", "none"),
                        rows=24, cols=100, terminal_replies=True, environment=env)
        try:
            term.expect(lambda text: "Using network media source" in text, "Startup screen did not appear", timeout=30)
            term.expect(lambda text: "QQQQQQQQ" in text, "Media frame did not appear", timeout=45)
            raw = "".join(term.raw)
            frames = list(re.finditer(r"\x1b\[\?2026h(.*?)\x1b\[\?2026l", raw, re.S))
            startup = [i for i, frame in enumerate(frames) if "Using network media source" in frame[1]]
            self.assertTrue(startup, "Startup screen was not published as a terminal frame")
            following = frames[startup[-1] + 1:]
            self.assertTrue(following, "No frame followed the startup screen")
            first_frame = re.sub(r"(.)\x1b\[(\d+)b", lambda match: match[1] * (int(match[2]) + 1), following[0][1])
            self.assertIn("QQQQQQQQ", first_frame, "The next frame after startup was not the media frame")
            between = raw[frames[startup[-1]].end():following[0].start()]
            between = re.sub(r"\x1b\](?:[^\x07\x1b]|\x1b(?!\\))*(?:\x07|\x1b\\)", "", between)
            between = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", between)
            self.assertFalse(between.strip(), f"Unexpected output between startup and media: {between!r}")
            self.assertIn(url, self.trace.read_text())
        finally:
            term.close()

    @pytest.mark.terminal
    @unittest.skipUnless(os.environ.get("ASCII_CHAT_TEST_LONG_TERMINAL") == "1",
                         "Set ASCII_CHAT_TEST_LONG_TERMINAL=1 for the 15-second large-terminal regression")
    def test_youtube_large_terminal_remains_stable_for_15_seconds(self):
        """Watch 400x200 startup and detailed media frames for missing rows.

        Run on demand with ASCII_CHAT_TEST_LONG_TERMINAL=1 and the python-extractor
        CTest target, or pass the built binary and extractor fixtures to pytest.
        """
        ffmpeg = shutil.which("ffmpeg")
        if not ffmpeg:
            self.skipTest("ffmpeg is required to generate the long-running fixture")
        rng = random.Random(42)
        pixels = bytes(rng.randrange(90) if y < 180 else 255
                       for y in range(200) for _x in range(400) for _channel in range(3))
        image = self.root / "video.ppm"
        image.write_bytes(ppm_image(400, 200, pixels))
        video = self.root / "stable.avi"
        subprocess.run([ffmpeg, "-v", "error", "-loop", "1", "-i", str(image), "-t", "30", "-r", "5",
                        "-c:v", "ffv1", str(video)], check=True, capture_output=True, timeout=60)
        env = dict(self.env, FAKE_YTDLP_MODE="combined", FAKE_YTDLP_VIDEO=self.base + "/stable.avi")
        term = Terminal(cli_command(self.binary, "mirror", "https://www.youtube.com/watch?v=M1b0Ka2gAak",
                                    "--audio=false", "--width", "400", "--height", "200", "--stretch",
                                    "--fps", "5", "--palette", "custom", "--palette-chars", "AZ",
                                    "--color-mode", "truecolor"),
                        rows=200, cols=400, terminal_replies=False, environment=env)
        pending = ""
        reply_tail = ""
        start = "\x1b[?2026h"
        end = "\x1b[?2026l"
        query = "\x1b]11;?\x1b\\"
        startup_seen = False
        media_seen = False
        media_frames = 0
        last_media_at = 0.0
        first_media = None
        last_media = None
        incomplete = []
        startup_flashes = []
        deadline = time.monotonic() + 60
        try:
            while time.monotonic() < deadline and term.process.isalive():
                try:
                    chunk = term.output.get(timeout=.1)
                except queue.Empty:
                    continue
                replies = reply_tail + chunk
                for _ in range(replies.count(query)):
                    term.write("\x1b]11;rgb:0000/0000/0000\x1b\\")
                reply_tail = replies[-(len(query) - 1):]
                pending += chunk
                while start in pending and end in pending[pending.index(start) + len(start):]:
                    start_at = pending.index(start)
                    if startup_seen:
                        outside = pending[:start_at]
                        outside = re.sub(r"\x1b\](?:[^\x07\x1b]|\x1b(?!\\))*(?:\x07|\x1b\\)", "", outside)
                        outside = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]|\x1b[78]", "", outside).strip()
                        if outside:
                            startup_flashes.append(f"output outside a frame: {outside[:100]!r}")
                    begin = start_at + len(start)
                    finish = pending.index(end, begin)
                    frame = pending[begin:finish]
                    pending = pending[finish + len(end):]
                    if "Using network media source" in frame or "Video chat in your terminal" in frame:
                        if media_seen:
                            startup_flashes.append("startup screen returned after video")
                        startup_seen = True
                    elif len(frame) > 500000:
                        if not media_seen:
                            media_seen = True
                            deadline = time.monotonic() + 15
                            first_media = frame
                        last_media = frame
                        media_frames += 1
                        last_media_at = time.monotonic()
                        if not re.search(r"Z(?:\x1b\[\d+b|Z{3})", frame):
                            incomplete.append((media_frames, len(frame), repr(frame[-30:])))
                        if re.search(r"\x1b(?:\[[0-?]*[ -/]*)?$", frame):
                            incomplete.append((media_frames, len(frame), "unfinished escape sequence"))
                    elif startup_seen and not media_seen:
                        visible = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", frame).strip()
                        startup_flashes.append(f"unexpected frame after startup: {visible[:100]!r}")
                    elif media_seen:
                        startup_flashes.append("non-media frame during video")
                if len(pending) > 8 * 1024 * 1024:
                    self.fail("Terminal output did not finish a frame")
            self.assertTrue(term.process.isalive(), "Media exited during the 15-second observation")
            self.assertTrue(startup_seen and media_seen, "Did not observe both startup and large media frames")
            self.assertGreater(media_frames, 1, "Did not observe multiple complete media frames")
            self.assertGreaterEqual(last_media_at, deadline - 2, "Media stopped rendering during the observation")
            self.assertFalse(startup_flashes, f"Startup screen flickered: {startup_flashes[:5]}")
            self.assertFalse(incomplete, f"Media frames lost their bottom rows: {incomplete[:5]}")
            assert first_media is not None and last_media is not None

            class ScrollScreen(CaptureScreen):
                def __init__(self):
                    self.watch_scroll = False
                    self.scrolls = 0
                    super().__init__(400, 200)

                def index(self):
                    if self.watch_scroll and self.cursor.y == self.lines - 1:
                        self.scrolls += 1
                    super().index()

            screen = ScrollScreen()
            stream = CaptureStream(screen)
            stream.feed(start + first_media + end)
            reference = tuple(screen.display)
            screen.watch_scroll = True
            stream.feed(start + last_media + end)
            self.assertEqual(screen.scrolls, 0, "Media output scrolled the terminal")
            self.assertEqual(tuple(screen.display), reference, "Static video moved between complete frames")
        finally:
            term.close()

    @pytest.mark.terminal
    def test_split_stream_startup_does_not_print_sanitizer_diagnostics(self):
        """Split-format metadata must not write diagnostics over the splash."""
        url = "https://www.youtube.com/watch?v=M1b0Ka2gAak"
        for mode in ("split", "split-missing"):
            with self.subTest(mode=mode):
                env = dict(self.env, FAKE_YTDLP_MODE=mode)
                term = Terminal(cli_command(self.binary, "mirror", url, "--audio=false", "--palette", "custom",
                                            "--palette-chars", "QQ", "--color-mode", "none"),
                                rows=24, cols=100, terminal_replies=True, environment=env)
                try:
                    term.expect(lambda text: "Using network media source" in text,
                                "Startup screen did not appear", timeout=30)
                    term.expect(lambda text: "QQQQQQQQ" in text, "Media frame did not appear", timeout=45)
                    output = "".join(term.raw)
                    if "runtime error:" in output:
                        index = output.index("runtime error:")
                        self.fail(f"Sanitizer output interrupted the startup screen: {output[index:index + 500]!r}")
                    self.assertIn(url, self.trace.read_text())
                finally:
                    term.close()

    def test_combined_stream_is_used_for_both_decoders(self):
        """Use the combined URL without requesting separate audio."""
        result = self.run_mirror("combined")
        self.assertEqual(result.returncode, 0, (self.request_headers, result.stderr.decode(errors="replace")[-4000:]))
        self.assertIn("/video.ppm", self.requests)
        self.assertNotIn("/audio.wav", self.requests)
        self.assertGreater(len(result.stdout), 20, repr(result.stdout))

    def test_separate_audio_and_video_streams_are_opened(self):
        """Open both URLs when the extractor returns separate streams."""
        result = self.run_mirror("split")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
        self.assertIn("/video.ppm", self.requests)
        self.assertIn("/audio.wav", self.requests)
        self.assertGreater(len(result.stdout), 20, repr(result.stdout))

    def test_user_format_overrides_default(self):
        """Pass each supported explicit format form after the default selector."""
        for options in ("--format worst", "--format=worst", "-f worst", "-fworst"):
            with self.subTest(options=options):
                self.trace.unlink(missing_ok=True)
                result = self.run_mirror("combined", options)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_audio_first_selector_plays_video_and_audio(self):
        """Support selectors that return the audio URL before the video URL."""
        result = self.run_mirror("reversed", "--format ba+bv")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
        self.assertIn("/video.ppm", self.requests)
        self.assertIn("/audio.wav", self.requests)
        self.assertGreater(len(result.stdout), 20, repr(result.stdout))

    def test_stream_requests_include_extracted_headers_and_cookies(self):
        """Both decoders authenticate their media requests using extractor metadata."""
        self.require_auth = True
        result = self.run_mirror("split")
        self.assertEqual(result.returncode, 0, (self.request_headers, result.stderr.decode(errors="replace")[-4000:]))
        self.assertIn("/video.ppm", self.requests)
        self.assertIn("/audio.wav", self.requests)
        self.assertTrue(all(h.get("X-Test") == "fixture" and "session=fixture" in h.get("Cookie", "")
                            for h in self.request_headers))

    def test_explicit_http_proxy_is_used_for_both_streams(self):
        """Forward both supported --proxy forms to the media decoders."""
        self.env.update(FAKE_YTDLP_VIDEO="http://unresolvable.invalid/video.ppm",
                        FAKE_YTDLP_AUDIO="http://unresolvable.invalid/audio.wav", FAKE_YTDLP_COOKIE="")
        for option in (f"--proxy {self.base}", f"--proxy={self.base}"):
            with self.subTest(option=option):
                self.trace.unlink(missing_ok=True)
                self.requests.clear()
                result = self.run_mirror("split", option)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
                self.assertTrue(any(path.endswith("/video.ppm") for path in self.requests), self.requests)
                self.assertTrue(any(path.endswith("/audio.wav") for path in self.requests), self.requests)

    def test_empty_proxy_overrides_environment(self):
        self.env.update(http_proxy="http://127.0.0.1:1", no_proxy="")
        result = self.run_mirror("combined", "--proxy ''")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
        self.assertIn("/video.ppm", self.requests)

    def test_extractor_stderr_does_not_corrupt_metadata(self):
        self.env["FAKE_YTDLP_DIAGNOSTIC"] = "1"
        result = self.run_mirror("combined", "--verbose")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    @unittest.skipUnless(os.name == "nt", "Windows pipe inheritance")
    def test_extractor_drains_output_after_parent_exits(self):
        env = dict(self.env, FAKE_YTDLP_MODE="delayed")
        result = subprocess.run([self.audio_probe, self.base + "/watch", self.base + "/video.ppm"],
                                env=env, cwd=self.root, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_extracted_rtmp_stream_is_returned(self):
        """Accept RTMP streams supplied by the extractor."""
        stream = "rtmp://stream.example.test/live/example"
        env = dict(self.env, FAKE_YTDLP_MODE="combined", FAKE_YTDLP_VIDEO=stream)
        result = subprocess.run([self.audio_probe, self.base + "/watch", stream],
                                env=env, cwd=self.root, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_invalid_or_local_stream_is_rejected(self):
        """Reject malformed network URLs and local file URLs."""
        for stream in ("rtmp:///missing-host", "file:///private-file"):
            with self.subTest(stream=stream):
                env = dict(self.env, FAKE_YTDLP_MODE="combined", FAKE_YTDLP_VIDEO=stream)
                result = subprocess.run([self.audio_probe, self.base + "/watch", "--expect-error"],
                                        env=env, cwd=self.root, capture_output=True, timeout=45)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_unterminated_cookie_is_rejected(self):
        env = dict(self.env, FAKE_YTDLP_MODE="combined", FAKE_YTDLP_COOKIE='session="unterminated')
        result = subprocess.run([self.audio_probe, self.base + "/watch", "--expect-error"],
                                env=env, cwd=self.root, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_unsupported_proxy_does_not_open_media(self):
        cmd = [self.binary, "--no-check-update", "mirror", self.base + "/watch", "--snapshot",
               "--splash-screen=false", "--audio=false", "--yt-dlp-options=--proxy socks5://127.0.0.1:1080"]
        result = subprocess.run(cmd, env=self.env, cwd=self.root, capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("/video.ppm", self.requests)
        self.assertNotIn("/audio.wav", self.requests)

    def test_http_proxy_is_retained_for_fallback(self):
        self.env["no_proxy"] = ""
        result = self.run_mirror("error", f"--proxy {self.base}")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(any(path.startswith("http://") and "/watch" in path for path in self.requests), self.requests)

    def test_http_proxy_is_retained_when_extractor_is_unavailable(self):
        self.env.update(no_proxy="", FAKE_YTDLP_VERSION_FAIL="1")
        cmd = [self.binary, "--no-check-update", "mirror", self.base + "/watch", "--snapshot",
               "--splash-screen=false", "--audio=false", f"--yt-dlp-options=--proxy {self.base}"]
        result = subprocess.run(cmd, env=self.env, cwd=self.root, capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(any(path.startswith("http://") and "/watch" in path for path in self.requests), self.requests)

    def test_http_proxy_rejects_non_http_extracted_stream(self):
        self.env.update(FAKE_YTDLP_VIDEO="rtmp://127.0.0.1/live/test", FAKE_YTDLP_COOKIE="")
        cmd = [self.binary, "--no-check-update", "mirror", self.base + "/watch", "--snapshot",
               "--splash-screen=false", "--audio=false", f"--yt-dlp-options=--proxy {self.base}"]
        result = subprocess.run(cmd, env=self.env, cwd=self.root, capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.requests, [])

    def test_unrelated_extraction_failure_does_not_retry(self):
        """Do not retry extraction after an unrelated failure."""
        result = self.run_mirror("error")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("/audio.wav", self.requests)
        self.assertNotIn("/video.ppm", self.requests)

    def test_cancel_terminates_stalled_extractor(self):
        """Cancellation terminates and reaps an unresponsive extractor."""
        env = dict(self.env, FAKE_YTDLP_MODE="hang")
        result = subprocess.run([self.audio_probe, self.base + "/watch", "--expect-cancel"],
                                env=env, cwd=self.root, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    @unittest.skipIf(os.name == "nt", "POSIX terminal job control")
    def test_cancellable_extractor_can_read_foreground_terminal(self):
        import termios

        extractor_pid = self.root / "extractor.pid"
        env = dict(self.env, FAKE_YTDLP_MODE="interactive", FAKE_YTDLP_EXPECT_TERMINAL="1",
                   FAKE_YTDLP_EXTRACTOR_PID=str(extractor_pid))
        with TerminalProcess([self.audio_probe, self.base + "/watch", self.base + "/video.ppm"],
                             environment=env, cwd=self.root, tostop=True, child_group_file=extractor_pid) as terminal:
            terminal.reply_when_prompted(b"fixture prompt:", b"yes\n")
            code, stdout, stderr = terminal.wait()
            self.assertEqual(code, 0, (stdout, stderr[-4000:]))
            self.assertTrue(termios.tcgetattr(terminal.master)[3] & termios.TOSTOP)

    @unittest.skipUnless(os.name == "nt", "Windows console")
    def test_interactive_extractor_prompt_is_visible_on_windows(self):
        env = dict(self.env, FAKE_YTDLP_MODE="interactive", FAKE_YTDLP_EXPECT_TERMINAL="1")
        terminal = Terminal([self.audio_probe, self.base + "/watch", self.base + "/video.ppm"],
                            rows=24, cols=100, environment=env)
        try:
            terminal.expect(lambda text: "fixture prompt:" in text, "Extractor prompt was not visible", timeout=20)
            terminal.write("yes\r")
            terminal.expect(lambda _text: not terminal.process.isalive(), "Extractor did not exit", timeout=20)
            self.assertEqual(terminal.process.exitstatus, 0, "".join(terminal.raw)[-4000:])
        finally:
            terminal.close()

    @unittest.skipIf(os.name == "nt", "POSIX terminal job control")
    def test_cancelling_interactive_extractor_kills_descendants_and_restores_terminal(self):
        import time

        pid_file = self.root / "descendant.pid"
        extractor_pid = self.root / "extractor.pid"
        env = dict(self.env, FAKE_YTDLP_MODE="interactive-hang", FAKE_YTDLP_EXPECT_TERMINAL="1",
                   FAKE_YTDLP_CHILD_PID=str(pid_file), FAKE_YTDLP_EXTRACTOR_PID=str(extractor_pid))
        with TerminalProcess([self.audio_probe, self.base + "/watch", "--expect-cancel"],
                             environment=env, cwd=self.root, child_group_file=extractor_pid) as terminal:
            terminal.reply_when_prompted(b"fixture prompt:", b"yes\n")
            code, stdout, stderr = terminal.wait()
            self.assertEqual(code, 0, (stdout, stderr[-4000:]))
            self.assertTrue(pid_file.exists(), "extractor did not start its descendant")
            child = int(pid_file.read_text())
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try:
                    if psutil.Process(child).status() == psutil.STATUS_ZOMBIE:
                        break
                    os.kill(child, 0)
                except (ProcessLookupError, psutil.NoSuchProcess):
                    break
                time.sleep(0.05)
            else:
                self.fail(f"extractor descendant {child} survived cancellation")

    @unittest.skipIf(os.name == "nt", "POSIX terminal job control")
    def test_stopped_extractor_fails_without_leaving_terminal_in_background(self):
        extractor_pid = self.root / "extractor.pid"
        env = dict(self.env, FAKE_YTDLP_MODE="interactive-stop", FAKE_YTDLP_EXPECT_TERMINAL="1",
                   FAKE_YTDLP_EXTRACTOR_PID=str(extractor_pid))
        with TerminalProcess([self.audio_probe, self.base + "/watch", "--expect-error"],
                             environment=env, cwd=self.root, child_group_file=extractor_pid) as terminal:
            code, stdout, stderr = terminal.wait(timeout=10)
            self.assertEqual(code, 0, (stdout, stderr[-4000:]))

    @unittest.skipIf(os.name == "nt", "POSIX terminal job control")
    def test_background_color_query_does_not_reach_foreground_extractor(self):
        import time

        extractor_pid = self.root / "extractor.pid"
        marker = self.root / "query-attempted"
        env = dict(self.env, FAKE_YTDLP_MODE="interactive", FAKE_YTDLP_EXPECT_TERMINAL="1",
                   FAKE_YTDLP_EXTRACTOR_PID=str(extractor_pid), FAKE_YTDLP_QUERY_ATTEMPTED=str(marker),
                   TERM_BACKGROUND="")
        terminal = Terminal([self.audio_probe, self.base + "/watch", self.base + "/video.ppm"],
                            rows=24, cols=100, terminal_replies=True, environment=env)
        try:
            terminal.expect(lambda text: "fixture prompt:" in text, "Extractor prompt was not visible", timeout=20)
            previous_output = len("".join(terminal.raw))
            deadline = time.monotonic() + 10
            while not marker.exists() and time.monotonic() < deadline:
                terminal.pump(0.1)
            self.assertTrue(marker.exists(), "Background query was not attempted")
            terminal.pump(0.2)
            self.assertNotIn("\x1b]11;?\x1b\\", "".join(terminal.raw)[previous_output:])
            terminal.write("yes\r")
            terminal.expect(lambda _text: not terminal.process.isalive(), "Extractor did not exit", timeout=20)
            self.assertEqual(terminal.process.exitstatus, 0, "".join(terminal.raw)[-4000:])
        finally:
            terminal.close()

    def test_capture_initialization_honors_configured_cancellation(self):
        """A caller's exit callback interrupts initial media extraction."""
        env = dict(self.env, FAKE_YTDLP_MODE="hang")
        result = subprocess.run([self.audio_probe, self.base + "/watch", "--expect-capture-cancel"],
                                env=env, cwd=self.root, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_extractor_api_links_from_cpp(self):
        """The public extractor declaration links to the C implementation from C++."""
        result = subprocess.run([self.audio_probe, self.base + "/watch", "--expect-cpp-linkage"],
                                env=self.env, cwd=self.root, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])

    def test_separate_audio_produces_samples(self):
        """Decode audio from a separate extracted stream."""
        env = dict(self.env, FAKE_YTDLP_MODE="split")
        result = subprocess.run([self.audio_probe, self.base + "/watch?first=1&second=2"],
                                env=env, cwd=self.root, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
        self.assertIn("/audio.wav", self.requests)

    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg is required to generate a combined media fixture")
    def test_combined_stream_produces_audio_samples(self):
        """Decode audio from a combined extracted stream."""
        combined = self.root / "combined.avi"
        ffmpeg = shutil.which("ffmpeg")
        assert ffmpeg is not None
        subprocess.run([ffmpeg, "-v", "error", "-loop", "1", "-i", str(self.root / "video.ppm"),
                        "-i", str(self.root / "audio.wav"), "-t", "1", "-c:v", "mpeg4", "-c:a", "pcm_s16le",
                        "-shortest", str(combined)], check=True, capture_output=True, timeout=30)
        env = dict(self.env, FAKE_YTDLP_MODE="combined", FAKE_YTDLP_VIDEO=self.base + "/combined.avi")
        result = subprocess.run([self.audio_probe, self.base + "/watch?first=1&second=2"],
                                env=env, cwd=self.root, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
        self.assertIn("/combined.avi", self.requests)
        self.assertNotIn("/audio.wav", self.requests)
