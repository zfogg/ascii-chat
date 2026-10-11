"""Exercise CLI video codecs against a fresh local server (no Criterion required).

Usage: python tests/integration/cli_video_encoding.py build/bin/ascii-chat[.exe]
Explicit encoders may be unavailable; failures must be actionable and never send raw.
"""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return str(sock.getsockname()[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    logs = Path(tempfile.mkdtemp(prefix="ascii-cli-video-"))
    print(f"Logs: {logs}", flush=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("ASCII_CHAT_")}
    env["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"

    def command(name, options):
        return [binary, "--no-check-update", "--log-file", str(logs / f"{name}.log"),
                "--log-level", "debug", *options]

    for name, codec, width in [("default", None, 40), ("auto", "auto", 40),
                                ("hevc", "hevc", 40), ("h264", "h264", 40),
                                ("raw", "raw", 40), ("auto-odd", "auto", 41),
                                ("hevc-odd", "hevc", 41), ("h264-odd", "h264", 41)]:
        port = unused_port()
        with (logs / f"{name}-server.out").open("wb") as server_output:
            server = subprocess.Popen(command(f"{name}-server", ["server", "127.0.0.1", "--port", port,
                                      "--websocket-port", unused_port(), "--status-screen=false"]),
                                      env=env, stdout=server_output, stderr=server_output)
            try:
                deadline = time.monotonic() + 15
                server_log = logs / f"{name}-server.log"
                while time.monotonic() < deadline:
                    assert server.poll() is None, (name, "server exited", server.returncode, logs)
                    if server_log.exists() and "Listening" in server_log.read_text(errors="replace"):
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError((name, "server startup timeout", logs))
                options = ["client", f"127.0.0.1:{port}", "--test-pattern", "--snapshot",
                           "--snapshot-delay", "1", "--width", str(width), "--height", "20",
                           "--splash-screen=false", "--audio=false"]
                if codec is not None:
                    options += ["--video-codec", codec]
                with (logs / f"{name}.out").open("wb") as output:
                    result = subprocess.run(command(name, options), env=env, stdout=output,
                                            stderr=output, timeout=30)
                log = (logs / f"{name}.log").read_text(errors="replace")
                received = server_log.read_text(errors="replace")
                assert server.poll() is None, (name, "server crashed", server.returncode, logs)
                if codec in ("hevc", "h264") and result.returncode != 0:
                    assert "Use --video-codec auto for fallback" in log, (name, result.returncode, logs)
                    assert "switching to" not in log and "IMAGE_FRAME_RAW delivered" not in log
                    assert "actual_type=3000 " not in received
                    print(f"PASS {name}: strict encoder failure (exit {result.returncode})", flush=True)
                else:
                    assert result.returncode == 0, (name, result.returncode, logs)
                    assert "CAPTURE_FRAME_SENT" in log, (name, "no frames sent", logs)
                    assert "DISPLAY_RENDER_RETURNED: frame was rendered" in log, (name, "no rendered frame", logs)
                    assert "input decode failed" not in received and "Invalid encoded video" not in received
                    assert "[FRAME_DISPATCH_SUCCESS] Handler completed successfully for type=300" in received
                    if codec == "raw":
                        assert "encoder created" not in log and "IMAGE_FRAME_RAW delivered" in log
                    if codec in ("hevc", "h264"):
                        assert "switching to" not in log
                        packet = "3002" if codec == "hevc" else "3003"
                        assert f"actual_type={packet} " in received
                    if name == "auto-odd":
                        assert "HEVC encoding failed; automatic video is switching to H.264" in log
                        assert "H.264 encoding failed; automatic video is switching to raw" in log
                        assert "IMAGE_FRAME_RAW delivered" in log
                    print(f"PASS {name}: rendered snapshot (exit 0)", flush=True)
            finally:
                if server.poll() is None:
                    server.terminate()
                server.wait(timeout=15)

    result = subprocess.run(command("invalid", ["client", "--video-codec", "typo"]),
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    assert result.returncode == 2 and b"must be auto, hevc, h264, or raw" in result.stdout
    print("PASS invalid codec: rejected by option validation", flush=True)


if __name__ == "__main__":
    main()
