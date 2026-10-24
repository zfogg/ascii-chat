"""Exercise MCP framing, cancellation, diagnostics, HTTP profiles and safe publication."""
from pathlib import Path as _PolicyPath
import sys as _policy_sys
_policy_sys.path.insert(0, str(_PolicyPath(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402

import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import tempfile
import threading
import time
import unittest

class Client:
    def __init__(self, binary, directory, extra=(), mode_extra=()):
        self.err = open(directory / "stderr.log", "wb")
        self.p = subprocess.Popen([str(binary), *extra, "mcp-server", *mode_extra],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.err)
        self.responses = queue.Queue()
        def read():
            for line in self.p.stdout:
                try: self.responses.put(json.loads(line))
                except ValueError: self.responses.put(AssertionError(f"Non-JSON stdout: {line!r}"))
        self.reader = threading.Thread(target=read, daemon=True); self.reader.start()
    def send(self, method, params=None, ident=None):
        obj = {"jsonrpc": "2.0", "method": method}
        if params is not None: obj["params"] = params
        if ident is not None: obj["id"] = ident
        self.p.stdin.write((json.dumps(obj) + "\n").encode()); self.p.stdin.flush()
    def receive(self, timeout=70):
        obj = self.responses.get(timeout=timeout)
        if isinstance(obj, Exception): raise obj
        return obj
    def close(self):
        self.p.stdin.close()
        try: self.p.wait(timeout=10)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()
        self.reader.join(timeout=2); self.p.stdout.close(); self.err.close()

class Protocol(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ascii-mcp-protocol-")
        self.directory = Path(self.tmp.name); self.addCleanup(self.tmp.cleanup)
    def client(self, extra=(), mode_extra=()):
        c = Client(BINARY, self.directory, extra, mode_extra); self.addCleanup(c.close); return c
    def test_stats_interval_preserves_stdio_protocol(self):
        c = self.client(mode_extra=("--stats-interval", "1"))
        c.send("tools/call", {"name": "render_file", "arguments": {
            "options": {"test-pattern": True, "width": 40, "height": 16}}}, 1)
        self.assertFalse(c.receive()["result"]["isError"])
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            text = (self.directory / "stderr.log").read_text(errors="replace")
            if "mcp_renders=1" in text and "mcp_requests=1" in text:
                break
            time.sleep(.05)
        else:
            self.fail("No MCP stats summary on stderr: " + text[-2000:])
        self.assertNotIn("frames_captured=", text)
        c.send("ping", {}, 2)
        self.assertEqual(c.receive()["id"], 2)

    def test_mode_help_and_http_options(self):
        top = subprocess.run([str(BINARY), "--help"], capture_output=True, text=True, timeout=10)
        self.assertEqual(top.returncode, 0)
        self.assertIn("mcp-server", top.stdout)
        mode = subprocess.run([str(BINARY), "mcp-server", "--help"], capture_output=True, text=True, timeout=10)
        self.assertEqual(mode.returncode, 0)
        for flag in ("--http-server", "--http-origin", "--http-tls-cert", "--http-tls-key", "--http-tls-key-password", "--port", "--address", "--stats-interval"):
            self.assertIn(flag, mode.stdout)
        self.assertNotIn("--websocket-tls-cert", mode.stdout)
        missing_key = subprocess.run([str(BINARY), "mcp-server", "--http-server", "--http-tls-cert", "missing.pem"],
                                     capture_output=True, text=True, timeout=10)
        self.assertNotEqual(missing_key.returncode, 0)
        self.assertIn("--http-tls-key", missing_key.stdout + missing_key.stderr)

    def test_cancel_ping_and_diagnostic_stream(self):
        c = self.client(("--sync-state", "0.2"))
        c.send("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                              "clientInfo": {"name": "protocol-test", "version": "1"}}, 1)
        self.assertEqual(c.receive()["id"], 1)
        c.send("notifications/initialized")
        c.send("tools/call", {"name": "render_file", "arguments": {
            "options": {"test-pattern": True, "snapshot-delay": 10}}}, 2)
        c.send("ping", {}, 3)
        self.assertEqual(c.receive(timeout=3)["id"], 3, "Rendering blocked protocol reader")
        c.send("notifications/cancelled", {"requestId": 2})
        result = c.receive(timeout=10)
        self.assertEqual(result["id"], 2)
        self.assertTrue(result["result"]["isError"])
        self.assertIn("cancel", result["result"]["content"][0]["text"].lower())
    def test_schema_and_invalid_parameters(self):
        c = self.client(); c.send("tools/list", {}, 1)
        tools = c.receive()["result"]["tools"]
        self.assertEqual(len(tools), 4)
        for tool in tools:
            props = tool["inputSchema"]["properties"]["options"]["properties"]
            self.assertNotIn("color", props); self.assertIn("color-mode", props)
            self.assertIn("outputSchema", tool)
        for i, arguments in enumerate(({"options": {"width": 100000}}, {"options": {"color": "true"}}, {"options": {"color-mode": "invalid"}}, {"options": {"seek": "invalid"}},
                                        {"path": "x", "data_base64": "AAAA"}, {"data_base64": "!"}), 2):
            c.send("tools/call", {"name": "render_file", "arguments": arguments}, i)
            self.assertTrue(c.receive()["result"]["isError"])
    def test_atomic_output_race(self):
        c = self.client(); output = self.directory / "winner.png"
        args = {"options": {"test-pattern": True, "snapshot-delay": .2}, "output_path": str(output)}
        for ident in (1, 2): c.send("tools/call", {"name": "render_file", "arguments": args}, ident)
        replies = [c.receive(), c.receive()]
        self.assertEqual(sum(not r["result"]["isError"] for r in replies), 1, replies)
        data = output.read_bytes(); self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        digest = hashlib.sha256(data).digest()
        c.send("tools/call", {"name": "render_file", "arguments": args}, 3)
        self.assertTrue(c.receive()["result"]["isError"])
        self.assertEqual(hashlib.sha256(output.read_bytes()).digest(), digest)
        self.assertEqual(list(self.directory.glob(".ascii-mcp-*")), [])
    def test_http_profile_and_port(self):
        with socket.socket() as sock: sock.bind(("127.0.0.1", 0)); port = sock.getsockname()[1]
        log = open(self.directory / "http.log", "wb"); self.addCleanup(log.close)
        p = subprocess.Popen([str(BINARY), "mcp-server", "--http-server", "--address", "127.0.0.1",
                              "--port", str(port), "--http-origin", "https://tools.example.test", "--status-screen=false"], stdout=log, stderr=log)
        def stop():
            p.terminate()
            try: p.wait(timeout=10)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
        self.addCleanup(stop)
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.1): break
            except OSError:
                self.assertIsNone(p.poll(), "HTTP server exited"); time.sleep(.05)
        else: self.fail("HTTP server did not bind requested port")
        def request(body, origin=None, method="POST"):
            headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
            if origin: headers["Origin"] = origin
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=70)
            conn.request(method, "/mcp", json.dumps(body) if body else None, headers)
            response = conn.getresponse(); status = response.status; data = response.read(); conn.close()
            return status, json.loads(data) if data else None
        status, response = request({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        self.assertEqual(status, 200)
        tools = {tool["name"]: tool for tool in response["result"]["tools"]}
        self.assertEqual(set(tools), {"render_file", "render_url", "render_visualization"})
        for tool, arguments in (("render_webcam", {}), ("render_visualization", {"visualization": "fft", "audio": {"type": "microphone"}})):
            _, denied = request({"jsonrpc": "2.0", "id": 20, "method": "tools/call", "params": {"name": tool, "arguments": arguments}})
            self.assertTrue("error" in denied or denied.get("result", {}).get("isError"), denied)
        self.assertEqual(request({"jsonrpc": "2.0", "id": 21, "method": "ping"}, "https://tools.example.test")[0], 200)

        for tool in tools.values(): self.assertNotIn("output_path", tool["inputSchema"]["properties"])
        self.assertNotIn("path", tools["render_file"]["inputSchema"]["properties"])
        self.assertEqual(request(None, method="GET")[0], 405)
        self.assertEqual(request({"jsonrpc": "2.0", "id": 2, "method": "ping"}, "https://evil.example")[0], 403)
        for i, args in enumerate(({"path": str(BINARY)}, {"options": {"test-pattern": True}, "output_path": str(self.directory / "bad.png")}), 3):
            _, result = request({"jsonrpc": "2.0", "id": i, "method": "tools/call", "params": {"name": "render_file", "arguments": args}})
            self.assertTrue(result["result"]["isError"])
        _, result = request({"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {
            "name": "render_file", "arguments": {"options": {"test-pattern": True, "width": 40, "height": 16}}}})
        self.assertFalse(result["result"]["isError"])
        self.assertGreater(len(result["result"]["structuredContent"]["ascii"]), 100)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--binary", type=Path, required=True)
    args, rest = parser.parse_known_args(); BINARY = args.binary.resolve()
    unittest.main(argv=[__file__, *rest], verbosity=2)
