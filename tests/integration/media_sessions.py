"""Exercise file rendering and encrypted loopback sessions without capture hardware.

Run with --binary build/bin/ascii-chat. Uses only Python's standard library.
"""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import tomllib
import tempfile
import termios
import time
import unittest


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class MediaSessions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="ascii-media-session-")
        cls.root = Path(cls.directory.name)
        cls.image = cls.root / "pattern.ppm"
        # Three vertical intensity bands give observable rendering structure.
        cls.image.write_bytes(b"P6\n96 48\n255\n" + bytes(
            channel for _y in range(48) for x in range(96)
            for channel in ((0, 0, 0) if x < 32 else (128, 128, 128) if x < 64 else (255, 255, 255))))
        cls.env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
        cls.env.update(HOME=str(cls.root), XDG_CONFIG_HOME=str(cls.root), TERM="xterm-256color")

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def command(self, *args):
        return [BINARY, "--no-check-update", "--log-file", str(self.root / "session.log"), *args]

    def run_snapshot(self, *args):
        result = subprocess.run(self.command(*args), env=self.env, capture_output=True, timeout=25)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-5000:])
        self.assertGreater(len(result.stdout), 100, "Snapshot did not contain a rendered frame")
        return result.stdout

    def test_generated_configuration_is_valid_and_controls_snapshot_dimensions(self):
        result = subprocess.run(self.command("--config-create", "-"), env=self.env,
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
        template = result.stdout.decode()
        document = tomllib.loads(template)
        self.assertIn("terminal", document)
        self.assertIn("network", document)
        configured = re.sub(r"^width = \d+$", "width = 37", template, flags=re.MULTILINE)
        configured = re.sub(r"^height = \d+$", "height = 18", configured, flags=re.MULTILINE)
        path = self.root / "generated.toml"
        path.write_text(configured)
        self.assertEqual(tomllib.loads(configured)["terminal"]["width"], 37)
        frame = self.run_snapshot("--config", str(path), "mirror", "--file", str(self.image),
                                  "--snapshot", "--snapshot-delay", "0", "--splash-screen=false",
                                  "--color-mode", "none", "--palette", "custom", "--palette-chars", " .#")
        rows = [line for line in frame.splitlines() if b"#" in line and set(line) <= set(b" .#")]
        self.assertTrue(rows, "The generated configuration did not produce a rendered frame")
        self.assertTrue(all(len(line) == 37 for line in rows), rows)

    def test_generated_shell_completions_and_manual_preserve_public_options(self):
        markers = {"bash": b"complete -F", "fish": b"complete -c ascii-chat",
                   "zsh": b"compdef", "powershell": b"Register-ArgumentCompleter"}
        for shell, marker in markers.items():
            with self.subTest(shell=shell):
                result = subprocess.run(self.command("--completions", shell), env=self.env,
                                        capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
                self.assertIn(marker, result.stdout)
                for option in (b"color-mode", b"password", b"snapshot", b"discovery-service"):
                    self.assertIn(option, result.stdout)
                path = self.root / f"completion.{shell}"
                written = subprocess.run(self.command("--completions", shell, str(path)), env=self.env,
                                         capture_output=True, timeout=20)
                self.assertEqual(written.returncode, 0, written.stderr.decode(errors="replace")[-3000:])
                self.assertEqual(path.read_bytes(), result.stdout)
                if shell == "bash":
                    syntax = subprocess.run(["bash", "-n", str(path)], capture_output=True, timeout=10)
                    self.assertEqual(syntax.returncode, 0, syntax.stderr.decode(errors="replace"))
        manual = self.root / "ascii-chat.1"
        result = subprocess.run(self.command("--man-page-create", str(manual)), env=self.env,
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
        text = manual.read_text().replace(r"\-", "-")
        for required in (".TH ascii-chat 1", ".SH SYNOPSIS", "--color-mode", "--password", "--snapshot"):
            self.assertIn(required, text)

    def test_mirror_palette_and_color_encodings(self):
        for mode in ("none", "16", "256", "truecolor"):
            with self.subTest(mode=mode):
                frame = self.run_snapshot("mirror", "--file", str(self.image), "--snapshot",
                                          "--snapshot-delay", "0", "--splash-screen=false",
                                          "--width", "60", "--height", "24", "--color-mode", mode,
                                          "--palette", "custom", "--palette-chars", " .#")
                self.assertIn(b"#", frame)
                if mode == "none":
                    self.assertNotIn(b"\x1b[38;", frame)
                elif mode == "256":
                    self.assertIn(b"38;5;", frame)
                elif mode == "truecolor":
                    self.assertIn(b"38;2;", frame)

    def test_missing_media_fails_without_rendering_a_frame(self):
        result = subprocess.run(self.command("mirror", "--file", str(self.root / "missing.mkv"),
                                             "--snapshot", "--splash-screen=false"),
                                env=self.env, capture_output=True, timeout=15)
        self.assertNotEqual(result.returncode, 0)

    def test_coverage_runtime_writes_the_cli_profile(self):
        if not COVERAGE_BUILD:
            self.skipTest("Requires gcov instrumentation")
        prefix = self.root / "isolated-profile"
        env = dict(self.env, GCOV_PREFIX=str(prefix))
        result = subprocess.run(self.command("mirror", "--file", str(self.image), "--snapshot",
                                             "--width", "40", "--height", "20", "--splash-screen=false"),
                                env=env, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
        self.assertNotIn(b"profiling:", result.stderr)
        profiles = list(prefix.rglob("ascii-chat.dir/main.c.gcda"))
        self.assertEqual(len(profiles), 1, "The CLI entry point did not write a gcov profile")
        self.assertGreater(profiles[0].stat().st_size, 20)

    def test_interactive_mirror_recovers_after_overlays_and_resize(self):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 45, 140, 0, 0))
        process = subprocess.Popen(self.command(
            "mirror", "--file", str(self.image), "--loop", "--audio=false", "--splash-screen=false",
            "--palette", "custom", "--palette-chars", "QQ", "--color-mode", "none"),
            env=self.env, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
        os.close(slave)

        def expect(marker, timeout=15):
            output = bytearray()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if select.select([master], [], [], 0.1)[0]:
                    try:
                        output.extend(os.read(master, 65536))
                    except OSError:
                        break
                    # The renderer may encode repeated cells with ECMA-48 REP.
                    expanded = re.sub(rb"(.)\x1b\[(\d+)b",
                                      lambda m: m[1] * (min(int(m[2]), 500) + 1), bytes(output))
                    if marker in expanded:
                        return bytes(output)
            self.fail(f"Missing {marker!r} in terminal output: {bytes(output[-3000:])!r}")

        try:
            expect(b"QQQQQQQQ")
            os.write(master, b"?")
            expect(b"Keyboard Shortcuts")
            os.write(master, b"\x1b[B")
            expect(b"Volume")
            os.write(master, b"?")
            expect(b"QQQQQQQQ")
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 19, 0, 0))
            process.send_signal(signal.SIGWINCH)
            expect(b"Terminal too small")
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 45, 140, 0, 0))
            process.send_signal(signal.SIGWINCH)
            expect(b"QQQQQQQQ")
            os.write(master, b"o")
            expect(b"Open media:")
            os.write(master, b"\x1b")
            expect(b"QQQQQQQQ")
            os.write(master, b"]")
            expect(b"] / Esc: close")
            os.write(master, b"]")
            expect(b"QQQQQQQQ")
            process.send_signal(signal.SIGINT)
            # Drain the PTY while shutdown joins the UI worker.
            deadline = time.monotonic() + 30
            while process.poll() is None and time.monotonic() < deadline:
                if select.select([master], [], [], 0.1)[0]:
                    try:
                        os.read(master, 65536)
                    except OSError:
                        break
            self.assertEqual(process.wait(timeout=2), 0)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)

    def test_loopback_encrypted_password_and_plain_sessions(self):
        for authentication in ([], ["--password", "loopback-test-secret"], ["--encrypt", "false"]):
            with self.subTest(authentication=authentication):
                port, websocket_port = free_port(), free_port()
                with open(self.root / "server.stderr", "wb") as errors:
                    server = subprocess.Popen(self.command(
                        "server", "127.0.0.1", "--port", str(port),
                        "--websocket-port", str(websocket_port), "--status-screen=false",
                        *authentication), env=self.env,
                        stdout=subprocess.DEVNULL, stderr=errors)
                    try:
                        deadline = time.monotonic() + 15
                        while True:
                            self.assertIsNone(server.poll(), "Server exited before accepting clients: " +
                                              (self.root / "server.stderr").read_text(errors="replace")[-4000:])
                            try:
                                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                                    break
                            except OSError:
                                if time.monotonic() >= deadline:
                                    self.fail("Server did not start listening")
                                time.sleep(0.05)
                        self.run_snapshot("client", "127.0.0.1", "--port", str(port),
                                          "--file", str(self.image), "--snapshot", "--snapshot-delay", "1",
                                          "--splash-screen=false", "--audio=false", "--width", "60",
                                          "--height", "24", "--color-mode", "none", *authentication)
                    finally:
                        if server.poll() is None:
                            server.send_signal(signal.SIGINT)
                        try:
                            server.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            server.kill()
                            server.wait()
                            self.fail("Server did not shut down after its client disconnected")
                    self.assertEqual(server.returncode, 0,
                                     (self.root / "server.stderr").read_text(errors="replace")[-5000:])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--coverage-build", action="store_true")
    args, remaining = parser.parse_known_args()
    BINARY = str(args.binary.resolve())
    COVERAGE_BUILD = args.coverage_build
    unittest.main(argv=[__file__, *remaining])
