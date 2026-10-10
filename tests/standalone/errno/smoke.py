"""Application error-boundary checks. Usage: python smoke.py /path/to/ascii-chat."""

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
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    logs = Path(tempfile.mkdtemp(prefix="ascii-errno-smoke-"))
    print(f"Logs: {logs}", flush=True)
    # The server is a fresh, local test instance. Do not modify known_hosts.
    env = dict(os.environ, ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1")

    def command(name, options):
        return [binary, "--no-check-update", "--log-file", str(logs / f"{name}.log"), *options]

    def run(name, options, expected):
        with (logs / f"{name}-output.log").open("wb") as output:
            result = subprocess.run(command(name, options), env=env, stdout=output,
                                    stderr=output, timeout=30)
        assert result.returncode == expected, (name, result.returncode, expected, logs)
        print(f"PASS {name}: exit {expected}", flush=True)
        return (logs / f"{name}.log").read_text(encoding="utf-8", errors="replace")

    snapshot = ["--snapshot", "--snapshot-delay", "0.3", "--width", "20", "--height", "10",
                "--splash-screen=false"]
    diagnostic = run("mirror", ["mirror", "--test-pattern", "--errno-stacks=0.05", *snapshot], 0)
    assert "Pending error stacks" in diagnostic
    assert "Error registry:" in diagnostic
    missing = run("missing-media", ["mirror", "--file", str(logs / "missing.mp4"), *snapshot], 26)
    assert "Failure chain (outer context -> root cause)" in missing
    assert "Media initialization failed" in missing

    frame = logs / "extensionless-frame"
    frame.write_bytes(b"P6\n2 2\n255\n" + bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0]))
    run("media-fallback", ["mirror", "--file", str(frame), *snapshot], 0)

    port = unused_port()
    with (logs / "server-output.log").open("wb") as output:
        server = subprocess.Popen(command("server", ["server", "127.0.0.1", "--port", port,
                                  "--websocket-port", unused_port(), "--password", "errno-test-secret",
                                  "--status-screen=false"]), env=env, stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                assert server.poll() is None, ("server exited", logs)
                log = logs / "server.log"
                if log.exists() and "Listening" in log.read_text(encoding="utf-8", errors="replace"):
                    break
                time.sleep(0.1)
            for attempt in range(2):
                run(f"rejected-client-{attempt}", ["client", f"127.0.0.1:{port}", "--password",
                    "wrong-test-secret", "--test-pattern", *snapshot], 62)
                assert server.poll() is None, "One client's authentication failure stopped the server"
            print("PASS server survives repeated authentication failures", flush=True)
            run("authenticated-client", ["client", f"127.0.0.1:{port}", "--password",
                "errno-test-secret", "--test-pattern", "--video-codec", "raw",
                *snapshot, "--snapshot-delay", "0"], 0)
        finally:
            server.terminate()
            server.wait(timeout=15)


if __name__ == "__main__":
    main()
