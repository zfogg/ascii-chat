import pytest
import shutil
#!/usr/bin/env python3
"""Exercise URL loading and Escape through a real terminal and delayed HTTP media."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time

from ascii_chat_tests.media import lavfi_fixture
from ascii_chat_tests.cli import cli_command

from ascii_chat_tests.terminal import TitleTerminal


pytestmark = [pytest.mark.integration, pytest.mark.terminal, pytest.mark.network, pytest.mark.media]


def test_url_title(binary, request):
    ffmpeg = request.config.getoption("--ffmpeg")
    if not shutil.which(ffmpeg):
        pytest.skip("FFmpeg is required")
    with tempfile.TemporaryDirectory(prefix="url-title-") as temporary:
        media = Path(temporary) / "stream.mkv"
        lavfi_fixture(['testsrc2=size=96x64:rate=10', 'sine=frequency=440:sample_rate=48000'],
                      media, ffmpeg=ffmpeg, ffmpeg_options=['-hide_banner', '-loglevel', 'error', '-y'],
                      output_options=['-t', '20', '-c:v', 'mpeg4', '-c:a', 'pcm_s16le'], timeout=30)
        gate = threading.Event()
        requested = threading.Event()

        class Handler(SimpleHTTPRequestHandler):
            def do_GET(self):
                requested.set()
                if gate.wait(20):
                    try:
                        super().do_GET()
                    except (BrokenPipeError, ConnectionResetError):
                        pass

            def log_message(self, format, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=temporary))
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f"http://127.0.0.1:{server.server_port}/stream.mkv"
        term = TitleTerminal(cli_command(str(binary), "--color=false", "mirror", url,
                              "--utf8=true", "--splash-screen=false", "--fps=10",
                              "--no-audio-playback"), rows=20, cols=60, terminal_replies=True,
                             environment={"TERM": "xterm-256color", "ASCII_CHAT_DISABLE_FIRST_USE": "1"})
        try:
            pattern = re.compile(r"[⠋⠙⠴⠦]  ascii-chat • mirror")
            term.expect(lambda _: requested.is_set() and any(pattern.fullmatch(t) for _, t in term.titles),
                        "No spinner while HTTP response was blocked", timeout=25)
            term.pump(.7)
            assert len({t for _, t in term.titles if pattern.fullmatch(t)}) >= 2, "Spinner did not animate"
            gate.set()
            term.expect_title("🔠 ascii-chat • mirror", timeout=35)
            term.expect(lambda text: any(re.search(r"[.:;oO0#@%]{4}", line) for line in text.splitlines()),
                        "No video frame appeared", timeout=20)
            term.pump(1)
            term.write("\x1b")
            deadline = time.monotonic() + 20
            while term.process.isalive() and time.monotonic() < deadline:
                term.pump(.1)
            term.pump(.3)
            assert not term.process.isalive(), "Escape did not stop streaming"
            assert term.process.exitstatus == 0, (term.process.exitstatus, "".join(term.raw)[-3000:])
            output = "".join(term.raw)
            assert "ERROR_MEDIA_DECODE" not in output, output[-4000:]
            assert "AddressSanitizer" not in output and "runtime error:" not in output, output[-4000:]
        finally:
            gate.set()
            term.close()
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)
    print("PASS delayed URL spinner, video frame, and Escape shutdown")
