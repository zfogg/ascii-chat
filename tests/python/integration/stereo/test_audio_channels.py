import pytest
"""Verify stereo file -> native client -> server mix-minus -> native client without audio hardware."""

from ascii_chat_tests.media import lavfi_fixture

import argparse
import contextlib
import json
import math
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time

from ascii_chat_tests.process import Service, free_port, server_options


pytestmark = [pytest.mark.integration, pytest.mark.network, pytest.mark.media]


def tone_amplitude(samples, channel, frequency):
    values = samples[channel::2]
    real = sum(value * math.cos(2 * math.pi * frequency * i / 48000) for i, value in enumerate(values))
    imag = sum(value * math.sin(2 * math.pi * frequency * i / 48000) for i, value in enumerate(values))
    return 2 * math.hypot(real, imag) / len(values)


def test_stereo_call(binary, tmp_path):
    if not shutil.which("ffmpeg") or not shutil.which("ffprobe"):
        pytest.skip("ffmpeg and ffprobe are required")
    root = tmp_path
    media = root / "stereo.mkv"
    lavfi_fixture(["testsrc2=size=64x48:rate=10", "aevalsrc=0.2*sin(2*PI*440*t)|0.2*sin(2*PI*880*t):s=48000"], str(media), ffmpeg_options=["-y", "-v", "error"], output_options=["-t", "12", "-c:v", "ffv1", "-c:a", "pcm_f32le"], timeout=30)
    port, websocket = free_port(), free_port()
    while websocket == port:
        websocket = free_port()
    with contextlib.ExitStack() as cleanup:
        def launch(name, mode, options, dump=False):
            directory = root / name
            directory.mkdir(exist_ok=True)
            environment = {"TMPDIR": str(directory), "TMP": str(directory), "TEMP": str(directory)}
            if dump:
                environment["ASCIICHAT_DUMP_AUDIO"] = "1"
                environment["ASCIICHAT_DUMP_AUDIO_DIR"] = str(directory)
            service = Service(binary, directory, name, mode, options, environment)
            cleanup.callback(service.stop)
            return service

        server = launch("server", "server", server_options(port, websocket) + ["--no-encrypt"])
        deadline = time.monotonic() + 20
        while True:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    break
            except OSError:
                if time.monotonic() >= deadline or server.process.poll() is not None:
                    raise AssertionError("Server did not become ready: " + server.text()[-4000:])
                time.sleep(0.05)
        common = [f"127.0.0.1:{port}", "--no-encrypt", "--audio=true", "--no-audio-playback",
                  "--splash-screen=false", "--snapshot", "--snapshot-delay=7", "--width=40", "--height=12"]
        receiver = launch("receiver", "client", common + ["--test-pattern", "--audio-capture-source=remote"], True)
        sender = launch("sender", "client", common + ["--file", str(media), "--audio-capture-source=media"])
        for service in (sender, receiver):
            code = service.process.wait(timeout=35)
            assert code == 0, (code, service.text()[-6000:])
        assert server.process.poll() is None
        received = root / "receiver" / "audio_playback_received.wav"
        probe = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-show_streams", "-of", "json", str(received),
        ]))
        assert probe["streams"][0]["channels"] == 2, probe
        pcm = subprocess.check_output([
            "ffmpeg", "-v", "error", "-i", str(received), "-f", "f32le", "-ar", "48000", "pipe:1",
        ])
        samples = struct.unpack("<" + "f" * (len(pcm) // 4), pcm)
        assert len(samples) >= 48000 * 2 * 3, len(samples)
        assert all(math.isfinite(value) for value in samples)
        # Pick the loudest complete second, independently of frequency or
        # channel separation. Process startup can delay the sender on busy
        # hosts, while the receiver already records the server's silence.
        windows = (samples[start:start + 48000 * 2]
                   for start in range(48000, len(samples) - 48000 * 2 + 1, 48000))
        window = max(windows, key=lambda values: sum(value * value for value in values))
        measured = {(channel, frequency): tone_amplitude(window, channel, frequency)
                    for channel in (0, 1) for frequency in (440, 880)}
        assert measured[0, 440] > 0.03 and measured[1, 880] > 0.03, measured
        assert measured[0, 440] > 6 * measured[0, 880], measured
        assert measured[1, 880] > 6 * measured[1, 440], measured
        print("PASS: stereo survived native capture, transport, server mix-minus, decoding and WAV output", measured)
