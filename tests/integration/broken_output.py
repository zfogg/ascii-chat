#!/usr/bin/env python3
"""Regression for issue #345: a vanished pipe reader must not leave a busy process.

Run with --binary pointing to a native Debug or Release ascii-chat build.
Uses synthetic frames, loopback connections, and no microphone or camera.
"""

import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    env = dict(os.environ, ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1")
    flags = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with tempfile.TemporaryDirectory(prefix="ascii-chat-broken-output-") as directory:
        directory = Path(directory)
        processes = []

        def launch(name, arguments, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
            process = subprocess.Popen(
                [binary, "--no-check-update", "--log-level", "warn", "--log-file",
                 str(directory / (name + ".log"))] + arguments,
                stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, env=env, **flags)
            processes.append(process)
            return process

        def close_reader(name, arguments):
            process = launch(name, arguments, stdout=subprocess.PIPE)
            first_byte = []
            reader = threading.Thread(target=lambda: first_byte.append(process.stdout.read(1)), daemon=True)
            reader.start()
            reader.join(20)
            assert first_byte and first_byte[0], f"{name}: no frame arrived before closing the pipe"
            started = time.monotonic()
            process.stdout.close()
            try:
                code = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                raise AssertionError(f"{name}: still running 10 seconds after its output consumer closed") from None
            log = (directory / (name + ".log")).read_text(errors="replace")
            assert "AddressSanitizer" not in log, f"{name}: sanitizer failure during shutdown"
            # Client shutdown can propagate its interrupted receive as exit 1.
            assert code in (0, 1), f"{name}: unexpected exit {code}"
            assert "platform_write_all: write() error" not in log, f"{name}: write-error retry storm"
            print(f"PASS {name}: exited in {time.monotonic()-started:.2f}s after pipe closed")

        try:
            media = ["--test-pattern", "--audio=false", "--splash-screen=false", "--width", "80", "--height", "24"]
            close_reader("mirror", ["mirror"] + media)
            port, websocket_port = unused_port(), unused_port()
            server_args = ["server", "--port", str(port), "--websocket-port", str(websocket_port)]
            server = launch("server", server_args + ["--status-screen=false"])
            deadline = time.monotonic() + 10
            while True:
                assert server.poll() is None, "server exited before listening"
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    assert time.monotonic() < deadline, "server did not listen"
                    time.sleep(0.1)
            close_reader("client", ["client", f"127.0.0.1:{port}", "--video-codec", "raw"] + media)
            server.kill()
            server.wait()

            # A forced status display still works without a console, but must
            # not generate keyboard/terminal errors on every frame.
            status = launch("status", server_args + ["--status-screen"])
            time.sleep(3)
            assert status.poll() is None, "status server exited"
            status.kill()
            status.wait()
            log = (directory / "status.log").read_text(errors="replace")
            for message in ["Cannot query terminal descriptor", "Failed to get console mode",
                            "keyboard_read_nonblocking: read() returned 0"]:
                assert message not in log, f"redirected status repeatedly attempted terminal operations: {message}"
            print("PASS redirected status: no terminal or keyboard error storm")

            # Generate a small video so this test also runs in a fresh checkout.
            fixture = directory / "paused.y4m"
            frame = b"FRAME\n" + bytes([128]) * (64 * 48 * 3 // 2)
            fixture.write_bytes(b"YUV4MPEG2 W64 H48 F30:1 Ip A1:1 C420jpeg\n" + frame * 90)
            paused = launch("paused", ["mirror", "--file", str(fixture), "--pause", "--audio=false",
                                       "--splash-screen=false"])
            time.sleep(3)
            assert paused.poll() is None, "paused playback exited"
            paused.kill()
            paused.wait()
            log = (directory / "paused.log").read_text(errors="replace")
            assert "keyboard_read_nonblocking: read() returned 0" not in log, "stdin EOF warning storm"
            print("PASS paused playback: redirected stdin EOF is quiet")
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                process.wait()
                if process.stdout:
                    process.stdout.close()


if __name__ == "__main__":
    main()
