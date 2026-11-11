#!/usr/bin/env python3
"""Exercise the public MCP proxy using only generated test media."""
import argparse
import base64
import http.client
import json
import pathlib
import subprocess
import tempfile
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


def call(base, payload):
    req = Request(base + "/mcp", data=json.dumps(payload).encode(), headers={
        "Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
    with urlopen(req, timeout=75) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="Public origin, e.g. https://mcp.example.com")
    parser.add_argument("--animated", action="store_true", help="Generate a video and check animated GIF frames (requires ffmpeg)")
    args = parser.parse_args()
    base = args.url.rstrip("/")
    result = call(base, {"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
    names = {tool["name"] for tool in result["result"]["tools"]}
    if names != {"render_file", "render_url", "render_visualization"}:
        raise RuntimeError(f"Unexpected restricted tools: {names}")
    result = call(base, {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
        "name": "render_file", "arguments": {"options": {"test-pattern": True, "width": 40, "height": 16}}}})
    if result["result"]["isError"]:
        raise RuntimeError(f"Render failed: {result}")
    link = result["result"]["structuredContent"]["download_url"]
    if not link.startswith(base + "/downloads/"):
        raise RuntimeError("Download URL is not on the configured public origin")
    with urlopen(link, timeout=15) as response:
        if response.status != 200 or response.read(8) != b"\x89PNG\r\n\x1a\n":
            raise RuntimeError("Download is not a PNG")
    if args.animated:
        with tempfile.TemporaryDirectory(prefix="mcp-smoke-") as directory:
            video = pathlib.Path(directory) / "source.mp4"
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=80x64:rate=10",
                            "-t", "2", "-c:v", "mpeg4", "-y", str(video)], check=True, timeout=30)
            result = call(base, {"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {
                "name": "render_file", "arguments": {"data_base64": base64.b64encode(video.read_bytes()).decode(),
                    "mime_type": "video/mp4", "output_format": "gif", "options": {"width": 40, "height": 16, "fps": 10}}}})
            if result["result"]["isError"]:
                raise RuntimeError(f"GIF render failed: {result}")
            link = result["result"]["structuredContent"]["download_url"]
            if not link.startswith(base + "/downloads/"):
                raise RuntimeError("GIF URL is not on the configured public origin")
            animation = pathlib.Path(directory) / "animation.gif"
            with urlopen(link, timeout=15) as response:
                animation.write_bytes(response.read())
            probe = subprocess.run(["ffmpeg", "-v", "error", "-i", str(animation), "-f", "framemd5", "-"],
                                   check=True, capture_output=True, text=True, timeout=30)
            frames = [line.rsplit(",", 1)[-1].strip() for line in probe.stdout.splitlines()
                      if line and not line.startswith("#")]
            if len(frames) < 2 or len(set(frames)) < 2:
                raise RuntimeError("GIF does not contain distinct animated frames")
    parsed = urlsplit(base)
    if parsed.scheme not in ("http", "https") or parsed.path or not parsed.hostname:
        raise ValueError("--url must be an HTTP(S) origin without a path")
    conn_type = http.client.HTTPSConnection if parsed.scheme == "https" else http.client.HTTPConnection
    conn = conn_type(parsed.hostname, parsed.port, timeout=15)
    conn.putrequest("POST", "/mcp")
    conn.putheader("Content-Length", str(24 * 1024 * 1024 + 1))
    conn.endheaders()
    response = conn.getresponse()
    if response.status != 413:
        raise RuntimeError(f"Expected 413 for oversized request, got {response.status}")
    response.read(); conn.close()
    for index, target in enumerate(("http://127.0.0.1:27228/", "http://169.254.169.254/"), 3):
        result = call(base, {"jsonrpc": "2.0", "id": index, "method": "tools/call", "params": {
            "name": "render_url", "arguments": {"url": target, "timeout_ms": 5000}}})
        if not result.get("result", {}).get("isError"):
            raise RuntimeError(f"Restricted URL unexpectedly succeeded: {target}")
    print("Restricted tools, rendering, download, request limit and local/metadata URL rejection passed." +
          (" Animated GIF passed." if args.animated else ""))


if __name__ == "__main__":
    main()
