import pytest
from types import SimpleNamespace
"""Focused real-process MCP webcam checks; never builds the shared checkout.

Run after building the native binary:
  uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/mcp/test_render_webcam.py --ascii-chat-binary build/bin/ascii-chat.exe
Add --webcam-device N to opt into a physical camera probe. Synthetic results do not
establish that physical camera capture works.
"""

from ascii_chat_tests.jsonrpc import JsonRpcProcess

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


ROOT = Path(__file__).resolve().parents[4]
pytestmark = [pytest.mark.integration, pytest.mark.mcp]
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
        self.rpc = JsonRpcProcess([str(binary), "mcp-server"], evidence / "server.stderr.txt", cwd=ROOT)

    def send(self, method, params, notification=False):
        def record(direction, message):
            self.messages.append({"sent" if direction == "request" else "received": message})
        return self.rpc.request(method, params, notification=notification, on_message=record)

    def call(self, arguments):
        return self.send("tools/call", {"name": "render_webcam", "arguments": arguments})

    def close(self):
        self.rpc.close()
        (self.evidence / "protocol.json").write_text(json.dumps(self.messages, indent=2), encoding="utf-8")


class WebcamTests(unittest.TestCase):
    @pytest.fixture(autouse=True)
    def setup_rpc(self, binary, tmp_path, request):
        self.config = SimpleNamespace(binary=binary, evidence=tmp_path, device=request.config.getoption("--webcam-device"))
        self.rpc = Rpc(binary, tmp_path)
        response = self.rpc.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                               "clientInfo": {"name": "webcam-integration", "version": "1"}})
        if "error" in response or "result" not in response:
            raise AssertionError(response)
        self.rpc.send("notifications/initialized", {}, notification=True)
        yield
        self.rpc.close()

    def success(self, response):
        self.assertNotIn("error", response, response)
        self.assertFalse(response["result"].get("isError", False), response)
        return response["result"]

    def rejected(self, response):
        self.assertTrue("error" in response or response.get("result", {}).get("isError"), response)

    def test_tools_schema(self):
        result = self.success(self.rpc.send("tools/list", {}))
        tools = {tool["name"]: tool for tool in result["tools"]}
        self.assertEqual(set(tools), {"render_file", "render_url", "render_webcam", "render_visualization", "device_list"})
        self.assertIn("video_device", tools["render_webcam"]["inputSchema"]["properties"])

    def test_synthetic_matches_cli(self):
        result = self.success(self.rpc.call({"video_device": 0, "options": OPTIONS}))
        texts = [part["text"] for part in result.get("content", []) if part.get("type") == "text"]
        self.assertTrue(texts, result)
        command = [str(self.config.binary), "mirror", "--webcam-index", "0"]
        for key, value in OPTIONS.items():
            command.append(f"--{key}={0 if key == 'test-pattern' and value is True else str(value).lower()}")
        run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=90, cwd=ROOT)
        (self.config.evidence / "cli-command.json").write_text(json.dumps(command), encoding="utf-8")
        (self.config.evidence / "cli.stdout.txt").write_text(run.stdout, encoding="utf-8")
        (self.config.evidence / "cli.stderr.txt").write_text(run.stderr, encoding="utf-8")
        (self.config.evidence / "mcp-ascii.txt").write_text("\n".join(texts), encoding="utf-8")
        self.assertEqual(run.returncode, 0, run.stderr)
        expected = plain(run.stdout)
        self.assertTrue(expected.strip(), "CLI produced no frame")
        self.assertGreater(len(set(expected) - set(" \n")), 3, "Synthetic output lacks detail")
        self.assertTrue(any(expected == plain(text) for text in texts), "MCP text differs from CLI; inspect evidence")

    def test_invalid_device(self):
        for device in (-1, "camera", 0.5):
            with self.subTest(device=device):
                self.rejected(self.rpc.call({"video_device": device, "options": OPTIONS}))

    def test_saved_media_and_no_overwrite(self):
        for extension in ("png", "mp4"):
            with self.subTest(extension=extension):
                path = self.config.evidence / f"synthetic.{extension}"
                self.assertFalse(path.exists(), f"Use a fresh evidence directory: {path}")
                arguments = {"video_device": 0, "options": OPTIONS, "output_path": str(path)}
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
        if self.config.device is None:
            self.skipTest("physical webcam probe requires explicit --device N")
        options = dict(OPTIONS)
        options.pop("test-pattern")
        result = self.success(self.rpc.call({"video_device": self.config.device, "options": options}))
        self.assertTrue(result.get("content"), result)
