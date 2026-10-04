"""Verify file audio in mirror mode and local/remote audio in a real call.

Run: python tests/integration/render_file_audio.py build/bin/ascii-chat
Requires ffmpeg/ffprobe; no camera, microphone, or speakers are required.
"""

import json
import math
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def verify(path, frequencies):
    streams = json.loads(run("ffprobe", "-v", "error", "-show_streams", "-of", "json", str(path)).stdout)["streams"]
    assert {s["codec_type"] for s in streams} >= {"audio", "video"}, streams
    data = run("ffmpeg", "-v", "error", "-i", str(path), "-vn", "-ac", "1", "-ar", "48000",
               "-f", "f32le", "pipe:1").stdout
    samples = struct.unpack("<" + "f" * (len(data) // 4), data)
    assert len(samples) >= 48000, "Recording must contain at least one second of audio"
    # Check several short windows: network jitter can interrupt a tone's phase continuity.
    for frequency in frequencies:
        amplitudes = []
        for offset in range(24000, len(samples) - 12000, 12000):
            window = samples[offset:offset + 12000]
            real = sum(x * math.cos(2 * math.pi * frequency * i / 48000) for i, x in enumerate(window))
            imag = sum(x * math.sin(2 * math.pi * frequency * i / 48000) for i, x in enumerate(window))
            amplitudes.append(2 * math.hypot(real, imag) / len(window))
        amplitude = max(amplitudes)
        print(f"{path.name}: {frequency} Hz amplitude {amplitude:.4f}")
        assert amplitude > 0.005, f"{path}: missing {frequency} Hz audio (amplitude {amplitude})"
    audio = next(s for s in streams if s["codec_type"] == "audio")
    video = next(s for s in streams if s["codec_type"] == "video")
    assert abs(float(audio["duration"]) - float(video["duration"])) < 0.15, streams


def main():
    binary = str(Path(sys.argv[1]).resolve())
    env = dict(os.environ, LSAN_OPTIONS="detect_leaks=0", ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1", ASCII_CHAT_QUESTION_PROMPT_RESPONSE="y;y;y;y")
    with tempfile.TemporaryDirectory(prefix="ascii-recording-") as directory:
        root = Path(directory)
        for frequency in (440, 880):
            run("ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=10",
                "-f", "lavfi", "-i", f"sine=frequency={frequency}:sample_rate=48000", "-t", "12",
                "-c:v", "libx264", "-c:a", "aac", str(root / f"{frequency}.mp4"))
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        processes = []
        handles = []

        def launch(name, *args):
            handle = open(root / f"{name}.stderr", "wb")
            handles.append(handle)
            process = subprocess.Popen([binary, "--no-check-update", "--log-level", "warn", "--log-file", str(root / f"{name}.log"), *args],
                                       env=env, stdout=subprocess.DEVNULL, stderr=handle)
            processes.append(process)
            return process

        try:
            mirror = launch("mirror", "mirror", "--file", str(root / "440.mp4"), "--audio-source", "media",
                            "--snapshot", "--snapshot-delay", "3", "--fps", "10", "--width", "40", "--height", "12",
                            "--render-file", str(root / "mirror.mp4"))
            mirror.wait(timeout=20)
            assert mirror.returncode == 0, f"Mirror exited with status {mirror.returncode}"
            verify(root / "mirror.mp4", (440,))
            server = launch("server", "server", "127.0.0.1", "--port", str(port), "--status-screen=false")
            deadline = time.monotonic() + 15
            while True:
                assert server.poll() is None, "Server exited during startup"
                log = root / "server.log"
                if log.exists() and "Listening on 127.0.0.1:" in log.read_text(errors="replace"):
                    break
                assert time.monotonic() < deadline, "Server never became ready"
                time.sleep(0.1)
            clients = []
            for frequency in (440, 880):
                clients.append(launch(str(frequency), "client", "127.0.0.1", "--port", str(port),
                                      "--file", str(root / f"{frequency}.mp4"), "--render-file", str(root / f"call-{frequency}.mp4"),
                                      "--width", "32", "--height", "16", "--fps", "10", "--splash-screen=false", "--snapshot", "--snapshot-delay", "6"))
                deadline = time.monotonic() + 10
                while not (root / f"{frequency}.log").exists() or "Connected" not in (root / f"{frequency}.log").read_text(errors="replace"):
                    assert clients[-1].poll() is None, "Client failed to connect"
                    assert time.monotonic() < deadline, "Client never connected"
                    time.sleep(0.1)
            for client in clients:
                client.wait(timeout=25)
                assert client.returncode == 0, f"Client exited with status {client.returncode}"
            for frequency in (440, 880):
                verify(root / f"call-{frequency}.mp4", (440, 880))
            print("Mirror and both client recordings contain the expected tones with synchronized audio/video.")
        except Exception:
            for path in root.glob("*.log"):
                for line in path.read_text(errors="replace").splitlines():
                    if any(word in line for word in ("ERROR", "FATAL", "Connected", "SNAPSHOT", "handshake")):
                        print(line, file=sys.stderr)
                print(f"--- {path.name} ---\n{path.read_text(errors='replace')[-3000:]}", file=sys.stderr)
            raise
        finally:
            for process in processes:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            for handle in handles:
                handle.close()


if __name__ == "__main__":
    main()


