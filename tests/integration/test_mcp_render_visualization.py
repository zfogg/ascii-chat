"""Exercise MCP audio visualizations against real mirror output.

Run only after the implementation publishes build/mcp-ready.json::

    python tests/integration/test_mcp_render_visualization.py --binary build/bin/ascii-chat.exe

No hardware or public network is needed. Artifacts retain protocol transcripts,
CLI commands, decoded frames and generated exports for visual inspection.
"""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402

import argparse
import base64
import functools
import http.server
import json
import math
import queue
import re
import struct
import subprocess
import tempfile
import threading
import time
import wave


ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)")
ROOT = Path(__file__).resolve().parents[2]


class Client:
    """Newline-delimited JSON-RPC with bounded reads on Windows and Unix."""

    def __init__(self, binary, artifacts, extra=()):
        self.transcript = []
        self.serial = 0
        self.lines = queue.Queue()
        self.stderr = (artifacts / "server.stderr.txt").open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            [str(binary), "mcp-server", *extra], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=self.stderr, text=True,
            encoding="utf-8", errors="replace", cwd=ROOT,
        )
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def send(self, method, params, notification=False):
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            self.serial += 1
            message["id"] = self.serial
        self.transcript.append({"direction": "request", "message": message})
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        if notification:
            return None
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            line = self.lines.get(timeout=max(.01, deadline - time.monotonic()))
            assert line is not None, f"MCP closed stdout (exit {self.process.poll()})"
            response = json.loads(line)
            self.transcript.append({"direction": "response", "message": response})
            if response.get("id") == message["id"]:
                return response
        raise TimeoutError(method)

    def call(self, arguments):
        return self.send("tools/call", {"name": "render_visualization", "arguments": arguments})

    def close(self, artifacts):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=10)
        self.process.stdout.close()
        self.stderr.close()
        (artifacts / "protocol.json").write_text(json.dumps(self.transcript, indent=2), encoding="utf-8")


def text_result(response):
    assert "error" not in response, response
    result = response["result"]
    assert not result.get("isError", False), response
    parts = [item["text"] for item in result.get("content", []) if item.get("type") == "text"]
    assert parts, f"Missing text content: {response}"
    return "\n".join(parts)


def frame(text):
    text = ANSI.sub("", text).replace("\r", "")
    return [line for line in text.splitlines() if line.strip()]


