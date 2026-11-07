"""Contract checks for the shared Python smoke-test helpers."""
import os
from pathlib import Path
import sys
import tempfile
import wave
import unittest
from unittest.mock import patch

from ascii_chat_tests.cli import clean_environment, cli_command, run_cli, start_cli
from ascii_chat_tests.media import lavfi_fixture, ppm_image, write_pcm_wav
from ascii_chat_tests.jsonrpc import JsonRpcProcess
from ascii_chat_tests.process import free_port
from ascii_chat_tests.protocol import crc32c


class HelperTests(unittest.TestCase):
    def test_command_preserves_flag_order_and_arguments(self):
        self.assertEqual(cli_command(Path('ascii-chat'), 'mirror', '--file', 'a b.mp4'),
                         ['ascii-chat', '--no-check-update', 'mirror', '--file', 'a b.mp4'])

    def test_cli_process_options_are_forwarded(self):
        with patch('subprocess.run') as run:
            run_cli('ascii-chat', 'mirror', '--audio=false', timeout=3, capture_output=True)
        run.assert_called_once_with(['ascii-chat', '--no-check-update', 'mirror', '--audio=false'],
                                    timeout=3, capture_output=True)
        with patch('subprocess.Popen') as popen:
            start_cli('ascii-chat', 'server', cwd='workspace')
        popen.assert_called_once_with(['ascii-chat', '--no-check-update', 'server'], cwd='workspace')

    def test_environment_removes_only_ascii_chat_options(self):
        with patch.dict(os.environ, {'ASCII_CHAT_WIDTH': '80', 'TEST_HELPER_VALUE': 'kept'}, clear=True):
            self.assertEqual(clean_environment(), {'TEST_HELPER_VALUE': 'kept'})

    def test_lavfi_command_and_subprocess_options(self):
        with patch('ascii_chat_tests.media.subprocess.run') as run:
            lavfi_fixture(['testsrc=rate=10', 'sine=frequency=440'], Path('out.mkv'),
                          ffmpeg_options=['-y'], output_options=['-t', '1'], timeout=30)
        run.assert_called_once_with(['ffmpeg', '-y', '-f', 'lavfi', '-i', 'testsrc=rate=10',
                                     '-f', 'lavfi', '-i', 'sine=frequency=440', '-t', '1', 'out.mkv'],
                                    check=True, timeout=30)

    def test_media_fixtures(self):
        self.assertEqual(ppm_image(1, 1, b"\x01\x02\x03"), b"P6\n1 1\n255\n\x01\x02\x03")
        with self.assertRaises(ValueError):
            ppm_image(1, 1, b"bad pixels")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tone.wav"
            write_pcm_wav(path, b"\x01\x02" * 4, sample_rate=8000)
            with wave.open(str(path), "rb") as audio:
                self.assertEqual((audio.getnchannels(), audio.getsampwidth(), audio.getframerate()), (1, 2, 8000))
                self.assertEqual(audio.readframes(4), b"\x01\x02" * 4)

    def test_json_rpc_process_handles_notifications_and_exit(self):
        code = ("import sys,json\n"
                "for line in sys.stdin:\n"
                " m=json.loads(line)\n"
                " if 'id' in m:\n"
                "  print(json.dumps({'method':'notice'}),flush=True)\n"
                "  print(json.dumps({'jsonrpc':'2.0','id':m['id'],'result':m.get('params')}),flush=True)\n")
        with tempfile.TemporaryDirectory() as directory:
            rpc = JsonRpcProcess([sys.executable, "-u", "-c", code], Path(directory) / "stderr")
            try:
                observed = []
                rpc.request("notify", notification=True)
                reply = rpc.request("echo", {"text": "hello"}, timeout=5,
                                    on_message=lambda direction, message: observed.append((direction, message)))
                self.assertEqual(reply["result"], {"text": "hello"})
                self.assertEqual([direction for direction, _ in observed], ["request", "response", "response"])
            finally:
                rpc.close()

    def test_json_rpc_reports_bad_output_eof_and_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            rpc = JsonRpcProcess([sys.executable, "-u", "-c", "print('not json')"], Path(directory) / "stderr")
            try:
                with self.assertRaisesRegex(ValueError, "Non-JSON stdout"):
                    rpc.receive(5)
                with self.assertRaises(EOFError):
                    rpc.receive(5)
            finally:
                rpc.close()
            rpc = JsonRpcProcess([sys.executable, "-u", "-c", "import sys; [line for line in sys.stdin]"],
                                 Path(directory) / "stderr")
            try:
                with self.assertRaises(TimeoutError):
                    rpc.request("unanswered", timeout=0.1)
            finally:
                rpc.close()

    def test_protocol_checksum_and_port(self):
        self.assertEqual(crc32c(b'123456789'), 0xe3069283)
        self.assertGreater(free_port(), 0)


if __name__ == '__main__':
    unittest.main()
