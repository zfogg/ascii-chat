"""Exercise real MCP file rendering.

Run: python tests/integration/test_mcp_render_file.py --binary build/bin/ascii-chat.exe
Evidence is retained under build/mcp-evidence/render_file/<unique run>/.
This standalone harness does not build or require camera/audio hardware.
"""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402

import argparse
import base64
import hashlib
import json
import os
import queue
import re
import shutil
import struct
import subprocess
import threading
import time
import zlib


ROOT = Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")


def plain(text):
    return ANSI.sub("", text).replace("\r\n", "\n").strip("\n")


class MCP:
    def __init__(self, binary, directory, env):
        self.directory = directory
        self.log = (directory / "mcp.stderr.txt").open("wb")
        self.process = subprocess.Popen(
            [str(binary), "mcp-server"], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=self.log, env=env, cwd=directory,
        )
        self.responses = queue.Queue()
        self.next_id = 0
        self.transcript = []
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            try:
                self.responses.put(json.loads(line))
            except ValueError:
                self.responses.put(RuntimeError(f"Non-JSON stdout: {line!r}"))
        self.responses.put(EOFError("MCP stdout closed"))

    def send(self, method, params, notification=False):
        request = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            self.next_id += 1
            request["id"] = self.next_id
        self.transcript.append({"request": request})
        self.process.stdin.write((json.dumps(request) + "\n").encode())
        self.process.stdin.flush()
        if notification:
            return None
        deadline = time.monotonic() + 90
        while True:
            response = self.responses.get(timeout=max(0.01, deadline - time.monotonic()))
            if isinstance(response, Exception):
                raise response
            self.transcript.append({"response": response})
            if response.get("id") == request["id"]:
                assert response.get("jsonrpc") == "2.0", response
                return response

    def call(self, arguments):
        return self.send("tools/call", {"name": "render_file", "arguments": arguments})

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=10)
        self.process.stdout.close()
        self.log.close()
        (self.directory / "protocol.json").write_text(
            json.dumps(self.transcript, indent=2), encoding="utf-8")


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
    env = {k: v for k, v in os.environ.items() if not k.startswith("ASCII_CHAT_")}
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
        assert names == {"render_file", "render_url", "render_webcam", "render_visualization"}, names
        checks.append("initialize and four advertised tools")
        local = successful(client.call({"path": str(image), "options": options}))
        text = ascii_text(local)
        (evidence / "mcp-ascii.txt").write_text(text, encoding="utf-8")
        assert len(plain(text)) > 100 and len(set(plain(text))) > 3, "Missing ASCII image"
        command = [str(binary), "--no-check-update", "--log-level", "error", "mirror", "--file", str(image)]
        for key, value in options.items():
            command.append(f"--{key}={str(value).lower() if isinstance(value, bool) else value}")
        commands.append(command)
        cli = subprocess.run(command, cwd=evidence, env=env, capture_output=True, timeout=60)
        (evidence / "cli-ascii.txt").write_bytes(cli.stdout)
        (evidence / "cli.stderr.txt").write_bytes(cli.stderr)
        assert cli.returncode == 0, cli.stderr.decode(errors="replace")
        assert plain(text) == plain(cli.stdout.decode("utf-8")), "MCP and CLI ASCII differ"
        checks.append("path ASCII equals matching mirror CLI")
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/bin/ascii-chat.exe")
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.exit(77, f"SKIP: built executable does not exist: {binary}\n")
    evidence = args.evidence or ROOT / "build/mcp-evidence/render_file" / f"run-{time.time_ns()}"
    run(binary, evidence.resolve())


if __name__ == "__main__":
    main()
