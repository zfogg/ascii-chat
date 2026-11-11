import pytest
import sys
"""Exercise encrypted two-client video/audio sessions using synthetic inputs."""
from ascii_chat_tests.cli import start_cli
from ascii_chat_tests.cli import clean_environment

from pathlib import Path
import re
import signal
import socket
import tempfile
import time
import unittest


from ascii_chat_tests.process import free_port


pytestmark = [pytest.mark.integration, pytest.mark.network, pytest.mark.skipif(sys.platform == "win32", reason="POSIX signal fixture")]


class SyntheticAudioSessions(unittest.TestCase):
    @pytest.fixture(autouse=True)
    def setup_fixture(self, binary):
        self.binary = str(binary)

    def test_encrypted_two_client_audio_and_each_video_codec(self):
        for codec in ("raw", "h264", "hevc"):
            with self.subTest(codec=codec), tempfile.TemporaryDirectory(prefix="ascii-synthetic-session-") as directory:
                root = Path(directory)
                env = clean_environment()
                env.update(HOME=str(root), XDG_CONFIG_HOME=str(root), TERM="xterm-256color",
                           ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none")
                port = free_port()
                websocket_port = free_port()
                while websocket_port == port:
                    websocket_port = free_port()
                processes = []
                streams = []

                def start(name, *arguments):
                    stdout = open(root / f"{name}.stdout", "wb")
                    stderr = open(root / f"{name}.stderr", "wb")
                    streams.extend((stdout, stderr))
                    process = start_cli(self.binary, "--log-level", "debug", "--log-file", str(root / f"{name}.log"),
                         *arguments, env=env, stdout=stdout, stderr=stderr)
                    processes.append(process)
                    return process

                def diagnostics(name):
                    return "\n".join(path.read_text(errors="replace")[-6000:]
                                     for path in (root / f"{name}.stderr", root / f"{name}.log") if path.exists())

                try:
                    server = start("server", "server", "127.0.0.1", "--port", str(port), "--websocket-port",
                                   str(websocket_port), "--status-screen=false", "--password", "synthetic-test-secret")
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
                                             "--snapshot", "--snapshot-delay", "5", "--splash-screen=false",
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
