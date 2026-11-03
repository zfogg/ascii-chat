"""Check CLI logging configuration without camera, audio devices, or network access."""
from pathlib import Path as _PolicyPath
import sys as _policy_sys
_policy_sys.path.insert(0, str(_PolicyPath(__file__).resolve().parents[1]))
import _environment  # noqa: E402,F401

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class LoggingContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ascii-cli-logging-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.environment = dict(os.environ, ASCII_CHAT_NO_CHECK_UPDATE="1", TERM="dumb")

    def run_binary(self, before=(), after=(), environment=None):
        return subprocess.run([str(BINARY), *before, "mirror", "--test-pattern", "--snapshot",
            "--snapshot-delay", "0", "--width", "24", "--height", "12", "--audio=false",
            "--color-mode", "none", "--strip-ansi", *after], cwd=self.directory,
            env=environment or self.environment, capture_output=True, text=True, timeout=25)

    def assert_snapshot(self, process):
        self.assertEqual(process.returncode, 0, process.stderr[-3000:])
        self.assertTrue(process.stdout.strip(), "Successful mirror produced no frame")
        self.assertNotIn(chr(27), process.stdout)

    def test_json_logging_uses_explicit_filename_and_keeps_snapshot_plain(self):
        destination = self.directory / "explicit.json"
        process = self.run_binary(("--json", "--log-file", str(destination), "--log-level", "debug"))
        self.assert_snapshot(process)
        records = [json.loads(line) for line in destination.read_text().splitlines() if line.strip()]
        self.assertTrue(records, "JSON logging produced no records")
        self.assertTrue(all(isinstance(record, dict) for record in records))
        self.assertTrue(any("message" in record.get("body", {}) for record in records))

    def test_custom_template_and_grep_preserve_frame_output(self):
        log_file = self.directory / "template.log"
        process = self.run_binary(("--log-template", "contract:%level:%message", "--log-format-console",
            "--log-file", str(log_file), "--log-level", "debug", "--grep", "/.*/i", "--color-scheme", "pastel"))
        self.assert_snapshot(process)
        self.assertIn("contract:", process.stderr)
        self.assertTrue(log_file.exists())
        self.assertNotIn("contract:", log_file.read_text())
        self.assertNotIn("contract:", process.stdout)

    def test_invalid_grep_and_unwritable_json_destination_fail_before_rendering(self):
        process = self.run_binary(("--grep", "/unterminated"))
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout.strip(), "")
        destination = self.directory / "missing" / "output.json"
        process = self.run_binary(("--json", "--log-file", str(destination)))
        self.assertNotEqual(process.returncode, 0)
        self.assertFalse(destination.exists())

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    BINARY = args.binary.resolve()
    unittest.main(argv=[__file__], verbosity=2)
