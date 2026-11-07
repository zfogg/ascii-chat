"""Verify file audio in mirror mode and local/remote audio in a real call.

Run: python tests/integration/render_file_audio.py build/bin/ascii-chat
Requires ffmpeg/ffprobe; no camera, microphone, or speakers are required.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401
from ascii_chat_tests.cli import start_cli
from ascii_chat_tests.media import lavfi_fixture
from ascii_chat_tests.cli import cli_command


import functools
import http.server
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
import threading


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
        # The HTTP fixture serves sequential bytes; faststart avoids requiring range requests.
        for frequency in (440, 880):
            lavfi_fixture(['testsrc2=size=64x48:rate=10', f'sine=frequency={frequency}:sample_rate=48000'],
                          root / f'{frequency}.mp4', ffmpeg_options=['-v', 'error'],
                          output_options=['-t', '30', '-c:v', 'mpeg4', '-c:a', 'aac', '-movflags', '+faststart'],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        run("ffmpeg", "-v", "error", "-i", str(root / "440.mp4"), "-t", "1",
            "-c", "copy", str(root / "short440.mp4"))
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(root))
        media_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        media_thread = threading.Thread(target=media_server.serve_forever, daemon=True)
        media_thread.start()
        processes = []
        handles = []

        def launch(name, *args):
            handle = open(root / f"{name}.stderr", "wb")
            handles.append(handle)
            process = start_cli(binary, "--log-level", "info", "--log-file", str(root / f"{name}.log"), *args,
                                       env=env, stdout=subprocess.DEVNULL, stderr=handle)
            processes.append(process)
            return process

        try:
            # A paused file must retain video until snapshot stop/finalization,
            # even without a later resume to expose the gap to the encoder.
            for name, source in (
                ("paused", ("--file", str(root / "440.mp4"), "--pause")),
                ("synthetic", ("--test-pattern", "--test-audio")),
                ("playlist", ("--file", str(root / "short440.mp4"), "--file", str(root / "880.mp4"))),
            ):
                output = root / f"{name}.mp4"
                process = launch(name, "mirror", *source, "--no-audio-playback",
                                 "--snapshot", "--snapshot-delay", "2", "--fps", "10",
                                 "--width", "32", "--height", "12", "--render-file", str(output))
                process.wait(timeout=30)
                assert process.returncode == 0, f"{name} exited with status {process.returncode}"
                streams = json.loads(run("ffprobe", "-v", "error", "-show_streams", "-of", "json", str(output)).stdout)["streams"]
                durations = {stream["codec_type"]: float(stream["duration"]) for stream in streams}
                assert {"audio", "video"} <= durations.keys(), durations
                assert 1.8 <= durations["video"] <= 3.0, (name, durations)
                assert abs(durations["audio"] - durations["video"]) < 0.15, (name, durations)
                pcm = run("ffmpeg", "-v", "error", "-i", str(output), "-vn", "-ac", "1",
                          "-ar", "48000", "-f", "f32le", "pipe:1").stdout
                samples = struct.unpack("<" + "f" * (len(pcm) // 4), pcm)
                tail = samples[-48000:]
                rms = math.sqrt(sum(sample * sample for sample in tail) / len(tail))
                assert rms < 0.001 if name == "paused" else rms > 0.005, (name, rms)
                print(f"{name}: finalized audio/video durations {durations}, tail RMS {rms:.4f}")
                if name == "playlist":
                    verify(output, (440, 880))
            if "--transitions-only" in sys.argv[2:]:
                return
            mirror = launch("mirror", "mirror", "--file", str(root / "440.mp4"), "--audio-capture-source", "media", "--no-audio-playback",
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
                # Disconnect one source while the other client's mixer is still running.
                recording_seconds = 8 if frequency == 440 else 12
                media_args = ("--url", f"http://127.0.0.1:{media_server.server_port}/{frequency}.mp4") if frequency == 440 else ("--file", str(root / f"{frequency}.mp4"))
                clients.append(launch(str(frequency), "client", "127.0.0.1", "--port", str(port),
                                      "--video-codec", "raw", "--no-audio-playback",
                                      *media_args, "--render-file", str(root / f"call-{frequency}.mp4"),
                                      "--width", "32", "--height", "16", "--fps", "10", "--splash-screen=false", "--snapshot", "--snapshot-delay", str(recording_seconds)))
                deadline = time.monotonic() + 10
                while not (root / f"{frequency}.log").exists() or "Connected" not in (root / f"{frequency}.log").read_text(errors="replace"):
                    assert clients[-1].poll() is None, "Client failed to connect"
                    assert time.monotonic() < deadline, "Client never connected"
                    time.sleep(0.1)
            for client in clients:
                try:
                    client.wait(timeout=25)
                except subprocess.TimeoutExpired as error:
                    raise AssertionError("Snapshot call must stop and join its audio sender before exiting") from error
                assert client.returncode == 0, f"Client exited with status {client.returncode}"
            assert server.poll() is None, "Server exited while removing an audio source"
            for frequency in (440, 880):
                verify(root / f"call-{frequency}.mp4", (440, 880))
            print("Mirror and both client recordings contain the expected tones with synchronized audio/video.")
        except Exception:
            print(f"Recording diagnostic platform={sys.platform}, processes={[(p.pid, p.poll()) for p in processes]}",
                  file=sys.stderr)
            if sys.platform == "darwin":
                for process in processes:
                    if process.poll() is None:
                        trace_path = root / f"{process.pid}.sample"
                        trace = subprocess.run(["sample", str(process.pid), "1", "1", "-file", str(trace_path)],
                                               capture_output=True, text=True, timeout=15)
                        print(f"sample pid={process.pid}: status={trace.returncode}, report={trace_path.exists()}",
                              file=sys.stderr)
                        print(trace.stderr, file=sys.stderr)
                        if trace_path.exists():
                            print(trace_path.read_text(errors="replace"), file=sys.stderr)
                        else:
                            debugger = "/opt/homebrew/opt/llvm/bin/lldb"
                            if not Path(debugger).exists():
                                debugger = "/usr/bin/lldb"
                            backtrace = subprocess.run([debugger, "--batch", "--attach-pid", str(process.pid),
                                                        "-o", "thread backtrace all", "-o", "detach", "-o", "quit"],
                                                       capture_output=True, text=True, timeout=20)
                            print(backtrace.stdout + backtrace.stderr, file=sys.stderr)
            for path in root.glob("*.log"):
                for line in path.read_text(errors="replace").splitlines():
                    if any(word in line for word in ("ERROR", "FATAL", "Connected", "SNAPSHOT", "handshake")):
                        print(line, file=sys.stderr)
                print(f"--- {path.name} ---\n{path.read_text(errors='replace')[-3000:]}", file=sys.stderr)
            raise
        finally:
            for process in processes:
                if process.poll() is None:
                    if os.name == "nt":
                        process.terminate()
                    else:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            for handle in handles:
                handle.close()
            media_server.shutdown()
            media_server.server_close()
            media_thread.join()


if __name__ == "__main__":
    main()


