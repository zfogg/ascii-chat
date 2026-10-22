#!/usr/bin/env python3
"""Exercise the actual UI through a resizable terminal.

Requires pyte and pywinpty (Windows) or pexpect (POSIX), plus ffmpeg.
Run: python tests/integration/terminal_ui.py --binary build/bin/ascii-chat.exe
The test creates its own media and uses only loopback network connections.
"""

import argparse
import importlib.util
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import tempfile
import threading
import time

prerequisites = ["pyte"] + ([] if os.name == "nt" else ["pexpect"])
missing = [name for name in prerequisites if importlib.util.find_spec(name) is None]
if missing:
    print("SKIP: terminal tests need " + ", ".join(missing) + "; install tests/requirements-terminal.txt")
    raise SystemExit(77)

import pyte


class CaptureScreen(pyte.Screen):
    """Handle REP, used by the renderer and ConPTY to compress repeated cells."""

    def reset(self):
        self.last_character = ""
        super().reset()

    def draw(self, data):
        super().draw(data)
        if data:
            self.last_character = data[-1]

    def repeat_character(self, count=1):
        if self.last_character:
            self.draw(self.last_character * (count or 1))


class CaptureStream(pyte.Stream):
    csi = dict(pyte.Stream.csi, b="repeat_character")
    events = pyte.Stream.events | {"repeat_character"}


def check_capture_emulator():
    screen = CaptureScreen(20, 2)
    stream = CaptureStream(screen)
    # Split an escape sequence across reads, just as a PTY can.
    stream.feed("A\x1b[4")
    stream.feed("bB\x1b[0bC\x1b[b")
    assert screen.display[0].startswith("AAAAABBCC"), screen.display[0]


