#!/usr/bin/env python3
"""Exercise the actual UI through a resizable terminal.

Requires pyte and pywinpty (Windows) or pexpect (POSIX), plus ffmpeg.
Run: python tests/integration/terminal_ui.py --binary build/bin/ascii-chat.exe
The test creates its own media and uses only loopback network connections.
"""

import argparse
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import tempfile
import threading
import time

import pyte


class Terminal:
    def __init__(self, argv, rows=12, cols=19):
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)
        self.output = queue.Queue()
        self.raw = []
        if os.name == "nt":
            from winpty import PtyProcess

            # Match a UTF-8 terminal, including its console code page.
            command = "chcp 65001>nul & " + subprocess.list2cmdline(argv)
            self.process = PtyProcess.spawn(
                ["cmd.exe", "/d", "/c", command], dimensions=(rows, cols)
            )
        else:
            import pexpect

            self.process = pexpect.spawn(
                argv[0], argv[1:], dimensions=(rows, cols), encoding="utf-8"
            )
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            while True:
                if os.name == "nt":
                    data = self.process.read(65536)
                else:
                    data = self.process.read_nonblocking(65536, timeout=None)
                self.output.put(data)
        except (EOFError, OSError):
            pass
        except Exception as exc:
            if type(exc).__name__ != "EOF":
                self.output.put(str(exc))

    def pump(self, seconds=0.2):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                chunk = self.output.get(timeout=0.02)
                self.raw.append(chunk)
                self.stream.feed(chunk)
            except queue.Empty:
                pass
        return "\n".join(self.screen.display)

    def expect(self, predicate, description, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.pump()
            if predicate(text):
                return text
        raise AssertionError(f"{description}\nRendered terminal:\n{text}")

    def resize(self, rows, cols):
        self.process.setwinsize(rows, cols)
        self.screen.resize(rows, cols)

    def write(self, text):
        if os.name == "nt":
            self.process.write(text)
        else:
            self.process.send(text)

    def interrupt(self):
        self.write("\x03")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and self.process.isalive():
            self.pump(0.1)
        assert not self.process.isalive(), "UI shutdown did not join its worker"

    def close(self):
        if os.name == "nt" and self.process.isalive():
            subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.name != "nt":
            self.process.terminate(force=True)
        self.reader.join(timeout=2)


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-ui-") as directory:
        root = Path(directory)
        media = root / "test.mp4"
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
             "testsrc2=size=160x120:rate=10", "-t", "12", "-c:v", "libx264",
             "-pix_fmt", "yuv420p", str(media)], check=True
        )
        base = [binary, "--no-check-update", "--log-file", str(root / "application.log"), "--log-level", "info"]
        sessions = []

        def terminal(name, argv, **kwargs):
            term = Terminal(base + argv, **kwargs)
            sessions.append((name, term))
            return term

        def small(term):
            return term.expect(lambda text: "Terminal too small" in text,
                               "Expected the size warning")

        def video(term):
            return term.expect(lambda text: "Terminal too small" not in text
                               and "Keyboard Shortcuts" not in text
                               and len(text.strip()) > 100,
                               "Expected the media screen")

        try:
            port = free_port()
            websocket_port = free_port()
            status = terminal("status", ["server", "127.0.0.1", "--port", str(port),
                               "--websocket-port", str(websocket_port), "--status-screen"])
            small(status)
            # The WebSocket handshake must continue behind the warning.
            with socket.create_connection(("127.0.0.1", websocket_port), timeout=3) as sock:
                sock.settimeout(3)
                sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                             b"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                             b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n")
                assert b"101" in sock.recv(4096), "WebSocket handshake stopped while covered"
            status.write("/should-not-replay")
            status.pump(0.3)
            status.resize(25, 80)
            normal = status.expect(lambda text: "Terminal too small" not in text and "127.0.0.1" in text,
                                   "Expected status recovery")
            assert "should-not-replay" not in normal
            status.resize(2, 2)
            status.pump(0.4)
            status.resize(12, 19)
            small(status)
            status.close()
            print("PASS status: startup, resize, tiny terminal, input suppression, live handshake")

            mirror_args = ["mirror", "--file", str(media), "--loop", "--splash-screen=false",
                           "--audio=false", "--color-mode", "none"]
            mirror = terminal("mirror", mirror_args)
            small(mirror)
            mirror.resize(40, 80)
            video(mirror)
            mirror.write("?")
            mirror.expect(lambda text: "Keyboard Shortcuts" in text, "Expected help")
            mirror.resize(12, 19)
            small(mirror)
            mirror.write("?m ")
            mirror.pump(0.4)
            mirror.resize(40, 80)
            help_text = mirror.expect(lambda text: "Keyboard Shortcuts" in text, "Expected help restoration")
            assert "100%" in help_text, "Controls were replayed after resize"
            mirror.write("?")
            video(mirror)
            mirror.write(" ")
            mirror.pump(0.5)
            paused = mirror.pump(0.3)
            assert mirror.pump(0.3) == paused, "Playback did not pause"
            mirror.resize(12, 19)
            small(mirror)
            mirror.resize(40, 80)
            video(mirror)
            # Restoring a paused screen must not resume capture.
            paused = mirror.pump(0.3)
            assert mirror.pump(0.3) == paused, "Resize changed the paused state"
            mirror.write(" ")
            mirror.expect(lambda text: text != paused, "Expected playback to resume")
            mirror.close()
            print("PASS media: startup, help priority, blocked keys, paused resize, recovery")

            explicit = terminal("explicit", mirror_args + ["--width", "60", "--height", "20"])
            small(explicit)
            explicit.resize(40, 80)
            video(explicit)
            explicit.resize(12, 19)
            small(explicit)
            explicit.interrupt()
            explicit.close()
            print("PASS explicit dimensions do not mask the physical terminal size")

            recording = root / "recorded.mp4"
            recorder = terminal("recording", mirror_args + ["--render-file", str(recording)])
            small(recorder)
            recorder.pump(2)
            recorder.interrupt()
            recorder.close()
            probe = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=nb_frames",
                "-of", "csv=p=0", str(recording)], capture_output=True, text=True, check=True)
            assert any(line.isdigit() and int(line) > 1 for line in probe.stdout.splitlines()), probe.stdout
            print("PASS recording continues behind the warning and flushes on interrupt")

            snapshot = subprocess.run(base + ["mirror", "--file", str(media), "--snapshot",
                "--snapshot-delay", "0", "--width", "20", "--height", "10", "--audio=false",
                "--splash-screen=false", "--color-mode", "none", "--strip-ansi"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
            assert snapshot.returncode == 0, snapshot.stderr.decode(errors="replace")[-2000:]
            assert snapshot.stdout.strip() and b"Terminal too small" not in snapshot.stdout
            print("PASS redirected snapshot remains synchronous and contains only media output")
        finally:
            if args.artifacts and (root / "application.log").exists():
                args.artifacts.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / "application.log", args.artifacts / "application.log")
            for name, term in sessions:
                if args.artifacts:
                    args.artifacts.mkdir(parents=True, exist_ok=True)
                    (args.artifacts / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
                    (args.artifacts / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
                try:
                    term.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
