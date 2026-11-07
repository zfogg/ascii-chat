"""Exercise encrypted two-client video/audio sessions using synthetic inputs."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents
                               if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401
from ascii_chat_tests.cli import start_cli
from ascii_chat_tests.cli import clean_environment
from ascii_chat_tests.cli import cli_command

import argparse
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest


from ascii_chat_tests.process import free_port


class SyntheticAudioSessions(unittest.TestCase):
    def test_encrypted_two_client_audio_and_each_video_codec(self):
        for codec in ("raw", "h264", "hevc"):
            with self.subTest(codec=codec), tempfile.TemporaryDirectory(prefix="ascii-synthetic-session-") as directory:
                root = Path(directory)
                env = clean_environment()
                env.update(HOME=str(root), XDG_CONFIG_HOME=str(root), TERM="xterm-256color",
                           ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none")
                port = free_port()
                processes = []
                streams = []

                def start(name, *arguments):
                    stdout = open(root / f"{name}.stdout", "wb")
                    stderr = open(root / f"{name}.stderr", "wb")
                    streams.extend((stdout, stderr))
                    process = start_cli(BINARY, "--log-level", "debug", "--log-file", str(root / f"{name}.log"),
                         *arguments, env=env, stdout=stdout, stderr=stderr)
                    processes.append(process)
                    return process

                def diagnostics(name):
                    return "\n".join(path.read_text(errors="replace")[-6000:]
                                     for path in (root / f"{name}.stderr", root / f"{name}.log") if path.exists())

                try:
                    server = start("server", "server", "127.0.0.1", "--port", str(port), "--websocket-port",
                                   str(free_port()), "--status-screen=false", "--password", "synthetic-test-secret")
                    deadline = time.monotonic() + 15
                    while True:
                        self.assertIsNone(server.poll(), diagnostics("server"))
                        try:
                            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                                break
                        except OSError:
                            self.assertLess(time.monotonic(), deadline, "Server never listened: " + diagnostics("server"))
                            time.sleep(0.02)
                    clients = []
                    for number in range(2):
                        clients.append(start(f"client{number}", "client", "127.0.0.1", "--port", str(port),
                                             "--password", "synthetic-test-secret", "--test-pattern", "--test-audio",
                                             "--no-audio-playback", "--audio", "--video-codec", codec, "--fps", "20",
                                             "--snapshot", "--snapshot-delay", "2", "--splash-screen=false",
                                             "--width", "40", "--height", "12", "--color-mode", "none",
                                             "--palette", "custom", "--palette-chars", " .#"))
                    for number, client in enumerate(clients):
                        client.wait(timeout=25)
                        self.assertEqual(client.returncode, 0, diagnostics(f"client{number}"))
                        output = (root / f"client{number}.stdout").read_bytes()
                        output = re.sub(rb"(.)\x1b\[(\d+)b", lambda match: match[1] * (int(match[2]) + 1), output)
                        output = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", output)
                        self.assertTrue(any(len(row) == 40 and b"#" in row for row in output.splitlines()),
                                        "No received ASCII video grid: " + diagnostics(f"client{number}"))
                        log = (root / f"client{number}.log").read_text(errors="replace")
                        self.assertIn("CAPTURE_FRAME_SENT", log)
                        self.assertRegex(log, r"CLIENT AUDIO RECV #\d+: \d+ samples, RMS=0\.[0-9]*[1-9]")
                    log = (root / "server.log").read_text(errors="replace")
                    self.assertRegex(log, r"Decoded \d+ Opus frames -> \d+ samples")
                    server.send_signal(signal.SIGINT)
                    server.wait(timeout=15)
                    self.assertEqual(server.returncode, 0, diagnostics("server"))
                finally:
                    for process in reversed(processes):
                        if process.poll() is None:
                            process.kill()
                        process.wait(timeout=5)
                    for stream in streams:
                        stream.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BINARY = str(args.binary.resolve())
    unittest.main(argv=[__file__, *remaining])
