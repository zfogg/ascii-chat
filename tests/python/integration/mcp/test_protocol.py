"""Exercise MCP framing, cancellation, diagnostics, HTTP profiles and safe publication."""
from ascii_chat_tests.jsonrpc import JsonRpcProcess
from ascii_chat_tests.process import free_port

import hashlib
import http.client
import json
import os
import re
import shutil
import shlex
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
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
        self.test_home = test_environment["HOME"]
        for key in list(os.environ):
            if key.startswith("ASCII_CHAT_") and key not in test_environment:
                monkeypatch.delenv(key)
        for key, value in test_environment.items():
            monkeypatch.setenv(key, value)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ascii-mcp-protocol-", dir=self.test_home)
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

    def test_restricted_stdio_does_not_initialize_downloads(self):
        invalid = self.directory / "missing" / "parent" / "downloads"
        invalid.parent.parent.write_text("not a directory")
        c = self.client(mode_extra=("--server", "--downloads-dir", str(invalid)))
        c.send("ping", {}, 1)
        self.assertEqual(c.receive()["id"], 1)

    def test_mode_help_and_http_options(self):
        top = subprocess.run([str(self.binary), "--help"], capture_output=True, text=True, timeout=10)
        self.assertEqual(top.returncode, 0)
        self.assertIn("mcp-server", top.stdout)
        mode = subprocess.run([str(self.binary), "mcp-server", "--help"], capture_output=True, text=True, timeout=10)
        self.assertEqual(mode.returncode, 0)
        for flag in ("--transport", "--server", "--http-origin", "--tls-cert", "--tls-key", "--tls-key-password", "--port", "--address", "--stats-interval"):
            self.assertIn(flag, mode.stdout)
        self.assertRegex(mode.stdout, r"--max-clients INTEGER\s+Maximum concurrent clients\. \(default: available CPUs\)")
        self.assertNotIn("--http-tls-cert", mode.stdout)
        missing_key = subprocess.run([str(self.binary), "mcp-server", "--transport", "http", "--tls-cert", "missing.pem"],
                                     capture_output=True, text=True, timeout=10)
        self.assertNotEqual(missing_key.returncode, 0)
        self.assertIn("--tls-key", missing_key.stdout + missing_key.stderr)
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

    def test_hosted_downloads_config_toml(self):
        port = free_port()
        downloads = self.directory / "toml-downloads"
        config = self.directory / "mcp.toml"
        config.write_text('[mcp]\nserver = true\ntransport = "http"\n' +
                          f'downloads_dir = {json.dumps(downloads.as_posix())}\n' +
                          'downloads_url_prefix = "https://example.test"\n' +
                          f'[network]\naddress = "127.0.0.1"\nport = {port}\nmax_clients = 1\n', encoding="utf-8")
        env = os.environ.copy()
        for key in ("ASCII_CHAT_DOWNLOADS_DIR", "ASCII_CHAT_DOWNLOADS_URL_PREFIX", "ASCII_CHAT_TRANSPORT", "ASCII_CHAT_SERVER"):
            env.pop(key, None)
        log = open(self.directory / "toml-http.log", "wb"); self.addCleanup(log.close)
        process = subprocess.Popen([str(self.binary), "--config", str(config), "mcp-server", "--status-screen=false"],
                                   stdout=log, stderr=log, env=env)
        def stop():
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        self.addCleanup(stop)
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.1): break
            except OSError:
                self.assertIsNone(process.poll(), "TOML-configured HTTP server exited"); time.sleep(.05)
        else: self.fail("TOML-configured HTTP server did not bind")
        body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": "render_file",
                "arguments": {"options": {"test-pattern": True, "width": 40, "height": 16}}}}
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=70)
        conn.request("POST", "/mcp", json.dumps(body), {"Content-Type": "application/json",
                     "Accept": "application/json, text/event-stream"})
        response = conn.getresponse(); result = json.loads(response.read()); conn.close()
        self.assertEqual(response.status, 200); self.assertFalse(result["result"]["isError"], result)
        url = result["result"]["structuredContent"]["download_url"]
        self.assertTrue(url.startswith("https://example.test/downloads/"), url)
        self.assertTrue((downloads / url.rsplit("/", 1)[-1]).is_file())

    def test_hosted_uses_xdg_data_directory_by_default(self):
        if sys.platform == "win32":
            self.skipTest("XDG data directories are a Unix convention")
        port = free_port()
        data_home = self.directory / "xdg"
        env = {**os.environ, "XDG_DATA_HOME": str(data_home)}
        log = open(self.directory / "xdg-http.log", "wb"); self.addCleanup(log.close)
        process = subprocess.Popen([str(self.binary), "mcp-server", "--transport", "http", "--server",
                                    "--address", "127.0.0.1", "--port", str(port), "--status-screen=false"],
                                   stdout=log, stderr=log, env=env)
        def stop():
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        self.addCleanup(stop)
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.1): break
            except OSError:
                self.assertIsNone(process.poll(), "HTTP server exited"); time.sleep(.05)
        else: self.fail("HTTP server did not bind")
        self.assertTrue((data_home / "ascii-chat" / "downloads").is_dir())

    def test_hosted_url_video_with_audio(self):
        ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
        if not ffmpeg or not ffprobe:
            self.skipTest("ffmpeg and ffprobe are required to generate and inspect the clip")
        source = self.directory / "source.mp4"
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=80x64:rate=10", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
                        "-t", "3", "-c:v", "mpeg4", "-c:a", "aac", "-movflags", "+faststart", "-y", str(source)],
                       check=True, timeout=30)
        class MediaHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                body = source.read_bytes()
                range_header = self.headers.get("Range")
                start = int(range_header.split("=", 1)[1].split("-", 1)[0] or 0) if range_header else 0
                self.send_response(206 if range_header else 200)
                self.send_header("Content-Type", "video/mp4")
                self.send_header("Accept-Ranges", "bytes")
                if range_header: self.send_header("Content-Range", f"bytes {start}-{len(body) - 1}/{len(body)}")
                self.send_header("Content-Length", str(len(body) - start)); self.end_headers(); self.wfile.write(body[start:])
            def log_message(self, format: str, *args: object) -> None: pass
        media = ThreadingHTTPServer(("127.0.0.1", 0), MediaHandler)
        threading.Thread(target=media.serve_forever, daemon=True).start()
        self.addCleanup(media.server_close); self.addCleanup(media.shutdown)
        port = free_port()
        log = open(self.directory / "video-http.log", "wb"); self.addCleanup(log.close)
        env = os.environ.copy()
        home_record = self.directory / "worker-home"
        if sys.platform != "win32":
            tools = self.directory / "tools"; tools.mkdir()
            extractor = tools / "yt-dlp"
            stream = f"http://127.0.0.1:{media.server_port}/source.mp4"
            metadata = json.dumps({"base": {"url": stream}, "formats": None})
            extractor.write_text("#!/bin/sh\nprintf '%s' \"$HOME\" > " + shlex.quote(str(home_record)) +
                                 "\nif [ \"$1\" = --version ]; then echo test; else printf '%s\\n' " +
                                 shlex.quote(metadata) + "; fi\n", encoding="utf-8")
            extractor.chmod(0o755)
            env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
        process = subprocess.Popen([str(self.binary), "mcp-server", "--transport", "http", "--server",
                                    "--downloads-dir", str(self.directory / "clips"), "--address", "127.0.0.1",
                                    "--port", str(port), "--status-screen=false"], stdout=log, stderr=log, env=env)
        def stop():
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        self.addCleanup(stop)
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.1): break
            except OSError:
                self.assertIsNone(process.poll(), "HTTP server exited"); time.sleep(.05)
        else: self.fail("HTTP server did not bind")
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=70)
        body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": "render_url",
                "arguments": {"url": f"http://127.0.0.1:{media.server_port}/source.mp4", "output_format": "mp4",
                              "options": {"width": 40, "height": 16, "snapshot-delay": 1.5,
                                          "fps": 10, "audio": True}}}}
        conn.request("POST", "/mcp", json.dumps(body), {"Content-Type": "application/json",
                     "Accept": "application/json, text/event-stream"})
        response = conn.getresponse(); result = json.loads(response.read()); conn.close()
        self.assertEqual(response.status, 200); self.assertFalse(result["result"]["isError"], result)
        link = result["result"]["structuredContent"]["download_url"]
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        conn.request("GET", urlparse(link).path)
        response = conn.getresponse(); clip = response.read(); conn.close()
        self.assertEqual(response.status, 200)
        output = self.directory / "rendered.mp4"; output.write_bytes(clip)
        probe = subprocess.run([ffprobe, "-v", "error", "-show_entries", "stream=codec_type", "-of", "json", str(output)],
                               check=True, capture_output=True, text=True, timeout=20)
        kinds = {stream["codec_type"] for stream in json.loads(probe.stdout)["streams"]}
        self.assertEqual(kinds, {"video", "audio"})

        body["id"] = 2
        body["params"]["arguments"] = {"url": f"http://127.0.0.1:{media.server_port}/source.mp4",
                                         "output_format": "gif", "options": {"width": 40, "height": 16, "fps": 10}}
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=70)
        conn.request("POST", "/mcp", json.dumps(body), {"Content-Type": "application/json",
                     "Accept": "application/json, text/event-stream"})
        response = conn.getresponse(); result = json.loads(response.read()); conn.close()
        self.assertEqual(response.status, 200); self.assertFalse(result["result"]["isError"], result)
        link = result["result"]["structuredContent"]["download_url"]
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        conn.request("GET", urlparse(link).path)
        response = conn.getresponse(); animation = response.read(); conn.close()
        self.assertEqual(response.status, 200)
        output = self.directory / "animated.gif"; output.write_bytes(animation)
        probe = subprocess.run([ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0",
                                "-show_entries", "stream=nb_read_frames,duration", "-of", "json", str(output)],
                               check=True, capture_output=True, text=True, timeout=20)
        video = json.loads(probe.stdout)["streams"][0]
        self.assertGreater(int(video["nb_read_frames"]), 1)
        self.assertGreater(float(video["duration"]), 0.5)
        hashes = subprocess.run([ffmpeg, "-v", "error", "-i", str(output), "-f", "framemd5", "-"],
                                check=True, capture_output=True, text=True, timeout=20)
        frames = [line.rsplit(",", 1)[-1].strip() for line in hashes.stdout.splitlines() if line and not line.startswith("#")]
        self.assertGreater(len(set(frames)), 1, "GIF frames must show motion")

        if sys.platform != "win32":
            body["id"] = 3
            body["params"]["arguments"] = {"url": f"http://127.0.0.1:{media.server_port}/extract",
                                             "options": {"width": 40, "height": 16}}
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=70)
            conn.request("POST", "/mcp", json.dumps(body), {"Content-Type": "application/json",
                         "Accept": "application/json, text/event-stream"})
            response = conn.getresponse(); result = json.loads(response.read()); conn.close()
            self.assertEqual(response.status, 200); self.assertFalse(result["result"]["isError"], result)
            worker_home = Path(home_record.read_text())
            self.assertEqual(worker_home.parent.resolve(), (self.directory / "clips").resolve())
            self.assertTrue(worker_home.name.startswith(".ascii-mcp-"))
            self.assertFalse(worker_home.exists(), "private worker home must be removed after the request")

    def http_profile(self, restricted):
        port = free_port()
        log = open(self.directory / "http.log", "wb"); self.addCleanup(log.close)
        downloads = self.directory / "downloads"
        prefix = f"http://127.0.0.1:{port}"
        hosted_options = ["--server", "--downloads-dir", os.path.relpath(downloads), "--downloads-url-prefix", prefix] if restricted else []
        p = subprocess.Popen([str(self.binary), "mcp-server", "--transport", "http", *hosted_options, "--address", "127.0.0.1",
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
        self.assertIn("output_format", tools["render_url"]["inputSchema"]["properties"])
        self.assertIn("expires_at", tools["render_url"]["outputSchema"]["properties"])
        formats = tools["render_file"]["inputSchema"]["properties"]["output_format"]["enum"]
        self.assertTrue({"png", "jpg", "jpeg", "bmp"} <= set(formats))
        self.assertTrue(set(formats) <= {"png", "jpg", "jpeg", "bmp", "webp", "gif", "mp4", "mov", "mkv",
                                         "webm", "avi", "flv", "ogv", "ogg", "3gp"})
        _, init = request({"jsonrpc": "2.0", "id": 19, "method": "initialize", "params": {}})
        self.assertIn("expire after 30 minutes", init["result"]["instructions"])
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
        self.assertFalse(result["result"]["isError"], result)
        data = result["result"]["structuredContent"]
        link = data["download_url"]
        self.assertTrue(link.startswith(prefix + "/"), link)
        expires = datetime.fromisoformat(data["expires_at"].replace("Z", "+00:00"))
        self.assertTrue(1790 <= (expires - datetime.now(timezone.utc)).total_seconds() <= 1801)
        self.assertEqual(data["saved_files"], [link])
        self.assertIn(data["expires_at"], result["result"]["content"][0]["text"])
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        conn.request("GET", urlparse(link).path)
        response = conn.getresponse(); payload = response.read()
        self.assertEqual(response.status, 200)
        self.assertTrue(payload.startswith(b"\x89PNG\r\n\x1a\n"))
        conn.close()
        # Exercise newly exposed formats through encoding, publication and HTTP delivery.
        signatures = {"jpeg": ("image/jpeg", b"\xff\xd8"), "bmp": ("image/bmp", b"BM"),
                      "webp": ("image/webp", b"RIFF"), "mov": ("video/quicktime", None),
                      "avi": ("video/x-msvideo", b"RIFF"), "flv": ("video/x-flv", b"FLV"),
                      "ogv": ("video/ogg", b"OggS"), "ogg": ("video/ogg", b"OggS"),
                      "3gp": ("video/3gpp", None)}
        for index, (extension, (mime, signature)) in enumerate(signatures.items(), 100):
            if extension not in formats:
                continue
            _, rendered = request({"jsonrpc": "2.0", "id": index, "method": "tools/call", "params": {
                "name": "render_file", "arguments": {"output_format": extension,
                    "options": {"test-pattern": True, "width": 20, "height": 10}}}})
            self.assertFalse(rendered["result"]["isError"], (extension, rendered))
            format_url = rendered["result"]["structuredContent"]["download_url"]
            self.assertTrue(format_url.endswith("." + extension), format_url)
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            conn.request("GET", urlparse(format_url).path)
            response = conn.getresponse(); contents = response.read()
            self.assertEqual(response.status, 200, extension)
            self.assertEqual(response.getheader("Content-Type"), mime, extension)
            self.assertTrue(contents, extension)
            if signature: self.assertTrue(contents.startswith(signature), extension)
            if extension == "webp": self.assertEqual(contents[8:12], b"WEBP")
            if extension in ("mov", "3gp"): self.assertEqual(contents[4:8], b"ftyp")
            conn.close()
        stored = downloads / urlparse(link).path.rsplit("/", 1)[-1]
        self.assertTrue(stored.is_file())
        stored.unlink()
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        conn.request("GET", urlparse(link).path)
        response = conn.getresponse(); response.read()
        self.assertEqual(response.status, 404)
        conn.close()
        expired = downloads / ("1000000000-" + "a" * 32 + ".png")
        expired.write_bytes(b"expired")
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        conn.request("GET", "/downloads/" + expired.name)
        response = conn.getresponse(); response.read()
        self.assertEqual(response.status, 404)
        conn.close()
        self.assertFalse(expired.exists())
        stale = downloads / ("1000000000-" + "b" * 32 + ".png")
        stale.write_bytes(b"stale")
        deadline = time.monotonic() + 5
        while stale.exists() and time.monotonic() < deadline:
            time.sleep(.05)
        self.assertFalse(stale.exists(), "periodic cleanup did not remove expired file")

        if sys.platform != "win32":
            secret = self.directory / "secret"
            secret.write_bytes(b"private")
            symlink = downloads / ("9999999999-" + "c" * 32 + ".png")
            symlink.symlink_to(secret)
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            conn.request("GET", "/downloads/" + symlink.name)
            response = conn.getresponse(); response.read()
            self.assertEqual(response.status, 404)
            conn.close()

        # A URL provided by the caller is consumed by the renderer, without granting
        # the hosted MCP process access to caller-selected local paths.
        class MediaHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                body = b"P6\n2 2\n255\n" + bytes([255, 0, 0] * 4)
                self.send_response(200); self.send_header("Content-Length", str(len(body)))
                self.send_header("Content-Type", "image/x-portable-pixmap"); self.end_headers()
                self.wfile.write(body)
            def log_message(self, format: str, *args: object) -> None: pass
        media = ThreadingHTTPServer(("127.0.0.1", 0), MediaHandler)
        thread = threading.Thread(target=media.serve_forever, daemon=True); thread.start()
        self.addCleanup(media.server_close); self.addCleanup(media.shutdown)
        _, rendered = request({"jsonrpc": "2.0", "id": 55, "method": "tools/call", "params": {
            "name": "render_url", "arguments": {"url": f"http://127.0.0.1:{media.server_port}/upload.ppm",
                                                    "options": {"width": 40, "height": 16}}}})
        self.assertFalse(rendered["result"]["isError"], rendered)
        self.assertIn("download_url", rendered["result"]["structuredContent"])