class Terminal:
    def __init__(self, argv, rows=12, cols=19, terminal_replies=False, code_page=65001, environment=None):
        self.terminal_replies = terminal_replies
        self.background_reply = "\x1b]11;rgb:0000/0000/0000\x1b\\"
        self.query_tail = ""
        self.screen = CaptureScreen(cols, rows)
        self.stream = CaptureStream(self.screen)
        self.output = queue.Queue()
        self.raw = []
        child_environment = os.environ.copy()
        child_environment.update(environment or {})
        if os.name == "nt":
            from winpty import PtyProcess

            # Match a UTF-8 terminal, including its console code page.
            command = f"chcp {code_page}>nul & " + subprocess.list2cmdline(argv)
            self.process = PtyProcess.spawn(
                ["cmd.exe", "/d", "/c", command], dimensions=(rows, cols), env=child_environment
            )
        else:
            import pexpect

            self.process = pexpect.spawn(
                argv[0], argv[1:], dimensions=(rows, cols), encoding="utf-8", env=child_environment
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
                if self.terminal_replies:
                    queries = self.query_tail + chunk
                    query = "\x1b]11;?\x1b\\"
                    for _ in range(queries.count(query)):
                        try:
                            self.write(self.background_reply)
                        except (EOFError, OSError):
                            # A queued query can outlive the exiting PTY child.
                            pass
                    self.query_tail = queries[-(len(query) - 1):]
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
            # The reader continues draining the PTY. Retain output without
            # parsing queued frames while measuring the process exit deadline.
            try:
                self.raw.append(self.output.get(timeout=0.05))
            except queue.Empty:
                pass
        assert not self.process.isalive(), "UI shutdown did not join its worker"

    def close(self):
        if os.name == "nt" and self.process.isalive():
            try:
                subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=5, check=False)
            except subprocess.TimeoutExpired:
                pass
        if os.name != "nt":
            self.process.terminate(force=True)
        self.reader.join(timeout=.2 if os.name == "nt" else 2)


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    check_capture_emulator()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--debug-sync", action="store_true")
    parser.add_argument("--debug-updates", action="store_true")
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

        def terminal(name, argv, check_updates=False, environment=None, **kwargs):
            command = [binary, "--log-file", str(root / "application.log"), "--log-level", "info"]
            if not check_updates:
                command.insert(1, "--no-check-update")
            term = Terminal(command + argv, environment=environment, **kwargs)
            sessions.append((name, term))
            return term

        def finish(term):
            term.interrupt()
            if os.name != "nt":
                term.process.close()
                assert term.process.exitstatus == 0, term.process.exitstatus
            term.close()

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
                               "--websocket-port", str(websocket_port), "--status-screen"], environment={
                "ASCII_CHAT_TEST_UPDATE_LATEST_VERSION": "v0.0.0",
                "ASCII_CHAT_UPDATE_CACHE_DIR": str(root / "status-update-cache"),
            })
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
            status.write("\x1b[24~")
            status.expect(lambda text: "Check for updates" in text or "You're up to date" in text or "Update available" in text or "Update check unavailable" in text,
                          "F12 did not open the update checker over the server status UI")
            status.write("\x1b")
            status.expect(lambda text: "127.0.0.1" in text,
                          "Esc did not restore the server status UI after the update checker")
            status.resize(2, 2)
            status.pump(0.4)
            status.resize(12, 19)
            small(status)
            finish(status)
            print("PASS status: startup, resize, tiny terminal, input suppression, live handshake")

            mirror_args = ["mirror", "--file", str(media), "--loop", "--splash-screen=false",
                           "--audio=false", "--color-mode", "none"]
            mirror = terminal("mirror", mirror_args, environment={
                "ASCII_CHAT_TEST_UPDATE_LATEST_VERSION": "v0.0.0",
                "ASCII_CHAT_UPDATE_CACHE_DIR": str(root / "f12-cache"),
            })
            small(mirror)
            mirror.resize(40, 100)
            video(mirror)
            mirror.write("?")
            mirror.expect(lambda text: "Keyboard Shortcuts" in text, "Expected help")
            mirror.write("\x1b[24~")
            mirror.expect(lambda text: "Check for updates" in text or "You're up to date" in text or "Update available" in text or "Update check unavailable" in text,
                          "F12 did not replace the help overlay with the update checker")
            mirror.write("\x1b")
            mirror.expect(lambda text: "Keyboard Shortcuts" in text,
                          "Esc did not restore the covered help overlay")
            print("PASS update checker: F12 opens the retained overlay over help and Esc restores it")
            if args.debug_sync:
                mirror.resize(40, 160)
                mirror.write("0")
                mirror.expect(lambda text: "Sync primitives" in text and "Keyboard Shortcuts" not in text,
                              "Sync did not replace help")
                mirror.write("?")
                mirror.expect(lambda text: "Keyboard Shortcuts" in text and "Sync primitives |" not in text,
                              "Help did not replace sync")
                mirror.write("0")
                mirror.expect(lambda text: "Sync primitives" in text, "Sync did not reopen")
                mirror.write("0")
                video(mirror)
                mirror.write("?")
                mirror.expect(lambda text: "Keyboard Shortcuts" in text, "Help did not reopen")
                print("PASS sync/help replacement and closing to media")
            mirror.resize(12, 19)
            small(mirror)
            mirror.write("?m ")
            mirror.pump(0.4)
            mirror.resize(40, 100)
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
            mirror.resize(30, 100)
            video(mirror)
            # Restoring a paused screen must not resume capture.
            paused = mirror.pump(0.3)
            assert mirror.pump(0.3) == paused, "Resize changed the paused state"
            mirror.write(" ")
            mirror.expect(lambda text: text != paused, "Expected playback to resume")
            finish(mirror)
            print("PASS media: startup, help priority, blocked keys, paused resize, recovery")

            if args.debug_updates:
                update_command = ["mirror", "--file", str(media), "--loop", "--splash-screen=true",
                                  "--audio=false", "--color-mode", "none"]
                update_env = {
                    "ASCII_CHAT_TEST_UPDATE_LATEST_VERSION": "v99.0.0",
                    "ASCII_CHAT_UPDATE_CACHE_DIR": str(root / "update-cache"),
                }
                startup = terminal("startup-update", update_command, check_updates=True,
                                   environment=update_env, rows=40, cols=100)
                startup.expect(lambda text: "Update Available" in text,
                               "An older build did not show the startup update prompt")
                # The prompt may replace a short-lived splash between samples.
                assert "[ ok ]" in "".join(startup.raw), "Startup did not render compact initialization logs"
                startup.write("n")
                video(startup)
                # Let the post-splash capture pipeline finish before exercising shutdown.
                # A Ctrl-C during source creation is not the dismissal behavior this scenario covers.
                startup.pump(0.5)
                startup.write("\x1b[24~")
                startup.expect(lambda text: "Update available" in text,
                               "F12 did not recheck the dismissed release")
                startup.write("\x1b")
                finish(startup)

                dismissed = terminal("dismissed-update", update_command, check_updates=True,
                                     environment=update_env, rows=40, cols=100)
                dismissed_media = video(dismissed)
                # The forced update seam is synchronous, so give the splash/prompt
                # hand-off a bounded moment and then prove this version's dismissal
                # still suppresses the startup modal.
                dismissed_media = dismissed.pump(0.5) or dismissed_media
                assert "Update Available" not in dismissed_media, (
                    "Weekly dismissal did not suppress the repeated startup update prompt"
                )
                finish(dismissed)

                handoff = terminal("startup-f12", update_command, check_updates=True,
                                   environment={**update_env, "ASCII_CHAT_UPDATE_CACHE_DIR": str(root / "handoff-cache")},
                                   rows=40, cols=100)
                handoff.expect(lambda text: "Update Available" in text,
                               "F12 handoff did not reach the startup prompt")
                handoff.write("\x1b[24~")
                handoff.expect(lambda text: "Update available" in text,
                               "F12 did not replace the startup prompt")
                handoff.write("\x1b")
                handoff.expect(lambda text: "Update available" not in text and "Update Available" not in text,
                               "Startup remained blocked after closing the F12 overlay")
                handoff.pump(0.5)
                finish(handoff)

                failed = terminal("failed-update", mirror_args, environment={
                    **update_env, "ASCII_CHAT_TEST_UPDATE_COMMAND": "exit 7",
                }, rows=40, cols=100)
                video(failed)
                failed.write("\x1b[24~")
                failed.expect(lambda text: "Update available" in text,
                              "Failure scenario did not open the update overlay")
                failed.write("\r")
                failed.expect(lambda text: "Update command failed" in text,
                              "Failed installer did not report failure in the overlay")
                assert failed.process.isalive(), "Failed installer shut down the session"
                failed.write("\x1b")
                failed.pump(0.5)
                finish(failed)

                marker = root / "updated-marker"
                update_command_env = {
                    "ASCII_CHAT_TEST_UPDATE_LATEST_VERSION": "v99.0.0",
                    "ASCII_CHAT_UPDATE_CACHE_DIR": str(root / "update-command-cache"),
                    "ASCII_CHAT_TEST_UPDATE_COMMAND": (
                        f"copy NUL {marker}" if os.name == "nt" else f"touch {marker}"
                    ),
                }
                updater = terminal("run-update", update_command, check_updates=True,
                                   environment=update_command_env, rows=40, cols=100)
                updater.expect(lambda text: "Update Available" in text,
                               "Updater scenario did not reach the update prompt")
                updater.write("\r")
                updater.expect(lambda text: "Run ascii-chat again" in text,
                               "Successful installer did not print restart instructions")
                deadline = time.monotonic() + 10
                while updater.process.isalive() and time.monotonic() < deadline:
                    updater.pump(.1)
                assert not updater.process.isalive(), "Updater did not exit after installation"
                assert marker.exists(), "Update command was not executed"
                updater.close()
                print("PASS update checker: startup prompt, weekly dismissal, installer execution and clean exit")

            explicit = terminal("explicit", mirror_args + ["--width", "60", "--height", "20"])
            small(explicit)
            explicit.resize(40, 80)
            video(explicit)
            explicit.resize(12, 19)
            small(explicit)
            finish(explicit)
            print("PASS explicit dimensions do not mask the physical terminal size")

            recording = root / "recorded.mp4"
            recorder = terminal("recording", mirror_args + ["--render-file", str(recording)])
            small(recorder)
            recorder.pump(2)
            finish(recorder)
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