def validate_visual(text, kind):
    rows = frame(text)
    assert rows, "Empty visualization"
    allowed = set(" #+" if kind == "waveform" else " .:-=+*#%@")
    visual = [row for row in rows if set(row) <= allowed]
    assert visual, f"No {kind} rows: {text[:1000]!r}"
    assert all(len(row) <= 64 for row in visual), "Width exceeds requested 64 columns"
    ink = sum(len(row.strip()) for row in visual)
    assert ink > 20, f"Silent/empty {kind}: {text!r}"
    if kind == "waveform":
        assert "#" in "".join(visual), f"Waveform contains only a silent baseline: {text!r}"
    else:
        assert any(glyph in "".join(visual) for glyph in ":=+*#%@"), f"FFT has no signal: {text!r}"
    return {"rows": len(visual), "max_width": max(map(len, visual)), "ink": ink,
            "alphabet": "".join(sorted(set("".join(visual))))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, default=ROOT / "build/mcp-evidence/render_visualization")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        print("SKIP: built binary required")
        return 77
    args.artifacts.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=args.artifacts.resolve()))
    tone = artifacts / "tone.wav"
    with wave.open(str(tone), "wb") as output:
        output.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", int(18000 * math.sin(2 * math.pi * 440 * i / 48000)))
                                    for i in range(48000 * 3)))
    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *unused):
            pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(artifacts)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    client = Client(binary, artifacts)
    checks = []
    options = {"width": 64, "height": 20, "color-mode": "none", "snapshot-delay": 0.2}
    try:
        initialized = client.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                                 "clientInfo": {"name": "visualization-integration", "version": "1"}})
        assert "result" in initialized, initialized
        client.send("notifications/initialized", {}, notification=True)
        listed = client.send("tools/list", {})
        tools = {tool["name"]: tool for tool in listed["result"]["tools"]}
        assert set(tools) == {"render_file", "render_url", "render_webcam", "render_visualization"}, listed
        checks.append({"case": "initialize-and-tools-list", "passed": True})
        sources = {
            "test": ({"type": "test"}, ["--test-audio"]),
            "file": ({"type": "file", "path": str(tone)}, ["--file", str(tone)]),
            "inline": ({"type": "file", "data_base64": base64.b64encode(tone.read_bytes()).decode(),
                        "mime_type": "audio/wav"}, ["--file", str(tone)]),
            "url": ({"type": "url", "url": f"http://127.0.0.1:{server.server_port}/tone.wav"},
                    ["--url", f"http://127.0.0.1:{server.server_port}/tone.wav"]),
        }
        for kind in ("waveform", "fft"):
            for source, (audio, cli_source) in sources.items():
                case = f"{kind}-{source}"
                try:
                    response = client.call({"visualization": kind, "audio": audio, "options": options})
                    rendered = text_result(response)
                    (artifacts / f"{case}-mcp.txt").write_text(rendered, encoding="utf-8")
                    mcp_stats = validate_visual(rendered, kind)
                    command = [str(binary), "--no-check-update", "mirror", f"--{kind}", *cli_source,
                               "--no-audio-playback", "--snapshot", "--snapshot-delay", "0.2",
                               "--width", "64", "--height", "20", "--color-mode", "none", "--strip-ansi=true", "--splash-screen=false"]
                    (artifacts / f"{case}-command.json").write_text(json.dumps(command), encoding="utf-8")
                    cli = subprocess.run(command, capture_output=True, timeout=45, cwd=ROOT)
                    (artifacts / f"{case}-cli.txt").write_bytes(cli.stdout)
                    (artifacts / f"{case}-cli.stderr.txt").write_bytes(cli.stderr)
                    assert cli.returncode == 0, f"CLI exited {cli.returncode}: {cli.stderr[-1000:]!r}"
                    cli_stats = validate_visual(cli.stdout.decode("utf-8", "replace"), kind)
                    # Live sample timing varies; compare geometry and valid glyphs, not exact samples.
                    assert mcp_stats["max_width"] == cli_stats["max_width"], (mcp_stats, cli_stats)
                    checks.append({"case": case, "passed": True, "mcp": mcp_stats, "cli": cli_stats})
                except Exception as error:
                    checks.append({"case": case, "passed": False, "error": repr(error)})
                print(json.dumps(checks[-1]), flush=True)
        for extension in ("png", "mp4"):
            case = "output-" + extension
            try:
                output = artifacts / ("waveform." + extension)
                arguments = {"visualization": "waveform", "audio": {"type": "test"},
                             "options": options, "output_path": str(output)}
                text_result(client.call(arguments))
                data = output.read_bytes()
                assert len(data) > 100, "Missing or empty saved media"
                assert data.startswith(b"\x89PNG\r\n\x1a\n") if extension == "png" else b"ftyp" in data[:64], "Invalid media header"
                rejected = client.call(arguments)
                assert "error" in rejected or rejected.get("result", {}).get("isError"), rejected
                assert output.read_bytes() == data, "Existing media overwritten"
                checks.append({"case": case, "passed": True, "bytes": len(data)})
            except Exception as error:
                checks.append({"case": case, "passed": False, "error": repr(error)})
            print(json.dumps(checks[-1]), flush=True)
        for arguments in ({"visualization": "invalid", "audio": {"type": "test"}},
                          {"visualization": "fft", "audio": {"type": "file", "data_base64": "!", "mime_type": "audio/wav"}}):
            response = client.call(arguments)
            assert "error" in response or response.get("result", {}).get("isError"), response
        checks.append({"case": "invalid-inputs", "passed": True})
    finally:
        client.close(artifacts)
        server.shutdown()
        server.server_close()
        (artifacts / "summary.json").write_text(json.dumps(checks, indent=2), encoding="utf-8")
        print(f"Artifacts: {artifacts}", flush=True)
    return int(any(not check["passed"] for check in checks))


if __name__ == "__main__":
    sys.exit(main())
