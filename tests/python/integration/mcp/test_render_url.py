import pytest
"""Real MCP render_url / mirror comparison using generated loopback media.

Run with --binary build/bin/ascii-chat.exe.
Evidence is retained in build/mcp-evidence/render_url (or --output).
"""
from ascii_chat_tests.jsonrpc import JsonRpcProcess
from ascii_chat_tests.media import lavfi_fixture
from ascii_chat_tests.cli import cli_command

import functools
import http.server
import json
import re
import subprocess
import threading


pytestmark = [pytest.mark.integration, pytest.mark.mcp]


class Rpc:
    def __init__(self, binary, evidence):
        self.transcript = []
        self.rpc = JsonRpcProcess([str(binary), "mcp-server"], evidence / "server.stderr.txt")

    def request(self, method, params=None, notification=False):
        return self.rpc.request(method, params, notification=notification,
                                on_message=lambda direction, message: self.transcript.append({direction: message}))

    def close(self, evidence):
        self.rpc.close()
        (evidence / "protocol.json").write_text(json.dumps(self.transcript, indent=2), encoding="utf-8")


def plain(text):
    return re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text).replace("\r", "").strip("\n")


def test_render_url(binary, tmp_path):
    evidence = tmp_path
    fixture = evidence / "pattern.png"
    lavfi_fixture(["testsrc=size=160x120:rate=1"], str(fixture), ffmpeg_options=["-y", "-v", "error"], output_options=["-frames:v", "1"], timeout=30)
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(evidence))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    rpc = Rpc(binary, evidence)
    try:
        response = rpc.request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                              "clientInfo": {"name": "render-url-integration", "version": "1"}})
        assert "result" in response, response
        rpc.request("notifications/initialized", notification=True)
        response = rpc.request("tools/list")
        assert "render_url" in [tool["name"] for tool in response["result"]["tools"]], response
        url = f"http://127.0.0.1:{server.server_port}/pattern.png"
        options = {"width": 40, "height": 16, "color-mode": "none", "snapshot-delay": 0}
        response = rpc.request("tools/call", {"name": "render_url", "arguments": {"url": url, "options": options}})
        assert "result" in response and not response["result"].get("isError"), response
        blocks = response["result"]["content"]
        text = "\n".join(block["text"] for block in blocks if block.get("type") == "text")
        assert text.strip(), response
        (evidence / "mcp.txt").write_text(text, encoding="utf-8")
        command = cli_command(str(binary), "mirror", "--url", url, "--snapshot",
                   "--snapshot-delay", "0", "--width", "40", "--height", "16",
                   "--color-mode", "none", "--strip-ansi=true", "--audio=false", "--splash-screen=false")
        (evidence / "cli-command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        cli = subprocess.run(command, capture_output=True, timeout=90)
        (evidence / "cli.stdout.txt").write_bytes(cli.stdout)
        (evidence / "cli.stderr.txt").write_bytes(cli.stderr)
        assert cli.returncode == 0, cli.stderr.decode(errors="replace")
        expected = plain(cli.stdout.decode("utf-8"))
        assert expected.strip() and len(set(expected) - set(" \n")) > 3, "CLI rendered no meaningful pattern"
        assert expected in plain(text), "MCP ASCII differs from matching CLI; inspect evidence"
        print(f"PASS: initialize, tools/list, render_url and CLI ASCII comparison: {evidence}")
    finally:
        rpc.close(evidence)
        server.shutdown()
        server.server_close()
