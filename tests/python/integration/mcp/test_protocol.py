"""Exercise MCP framing, cancellation, diagnostics, HTTP profiles and safe publication."""
from ascii_chat_tests.jsonrpc import JsonRpcProcess
from ascii_chat_tests.process import free_port

import hashlib
import http.client
import json
import os
import re
import shutil
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import pytest

pytestmark = [pytest.mark.integration, pytest.mark.mcp]

def available_cpu_count():
    if hasattr(os, "sched_getaffinity"):
        return len(os.sched_getaffinity(0))
    return os.cpu_count() or 1

class Client:
    def __init__(self, binary, directory, extra=(), mode_extra=()):
        self.rpc = JsonRpcProcess([str(binary), *extra, "mcp-server", *mode_extra], directory / "stderr.log")

    def send(self, method, params=None, ident=None):
        obj = {"jsonrpc": "2.0", "method": method}
        if params is not None: obj["params"] = params
        if ident is not None: obj["id"] = ident
        self.rpc.send_message(obj)

    def receive(self, timeout=70):
        return self.rpc.receive(timeout)

    def close(self):
        self.rpc.close()

class Protocol(unittest.TestCase):
    @pytest.fixture(autouse=True)
    def configure_binary(self, binary, test_environment, monkeypatch):
        self.binary = binary
        for key in list(os.environ):
            if key.startswith("ASCII_CHAT_") and key not in test_environment:
                monkeypatch.delenv(key)
        for key, value in test_environment.items():
            monkeypatch.setenv(key, value)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ascii-mcp-protocol-")
        self.directory = Path(self.tmp.name); self.addCleanup(self.tmp.cleanup)
    def client(self, extra=(), mode_extra=()):
        c = Client(self.binary, self.directory, extra, mode_extra); self.addCleanup(c.close); return c

    def test_explicit_concurrency_limit_reports_busy(self):
        c = self.client(extra=("--log-level", "info"), mode_extra=("--max-clients", "1"))
        arguments = {"options": {"test-pattern": True, "snapshot-delay": 8}}
        c.send("tools/call", {"name": "render_file", "arguments": arguments}, 1)
        c.send("tools/call", {"name": "render_file", "arguments": arguments}, 2)
        response = c.receive()
        self.assertEqual(response["id"], 2)
        self.assertEqual(response["error"]["code"], -32000)
        self.assertIn("at most 1 render may run", response["error"]["message"])
        c.send("notifications/cancelled", {"requestId": 1})
        self.assertEqual(c.receive()["id"], 1)

    def test_concurrency_configuration_precedence(self):
        config = self.directory / "concurrency.toml"
        config.write_text("[network]\nmax_clients = 3\n", encoding="utf-8")
        def capacity(extra=(), env=None):
            log_path = self.directory / "capacity.log"
            result = subprocess.run([str(self.binary), "--log-level", "info", "--log-file", str(log_path),
                                     *extra, "mcp-server"], input=b"", capture_output=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
            match = re.search(r"MCP server started .*\b(\d+) render jobs\)", log_path.read_text(errors="replace"))
            assert match is not None, "MCP server did not report its concurrency limit"
            return int(match.group(1))
        available = capacity()
        self.assertGreaterEqual(available, 1)
        self.assertEqual(capacity(("--config", str(config))), min(available, 3))
        env = dict(os.environ, ASCII_CHAT_MAX_CLIENTS="2")
        self.assertEqual(capacity(("--config", str(config)), env), min(available, 2))

    def test_more_than_four_concurrent_renders(self):
        log_path = self.directory / "concurrency.log"
        c = self.client(extra=("--log-level", "info", "--log-file", str(log_path)),
                        mode_extra=("--max-clients", "5"))
        deadline = time.monotonic() + 15
        match = None
        while time.monotonic() < deadline:
            if log_path.exists():
                match = re.search(r"MCP server started .*\b(\d+) render jobs\)", log_path.read_text(errors="replace"))
                if match:
                    break
            time.sleep(.05)
        assert match is not None, "MCP server did not report its concurrency limit"
        if int(match.group(1)) < 5:
            self.skipTest("Fewer than five CPUs available")
        arguments = {"options": {"test-pattern": True, "snapshot-delay": 8}}
        for ident in range(1, 7):
            c.send("tools/call", {"name": "render_file", "arguments": arguments}, ident)
        busy = c.receive()
        self.assertEqual(busy["id"], 6)
        self.assertEqual(busy["error"]["code"], -32000)
        self.assertIn("at most 5 renders", busy["error"]["message"])
        for ident in range(1, 6):
            c.send("notifications/cancelled", {"requestId": ident})
        self.assertEqual({c.receive()["id"] for _ in range(5)}, set(range(1, 6)))

    @unittest.skipUnless(hasattr(os, "sched_getaffinity") and shutil.which("taskset"), "Requires Linux taskset")
    def test_default_concurrency_respects_cpu_affinity(self):
        cpus = sorted(os.sched_getaffinity(0))
        for count in range(1, min(2, len(cpus)) + 1):
            affinity = ",".join(map(str, cpus[:count]))
            for extra, expected in (([], count), (["--max-clients", "1"], 1)):
                log_path = self.directory / f"affinity-{count}-{expected}.log"
                result = subprocess.run(["taskset", "-c", affinity, str(self.binary), "--log-level", "info", "--log-file", str(log_path),
                                         "mcp-server", *extra], input=b"", capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
                log = log_path.read_text(errors="replace")
                self.assertRegex(log, rf"MCP server started .*\b{expected} render jobs\)")

    def test_stabilization_option_for_both_renderers(self):
        c = self.client()
        c.send("tools/list", {}, 1)
        tools = {tool["name"]: tool for tool in c.receive()["result"]["tools"]}
        options = tools["render_file"]["inputSchema"]["properties"]["options"]["properties"]
        self.assertIn("stabilize", options)
        for ident, renderer in enumerate(("luminance", "glyph"), 2):
            c.send("tools/call", {"name": "render_file", "arguments": {"options": {
                "test-pattern": True, "snapshot": True, "snapshot-delay": 0,
                "width": 20, "height": 10, "color-mode": "none",
                "renderer": renderer, "stabilize": True,
            }}}, ident)
            response = c.receive()
            self.assertFalse(response["result"]["isError"], response)
            self.assertTrue(response["result"]["structuredContent"]["ascii"])

    def test_unicode_custom_palette(self):
        c = self.client()
        for ident, renderer in enumerate(("luminance", "glyph"), 1):
            palette = " ░▒▓█"
            c.send("tools/call", {"name": "render_file", "arguments": {"options": {
                "test-pattern": True, "snapshot": True, "snapshot-delay": 0,
                "width": 20, "height": 10, "color-mode": "none", "strip-ansi": True,
                "utf8": "true", "renderer": renderer, "palette-chars": palette,
            }}}, ident)
            result = c.receive()["result"]
            self.assertFalse(result["isError"], result)
            rendered = result["structuredContent"]["ascii"]
            self.assertTrue(set(rendered.rstrip("\n")) <= set(palette + "\n"), rendered)
            self.assertTrue(any(char in rendered for char in palette[1:]), rendered)

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
        top = subprocess.run([str(self.binary), "--help"], capture_output=True, text=True, timeout=10)
        self.assertEqual(top.returncode, 0)
        self.assertIn("mcp-server", top.stdout)
        mode = subprocess.run([str(self.binary), "mcp-server", "--help"], capture_output=True, text=True, timeout=10)
        self.assertEqual(mode.returncode, 0)
        for flag in ("--transport", "--server", "--http-origin", "--http-tls-cert", "--http-tls-key", "--http-tls-key-password", "--port", "--address", "--stats-interval"):
            self.assertIn(flag, mode.stdout)
        self.assertRegex(mode.stdout, r"--max-clients INTEGER\s+Maximum concurrent clients\. \(default: available CPUs\)")
        self.assertNotIn("--websocket-tls-cert", mode.stdout)
        missing_key = subprocess.run([str(self.binary), "mcp-server", "--transport", "http", "--http-tls-cert", "missing.pem"],
                                     capture_output=True, text=True, timeout=10)
        self.assertNotEqual(missing_key.returncode, 0)
        self.assertIn("--http-tls-key", missing_key.stdout + missing_key.stderr)
        invalid_transport = subprocess.run([str(self.binary), "mcp-server", "--transport", "udp"],
                                           capture_output=True, text=True, timeout=10)
        self.assertNotEqual(invalid_transport.returncode, 0)

    def test_configured_bind_addresses_are_validated(self):
        config = self.directory / "bind.toml"
        for key, address, valid in (("address", "127.0.0.1", True), ("address6", "::1", True),
                                    ("address", "not-an-ip.invalid", False), ("address6", "not-an-ip.invalid", False)):
            config.write_text(f"[network]\n{key} = '{address}'\n")
            result = subprocess.run([str(self.binary), "--config", str(config), "mcp-server"], input=b"", capture_output=True, timeout=15)
            self.assertEqual(result.returncode == 0, valid, (key, address, result.stderr.decode(errors="replace")))
        generated = self.directory / "generated.toml"
        created = subprocess.run([str(self.binary), "--config-create", str(generated)], capture_output=True, timeout=15)
        self.assertEqual(created.returncode, 0)
        result = subprocess.run([str(self.binary), "--config", str(generated), "mcp-server"], input=b"", capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))

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
        self.assertEqual(len(tools), 5)
        for tool in tools:
            if tool["name"] == "device_list":
                self.assertEqual(tool["inputSchema"]["properties"], {})
                continue
            props = tool["inputSchema"]["properties"]["options"]["properties"]
            self.assertNotIn("color", props); self.assertIn("color-mode", props)
            self.assertIn("outputSchema", tool)
        for i, arguments in enumerate(({"options": {"width": 100000}}, {"options": {"color": "true"}}, {"options": {"color-mode": "invalid"}}, {"options": {"seek": "invalid"}},
                                        {"path": "x", "data_base64": "AAAA"}, {"data_base64": "!"}), 2):
            c.send("tools/call", {"name": "render_file", "arguments": arguments}, i)
            self.assertTrue(c.receive()["result"]["isError"])
    @unittest.skipIf(available_cpu_count() < 2, "Requires two concurrent renders")
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
    def test_device_list_names_defaults_and_selection_validation(self):
        c = self.client()
        c.send("tools/call", {"name": "device_list", "arguments": {}}, 1)
        result = c.receive()["result"]
        devices = result["structuredContent"]
        self.assertEqual(json.loads(result["content"][0]["text"]), devices)
        for kind in ("audio", "video"):
            entries = devices[kind + "_devices"]
            self.assertIsInstance(entries, list)
            for entry in entries:
                self.assertIsInstance(entry["index"], int)
                self.assertTrue(entry["name"])
                self.assertIsInstance(entry["is_default"], bool)
            default = devices["default_" + kind + "_device"]
            if default is not None:
                self.assertTrue(any(e["index"] == default and e["is_default"] for e in entries))
        for tool in ("render_webcam", "render_visualization"):
            for arguments in ({"audio_device": -2}, {"audio_device": "mic"}, {"volume": -0.1}, {"volume": 1.1}, {"volume": "quiet"}):
                if tool == "render_visualization":
                    arguments = {"visualization": "waveform", **arguments}
                c.send("tools/call", {"name": tool, "arguments": arguments}, 2)
                self.assertTrue(c.receive()["result"]["isError"])
        c.send("tools/call", {"name": "render_webcam", "arguments": {
            "audio_device": 14, "options": {"test-pattern": True, "test-audio": True,
                                           "width": 40, "height": 16}}}, 3)
        self.assertFalse(c.receive()["result"]["isError"])

    def test_http_profile_and_port(self):
        self.http_profile(True)

    @unittest.skipIf(available_cpu_count() < 2, "Requires two concurrent renders")
    def test_http_local_devices_and_paths(self):
        self.http_profile(False)

    def http_profile(self, restricted):
        port = free_port()
        log = open(self.directory / "http.log", "wb"); self.addCleanup(log.close)
        p = subprocess.Popen([str(self.binary), "mcp-server", "--transport", "http", *(["--server"] if restricted else []), "--address", "127.0.0.1",
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
        def request(body, origin=None, method="POST", session=None, include_headers=False):
            headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
            if origin: headers["Origin"] = origin
            if session: headers["Mcp-Session-Id"] = session
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=70)
            conn.request(method, "/mcp", json.dumps(body) if body else None, headers)
            response = conn.getresponse(); status = response.status; session_id = response.getheader("Mcp-Session-Id"); data = response.read(); conn.close()
            result = (status, json.loads(data) if data else None)
            return (*result, session_id) if include_headers else result
        status, response = request({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        self.assertEqual(status, 200)
        tools = {tool["name"]: tool for tool in response["result"]["tools"]}
        if not restricted:
            self.assertEqual(set(tools), {"render_file", "render_url", "render_webcam", "render_visualization", "device_list"})
            self.assertIn("video_device", tools["render_webcam"]["inputSchema"]["properties"])
            for name in ("render_webcam", "render_visualization"):
                self.assertIn("audio_device", tools[name]["inputSchema"]["properties"])
                self.assertIn("volume", tools[name]["inputSchema"]["properties"])
            self.assertIn("path", tools["render_file"]["inputSchema"]["properties"])
            self.assertIn("output_path", tools["render_file"]["inputSchema"]["properties"])
            _, rendered = request({"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {
                "name": "render_webcam", "arguments": {"video_device": 0, "options": {
                    "test-pattern": True, "width": 40, "height": 16}}}})
            self.assertFalse(rendered["result"]["isError"], rendered)
            self.assertGreater(len(rendered["result"]["structuredContent"]["ascii"]), 100)
            initialize = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}}
            _, _, first_session = request(initialize, include_headers=True)
            _, _, second_session = request(initialize, include_headers=True)
            self.assertRegex(first_session, r"^[0-9a-f]{32}$")
            self.assertNotEqual(first_session, second_session)
            render = {"jsonrpc": "2.0", "id": 99, "method": "tools/call", "params": {
                "name": "render_file", "arguments": {"options": {"test-pattern": True, "snapshot-delay": 8}}}}
            replies = {}
            baseline = (self.directory / "http.log").read_text(errors="replace").count("MCP render started")
            workers = [threading.Thread(target=lambda key, token: replies.update({key: request(render, session=token)}),
                                        args=(key, token)) for key, token in (("first", first_session), ("second", second_session))]
            for worker in workers: worker.start()
            deadline = time.monotonic() + 10
            while (self.directory / "http.log").read_text(errors="replace").count("MCP render started") < baseline + 2:
                if time.monotonic() >= deadline: self.fail("Both HTTP renders did not start")
                time.sleep(.05)
            cancelled = {"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 99}}
            self.assertEqual(request(cancelled, session=first_session)[0], 202)
            for worker in workers: worker.join(timeout=20)
            self.assertTrue(all(not worker.is_alive() for worker in workers))
            self.assertTrue(replies["first"][1]["result"]["isError"])
            self.assertIn("cancel", replies["first"][1]["result"]["content"][0]["text"].lower())
            self.assertFalse(replies["second"][1]["result"]["isError"])
            for ident, renderer in enumerate(("luminance", "glyph"), 10):
                palette = " ░▒▓█"
                _, rendered = request({"jsonrpc": "2.0", "id": ident, "method": "tools/call", "params": {
                    "name": "render_file", "arguments": {"options": {
                        "test-pattern": True, "snapshot": True, "snapshot-delay": 0,
                        "width": 20, "height": 10, "color-mode": "none", "strip-ansi": True,
                        "utf8": "true", "renderer": renderer, "palette-chars": palette,
                    }}}})
                self.assertFalse(rendered["result"]["isError"], rendered)
                output = rendered["result"]["structuredContent"]["ascii"]
                self.assertTrue(set(output.rstrip("\n")) <= set(palette + "\n"), output)
                self.assertTrue(any(char in output for char in palette[1:]), output)
            return
        self.assertEqual(set(tools), {"render_file", "render_url", "render_visualization"})
        for tool, arguments in (("device_list", {}), ("render_webcam", {}), ("render_visualization", {"visualization": "fft", "audio": {"type": "microphone"}})):
            _, denied = request({"jsonrpc": "2.0", "id": 20, "method": "tools/call", "params": {"name": tool, "arguments": arguments}})
            self.assertTrue("error" in denied or denied.get("result", {}).get("isError"), denied)
        self.assertEqual(request({"jsonrpc": "2.0", "id": 21, "method": "ping"}, "https://tools.example.test")[0], 200)

        for tool in tools.values(): self.assertNotIn("output_path", tool["inputSchema"]["properties"])
        self.assertNotIn("path", tools["render_file"]["inputSchema"]["properties"])
        self.assertEqual(request(None, method="GET")[0], 405)
        self.assertEqual(request({"jsonrpc": "2.0", "id": 2, "method": "ping"}, "https://evil.example")[0], 403)
        for i, args in enumerate(({"path": str(self.binary)}, {"options": {"test-pattern": True}, "output_path": str(self.directory / "bad.png")}), 3):
            _, result = request({"jsonrpc": "2.0", "id": i, "method": "tools/call", "params": {"name": "render_file", "arguments": args}})
            self.assertTrue(result["result"]["isError"])
        _, result = request({"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {
            "name": "render_file", "arguments": {"options": {"test-pattern": True, "width": 40, "height": 16}}}})
        self.assertFalse(result["result"]["isError"])
        self.assertGreater(len(result["result"]["structuredContent"]["ascii"]), 100)
