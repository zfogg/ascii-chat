"""Focused real-process MCP webcam checks; never builds the shared checkout.

Run after building the native binary:
  python tests/integration/test_mcp_render_webcam.py --binary build/bin/ascii-chat.exe
Add --device N to opt into a physical camera probe. Synthetic results do not
establish that physical camera capture works.
"""

from pathlib import Path as _PolicyPath
import sys as _policy_sys
_policy_sys.path.insert(0, str(_PolicyPath(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402

import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import threading
import unittest
import time


ROOT = Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")
OPTIONS = {"test-pattern": True, "audio": False, "width": 48, "height": 16,
           "color-mode": "none", "strip-ansi": True, "snapshot": True,
           "snapshot-delay": 0}


def plain(text):
    return ANSI.sub("", text).replace("\r", "").strip("\n")


class Rpc:
    def __init__(self, binary, evidence):
        self.evidence = evidence
        self.messages = []
        self.sequence = 0
        self.lines = queue.Queue()
        self.stderr = (evidence / "server.stderr.txt").open("w", encoding="utf-8")
        self.process = subprocess.Popen([str(binary), "mcp-server"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.stderr,
                                        text=True, encoding="utf-8", cwd=ROOT)
        def reader():
            for line in self.process.stdout:
                self.lines.put(line)
            self.lines.put(None)
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()

    def send(self, method, params, notification=False):
        self.sequence += 1
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            message["id"] = self.sequence
        self.messages.append({"sent": message})
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        if notification:
            return None
        while True:
            line = self.lines.get(timeout=90)
            if line is None:
                raise AssertionError("MCP exited before replying; inspect server.stderr.txt")
            response = json.loads(line)
            self.messages.append({"received": response})
            if response.get("id") == message["id"]:
                return response

    def call(self, arguments):
        return self.send("tools/call", {"name": "render_webcam", "arguments": arguments})

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=10)
        self.reader.join(timeout=2)
        self.process.stdout.close()
        self.stderr.close()
        (self.evidence / "protocol.json").write_text(json.dumps(self.messages, indent=2), encoding="utf-8")


class WebcamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CONFIG.binary.is_file():
            raise unittest.SkipTest("built binary absent")
        CONFIG.evidence.mkdir(parents=True, exist_ok=True)
        cls.rpc = Rpc(CONFIG.binary, CONFIG.evidence)
        cls.addClassCleanup(cls.rpc.close)
        response = cls.rpc.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                               "clientInfo": {"name": "webcam-integration", "version": "1"}})
        if "error" in response or "result" not in response:
            raise AssertionError(response)
        cls.rpc.send("notifications/initialized", {}, notification=True)

    def success(self, response):
        self.assertNotIn("error", response, response)
        self.assertFalse(response["result"].get("isError", False), response)
        return response["result"]

    def rejected(self, response):
        self.assertTrue("error" in response or response.get("result", {}).get("isError"), response)

    def test_tools_schema(self):
        result = self.success(self.rpc.send("tools/list", {}))
        tools = {tool["name"]: tool for tool in result["tools"]}
        self.assertEqual(set(tools), {"render_file", "render_url", "render_webcam", "render_visualization"})
        self.assertIn("device", tools["render_webcam"]["inputSchema"]["properties"])

    def test_synthetic_matches_cli(self):
        result = self.success(self.rpc.call({"device": 0, "options": OPTIONS}))
        texts = [part["text"] for part in result.get("content", []) if part.get("type") == "text"]
        self.assertTrue(texts, result)
        command = [str(CONFIG.binary), "mirror", "--webcam-index", "0"]
        for key, value in OPTIONS.items():
            command.append(f"--{key}={0 if key == 'test-pattern' and value is True else str(value).lower()}")
        run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=90, cwd=ROOT)
        (CONFIG.evidence / "cli-command.json").write_text(json.dumps(command), encoding="utf-8")
        (CONFIG.evidence / "cli.stdout.txt").write_text(run.stdout, encoding="utf-8")
        (CONFIG.evidence / "cli.stderr.txt").write_text(run.stderr, encoding="utf-8")
        (CONFIG.evidence / "mcp-ascii.txt").write_text("\n".join(texts), encoding="utf-8")
        self.assertEqual(run.returncode, 0, run.stderr)
        expected = plain(run.stdout)
        self.assertTrue(expected.strip(), "CLI produced no frame")
        self.assertGreater(len(set(expected) - set(" \n")), 3, "Synthetic output lacks detail")
        self.assertTrue(any(expected == plain(text) for text in texts), "MCP text differs from CLI; inspect evidence")

    def test_invalid_device(self):
        for device in (-1, "camera", 0.5):
            with self.subTest(device=device):
                self.rejected(self.rpc.call({"device": device, "options": OPTIONS}))

    def test_saved_media_and_no_overwrite(self):
        for extension in ("png", "mp4"):
            with self.subTest(extension=extension):
                path = CONFIG.evidence / f"synthetic.{extension}"
                self.assertFalse(path.exists(), f"Use a fresh evidence directory: {path}")
                arguments = {"device": 0, "options": OPTIONS, "output_path": str(path)}
                self.success(self.rpc.call(arguments))
                self.assertTrue(path.is_file(), path)
                data = path.read_bytes()
                self.assertGreater(len(data), 100)
                if extension == "png":
                    self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
                else:
                    self.assertEqual(data[4:8], b"ftyp")
                digest = hashlib.sha256(data).hexdigest()
                self.rejected(self.rpc.call(arguments))
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)

    def test_physical_camera(self):
        if CONFIG.device is None:
            self.skipTest("physical webcam probe requires explicit --device N")
        options = dict(OPTIONS)
        options.pop("test-pattern")
        result = self.success(self.rpc.call({"device": CONFIG.device, "options": options}))
        self.assertTrue(result.get("content"), result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/bin/ascii-chat.exe")
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/mcp-evidence/render_webcam")
    parser.add_argument("--device", type=int)
    CONFIG, remaining = parser.parse_known_args()
    CONFIG.binary = CONFIG.binary.resolve()
    CONFIG.evidence = CONFIG.evidence.resolve() / f"run-{time.time_ns()}"
    unittest.main(argv=[__file__, *remaining], verbosity=2)
