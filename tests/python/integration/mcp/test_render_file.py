import pytest
"""Exercise real MCP file rendering.

Run: uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/mcp/test_render_file.py --ascii-chat-binary build/bin/ascii-chat.exe
Evidence is retained under build/mcp-evidence/render_file/<unique run>/.
This standalone harness does not build or require camera/audio hardware.
"""

from pathlib import Path

from ascii_chat_tests.jsonrpc import JsonRpcProcess
from ascii_chat_tests.cli import clean_environment
from ascii_chat_tests.cli import cli_command

import base64
import hashlib
import json
import re
import shutil
import struct
import subprocess
import zlib


ROOT = Path(__file__).resolve().parents[4]
pytestmark = [pytest.mark.integration, pytest.mark.mcp]
ANSI = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")


def plain(text):
    return ANSI.sub("", text).replace("\r\n", "\n").strip("\n")


class MCP:
    def __init__(self, binary, directory, env):
        self.directory = directory
        self.transcript = []
        self.rpc = JsonRpcProcess([str(binary), "mcp-server"], directory / "mcp.stderr.txt", cwd=directory, env=env, count_notifications=False)

    def send(self, method, params, notification=False):
        response = self.rpc.request(method, params, notification=notification, timeout=90,
                                    on_message=lambda direction, message: self.transcript.append({direction: message}))
        if response is not None:
            assert response.get("jsonrpc") == "2.0", response
        return response

    def call(self, arguments):
        return self.send("tools/call", {"name": "render_file", "arguments": arguments})

    def close(self):
        self.rpc.close()
        (self.directory / "protocol.json").write_text(json.dumps(self.transcript, indent=2), encoding="utf-8")


def successful(response):
    assert "error" not in response, response
    result = response["result"]
    assert not result.get("isError", False), result
    return result


def rejected(response):
    assert "error" in response or response.get("result", {}).get("isError"), response


def ascii_text(result):
    return "\n".join(c["text"] for c in result.get("content", []) if c.get("type") == "text")


def run(binary, evidence):
    evidence.mkdir(parents=True, exist_ok=False)
    config = evidence / "config"
    config.mkdir()
    env = clean_environment()
    env.update(TESTING="1", ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none", CLAUDECODE="1",
               XDG_CONFIG_HOME=str(config), APPDATA=str(config))
    # An asymmetric gradient makes empty frames, orientation, and source drift visible.
    image = evidence / "asymmetric.png"
    pixels = bytes(v for y in range(48) for x in range(96)
                   for v in (x * 255 // 95, y * 255 // 47, 255 if x < 24 and y < 24 else 0))
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))
    scanlines = b"".join(b"\0" + pixels[y * 288:(y + 1) * 288] for y in range(48))
    image.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", 96, 48, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))
    options = {"width": 40, "height": 16, "color-mode": "none", "strip-ansi": True,
               "audio": False, "snapshot": True, "snapshot-delay": 0,
               "render-mode": "foreground", "palette": "standard"}
    commands = []
    checks = []
    client = MCP(binary, evidence, env)
    try:
        init = successful(client.send("initialize", {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "clientInfo": {"name": "render-file-integration", "version": "1"}}))
        assert init.get("serverInfo"), init
        client.send("notifications/initialized", {}, notification=True)
        listing = successful(client.send("tools/list", {}))
        names = {tool["name"] for tool in listing["tools"]}
        assert names == {"render_file", "render_url", "render_webcam", "render_visualization", "device_list"}, names
        file_tool = next(tool for tool in listing["tools"] if tool["name"] == "render_file")
        fields = file_tool["inputSchema"]["properties"]["options"]["properties"]
        assert "glyph-enhance" not in fields
        checks.append("initialize and five advertised tools")
        local = successful(client.call({"path": str(image), "options": options}))
        text = ascii_text(local)
        (evidence / "mcp-ascii.txt").write_text(text, encoding="utf-8")
        assert len(plain(text)) > 100 and len(set(plain(text))) > 3, "Missing ASCII image"
        command = cli_command(str(binary), "--log-level", "error", "mirror", "--file", str(image))
        for key, value in options.items():
            command.append(f"--{key}={str(value).lower() if isinstance(value, bool) else value}")
        commands.append(command)
        cli = subprocess.run(command, cwd=evidence, env=env, capture_output=True, timeout=60)
        (evidence / "cli-ascii.txt").write_bytes(cli.stdout)
        (evidence / "cli.stderr.txt").write_bytes(cli.stderr)
        assert cli.returncode == 0, cli.stderr.decode(errors="replace")
        assert plain(text) == plain(cli.stdout.decode("utf-8")), "MCP and CLI ASCII differ"
        checks.append("path ASCII equals matching mirror CLI")
        glyph_options = {**options, "renderer": "glyph"}
        glyph = successful(client.call({"path": str(image), "options": glyph_options}))
        glyph_command = cli_command(str(binary), "--log-level", "error", "mirror", "--file", str(image))
        for key, value in glyph_options.items():
            glyph_command.append(f"--{key}={str(value).lower() if isinstance(value, bool) else value}")
        glyph_cli = subprocess.run(glyph_command, cwd=evidence, env=env, capture_output=True, timeout=60, check=True)
        assert plain(ascii_text(glyph)) == plain(glyph_cli.stdout.decode("utf-8")), "glyph MCP and CLI differ"
        checks.append("glyph output equals matching mirror CLI")
        encoded = successful(client.call({"data_base64": base64.b64encode(image.read_bytes()).decode(),
                                          "mime_type": "image/png", "options": options}))
        assert plain(ascii_text(encoded)) == plain(text), "base64 and path differ"
        checks.append("base64 ASCII equals path ASCII")
        rejected(client.call({"path": str(evidence / "missing.png"), "options": options}))
        rejected(client.call({"data_base64": "%%%invalid%%%", "mime_type": "image/png", "options": options}))
        checks.append("missing file and malformed base64 rejected")
        for suffix in ("png", "mp4"):
            output = evidence / f"rendered.{suffix}"
            successful(client.call({"path": str(image), "options": options, "output_path": str(output)}))
            assert output.is_file() and output.stat().st_size > 32, f"Missing {suffix} output"
            before = hashlib.sha256(output.read_bytes()).hexdigest()
            rejected(client.call({"path": str(image), "options": options, "output_path": str(output)}))
            assert hashlib.sha256(output.read_bytes()).hexdigest() == before, "Existing output changed"
            probe = shutil.which("ffprobe")
            if probe:
                metadata = subprocess.run([probe, "-v", "error", "-show_streams", "-of", "json", str(output)],
                                          capture_output=True, timeout=20, check=True)
                (evidence / f"{suffix}-probe.json").write_bytes(metadata.stdout)
                assert any(s["codec_type"] == "video" for s in json.loads(metadata.stdout)["streams"])
            checks.append(f"{suffix} output exists, decodes if ffprobe available, and cannot overwrite")
    finally:
        client.close()
        (evidence / "summary.json").write_text(json.dumps({"checks_completed": checks,
            "cli_commands": commands, "binary": str(binary),
            "limitations": ["Hosted restrictions require hosted transport; stdio only.",
                            "Visual review of saved ASCII and rendered media remains manual."]}, indent=2), encoding="utf-8")
    print(json.dumps({"passed": checks, "evidence": str(evidence)}, indent=2))


def test_render_file(binary, tmp_path):
    run(binary, tmp_path / "evidence")
