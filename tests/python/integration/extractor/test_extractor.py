import pytest
"""Exercise yt-dlp argument handling and separate stream playback end to end."""
from ascii_chat_tests.media import ppm_image, write_pcm_wav
from ascii_chat_tests.cli import clean_environment
from ascii_chat_tests.cli import cli_command

import argparse
import functools
import http.server
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile
import threading
import unittest
import wave


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
        requests = self.requests
        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                """Record each media request before serving it."""
                requests.append(self.path)
                super().do_GET()
            def log_message(self, *_args):
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
                        FAKE_YTDLP_AUDIO=self.base + "/audio.wav")

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

    def test_combined_stream_is_used_for_both_decoders(self):
        """Use the combined URL without requesting separate audio."""
        result = self.run_mirror("combined")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
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

    def test_unrelated_extraction_failure_does_not_retry(self):
        """Do not retry extraction after an unrelated failure."""
        result = self.run_mirror("error")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("/audio.wav", self.requests)
        self.assertNotIn("/video.ppm", self.requests)

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
        subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-loop", "1", "-i", str(self.root / "video.ppm"),
                        "-i", str(self.root / "audio.wav"), "-t", "1", "-c:v", "mpeg4", "-c:a", "pcm_s16le",
                        "-shortest", str(combined)], check=True, capture_output=True, timeout=30)
        env = dict(self.env, FAKE_YTDLP_MODE="combined", FAKE_YTDLP_VIDEO=self.base + "/combined.avi")
        result = subprocess.run([self.audio_probe, self.base + "/watch?first=1&second=2"],
                                env=env, cwd=self.root, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-4000:])
        self.assertIn("/combined.avi", self.requests)
        self.assertNotIn("/audio.wav", self.requests)
