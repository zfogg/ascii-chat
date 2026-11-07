import pytest
import sys
#!/usr/bin/env python3
"""Native export process IO and independent Kitty protocol reconstruction."""

from ascii_chat_tests.media import ppm_image, reconstruct

import argparse
import base64
import ctypes as c
import os
from pathlib import Path
import re
import subprocess
import tempfile


pytestmark = [pytest.mark.integration, pytest.mark.media, pytest.mark.skipif(sys.platform == "win32", reason="POSIX Kitty output fixture")]


def check_mp4_boxes(data):
    offset = 0
    while offset < len(data):
        assert len(data) - offset >= 8, "Trailing non-container bytes on binary stdout"
        size = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        assert kind in (b"ftyp", b"moov", b"moof", b"mdat", b"mfra", b"sidx", b"styp", b"free", b"skip"), kind
        header = 8
        if size == 1:
            assert len(data) - offset >= 16
            size = int.from_bytes(data[offset + 8:offset + 16], "big")
            header = 16
        assert header <= size <= len(data) - offset, (offset, size, len(data))
        offset += size


def test_kitty_output(binary):
    binary = str(binary)
    env = dict(os.environ, TERM="dumb", TESTING="1", ASAN_OPTIONS="detect_leaks=0")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        ppm = ppm_image(8, 8, bytes([220, 100, 35]) * 64)
        common = [binary, "--log-file", os.devnull, "mirror", "--file", "-", "--snapshot", "--snapshot-delay", "0",
                  "--width", "20", "--height", "10", "--color-mode", "truecolor"]

        def launch(flags, success=True):
            proc = subprocess.run(common + flags, input=ppm, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  cwd=root, env=env, timeout=45)
            assert (proc.returncode == 0) == success, (proc.returncode, proc.stderr[-4000:])
            return proc

        protocol = launch(["--render-file=--kitty"]).stdout
        width, height, rgba, chunks = reconstruct(protocol)
        assert chunks > 1 and len(set(rgba)) > 2
        assert not list(root.iterdir()), list(root.iterdir())
        print(f"protocol-only: {width}x{height}, {chunks} chunks, no filesystem output")
        assert reconstruct(launch(["--render-kitty"]).stdout)[:3] == (width, height, rgba)
        saved = root / "snapshot.png"
        sw, sh, srgb, _ = reconstruct(launch(["--render-kitty", "--render-file", str(saved)]).stdout)
        assert (sw, sh, srgb) == (width, height, rgba)
        decoded = subprocess.run(["ffmpeg", "-v", "error", "-i", str(saved), "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                                 capture_output=True, check=True).stdout
        assert decoded == rgba, "Saved PNG and Kitty pixels differ"
        launch(["--render-kitty", "--render-file=-"], success=False)
        failed_save = launch(["--render-kitty", "--render-file", str(root / "absent" / "snapshot.png")], success=False)
        assert not failed_save.stdout, "Failed output must not display a successful snapshot"
        rejected = subprocess.run([binary, "mirror", "--render-file=--kitty"], capture_output=True, env=env, timeout=15)
        assert rejected.returncode != 0 and b"snapshot" in rejected.stderr.lower()
        # Repeat fresh processes so delayed codec startup diagnostics cannot hide
        # behind a successful earlier encoder initialization.
        for _ in range(3):
            video = launch(["--render-file=-"]).stdout
            assert video[4:8] == b"ftyp", repr(video[:100])
            check_mp4_boxes(video)
        output = root / "stdout.mp4"
        output.write_bytes(video)
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(output), "-f", "null", "-"], check=True)
        print("saved PNG equals Kitty RGBA; stdin -> binary stdout MP4 decodes; invalid combinations rejected")
        for output_flag in ["--render-file=-", "--render-file=--kitty"]:
            proc = subprocess.Popen(common + [output_flag], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, cwd=root, env=env)
            proc.stdout.close()
            proc.stdout = None
            _, diagnostic = proc.communicate(ppm, timeout=45)
            assert proc.returncode != 0, (output_flag, diagnostic[-2000:])
        print("closed consumer: binary and Kitty export report failure")
        if os.name != "nt":
            import pty
            master, slave = pty.openpty()
            try:
                proc = subprocess.run(common + ["--render-file=--kitty"], input=ppm, stdout=slave,
                                      stderr=subprocess.PIPE, cwd=root, env=env, timeout=45)
                assert proc.returncode != 0 and b"capable terminal" in proc.stderr, proc.stderr[-2000:]
            finally:
                os.close(slave)
                os.close(master)
            print("unsupported TTY: rejected with capability diagnostic")
