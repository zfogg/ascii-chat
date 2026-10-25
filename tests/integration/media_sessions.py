"""Exercise file rendering and encrypted loopback sessions without capture hardware.

Run with --binary build/bin/ascii-chat. Uses only Python's standard library.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

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
import tempfile
import termios
import time
import unittest

try:
    import tomllib
except ImportError:
    tomllib = None


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

    @staticmethod
    def coverage_profiles(pattern):
        if COVERAGE_ROOT is None:
            raise AssertionError("Coverage root was not supplied for an instrumented test")
        return {path: path.stat().st_mtime_ns for path in COVERAGE_ROOT.rglob(pattern)}

    def run_snapshot(self, *args, env=None):
        result = subprocess.run(self.command(*args), env=env or self.env, capture_output=True, timeout=25)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-5000:])
        self.assertGreater(len(result.stdout), 100, "Snapshot did not contain a rendered frame")
        return result.stdout

    @unittest.skipIf(tomllib is None, "Independent TOML validation requires Python 3.11 or later")
    def test_generated_configuration_is_valid_and_controls_snapshot_dimensions(self):
        result = subprocess.run(self.command("--config-create", "-"), env=self.env,
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
        template = result.stdout.decode()
        document = tomllib.loads(template)
        self.assertIn("terminal", document)
        self.assertIn("network", document)
        # The unified template includes other modes; select the terminal settings for mirror.
        terminal = re.search(r"(?ms)^\[terminal\]\n.*?(?=^\[|\Z)", template)
        self.assertIsNotNone(terminal)
        configured = re.sub(r"^width = \d+$", "width = 37", terminal[0], flags=re.MULTILINE)
        configured = re.sub(r"^height = \d+$", "height = 18", configured, flags=re.MULTILINE)
        path = self.root / "generated.toml"
        path.write_text(configured)
        self.assertEqual(tomllib.loads(configured)["terminal"]["width"], 37)
        cases = [(["--config", str(path)], [], self.env, 37),
                 ([f"--config={path}", "--log-level", "warn"], [], dict(self.env, ASCII_CHAT_WIDTH="31"), 31),
                 (["--config", str(path)], ["--width", "29"], dict(self.env, ASCII_CHAT_WIDTH="31"), 29)]
        for config_args, cli_args, env, expected_width in cases:
            with self.subTest(width=expected_width):
                frame = self.run_snapshot(*config_args, "mirror", "--file", str(self.image),
                                          "--snapshot", "--snapshot-delay", "0", "--splash-screen=false",
                                          "--color-mode", "none", "--palette", "custom", "--palette-chars", " .#",
                                          *cli_args, env=env)
                frame = re.sub(rb"(.)\x1b\[(\d+)b", lambda match: match[1] * (min(int(match[2]), 500) + 1), frame)
                frame = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", frame)
                rows = [line for line in frame.splitlines() if b"#" in line and set(line) <= set(b" .#")]
                self.assertTrue(rows, "The generated configuration did not produce a rendered frame")
                self.assertTrue(all(len(line) == expected_width for line in rows), rows)

    def test_explicit_configuration_errors_are_fatal(self):
        broken = self.root / "broken.toml"
        broken.write_text("[terminal\nwidth = 37\n")
        for args in (["--config"], ["--config="], ["--config", str(self.root / "absent.toml")],
                     [f"--config={broken}"]):
            with self.subTest(args=args):
                result = subprocess.run(self.command(*args), env=self.env, capture_output=True, timeout=15)
                self.assertNotEqual(result.returncode, 0)

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
        before = self.coverage_profiles("ascii-chat.dir/main.c.gcda")
        result = subprocess.run(self.command("mirror", "--file", str(self.image), "--snapshot",
                                             "--width", "40", "--height", "20", "--splash-screen=false"),
                                env=self.env, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
        self.assertNotIn(b"profiling:", result.stderr)
        profiles = self.coverage_profiles("ascii-chat.dir/main.c.gcda")
        self.assertTrue(profiles, "The CLI entry point did not write a gcov profile into the build tree")
        self.assertTrue(any(path not in before or timestamp > before[path] for path, timestamp in profiles.items()),
                        "The CLI entry point did not update a gcov profile consumed by report generation")
        self.assertTrue(all(path.stat().st_size > 20 for path in profiles), profiles)

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
                    plain = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", expanded)
                    if marker in plain:
                        return bytes(output)
            log = (self.root / "session.log").read_text(errors="replace")
            self.fail(f"Missing {marker!r} in terminal output: {bytes(output[-3000:])!r}\nSession log:\n{log[-8000:]}")

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
            expect(b"Esc: close")
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
            self.assertEqual(process.wait(timeout=2), 0,
                             (self.root / "session.log").read_text(errors="replace")[-8000:])
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)

    def test_early_actions_write_coverage(self):
        if not COVERAGE_BUILD:
            self.skipTest("Requires gcov instrumentation")
        for name, arguments, marker in (
                ("help", ["--help"], b"--color-mode"),
                ("version", ["--version"], b"ascii-chat"),
                ("completion", ["--completions", "bash"], b"complete -F")):
            with self.subTest(action=name):
                before = self.coverage_profiles("actions.c.gcda")
                result = subprocess.run(self.command(*arguments), env=self.env,
                                        capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-3000:])
                self.assertIn(marker, result.stdout)
                self.assertNotIn(b"profiling:", result.stderr)
                profiles = self.coverage_profiles("actions.c.gcda")
                self.assertTrue(profiles, f"{name} exited without saving action coverage into the build tree")
                self.assertTrue(any(path not in before or timestamp > before[path] for path, timestamp in profiles.items()),
                                f"{name} exited without updating action coverage consumed by report generation")
                self.assertTrue(all(path.stat().st_size > 20 for path in profiles), profiles)

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
    parser.add_argument("--coverage-root", type=Path)
    args, remaining = parser.parse_known_args()
    BINARY = str(args.binary.resolve())
    COVERAGE_BUILD = args.coverage_build or os.environ.get("ASCIICHAT_COVERAGE_BUILD") == "1"
    coverage_root = args.coverage_root or os.environ.get("ASCIICHAT_COVERAGE_ROOT")
    COVERAGE_ROOT = Path(coverage_root).resolve() if coverage_root else None
    unittest.main(argv=[__file__, *remaining])
