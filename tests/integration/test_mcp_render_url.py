"""Real MCP render_url / mirror comparison using generated loopback media.

Run with --binary build/bin/ascii-chat.exe.
Evidence is retained in build/mcp-evidence/render_url (or --output).
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402

import argparse
import time
import functools
import http.server
import json
import queue
import re
import subprocess
import threading


class Rpc:
    def __init__(self, binary, evidence):
        self.log = (evidence / "server.stderr.txt").open("wb")
        self.process = subprocess.Popen([str(binary), "mcp-server"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.log)
        self.responses = queue.Queue()
        self.transcript = []
        self.counter = 0
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            self.responses.put(line)
        self.responses.put(None)

    def request(self, method, params=None, notification=False):
        self.counter += 1
        request = {"jsonrpc": "2.0", "method": method}
        if not notification:
            request["id"] = self.counter
        if params is not None:
            request["params"] = params
        self.transcript.append({"request": request})
        self.process.stdin.write((json.dumps(request) + "\n").encode())
        self.process.stdin.flush()
        if notification:
            return None
        while True:
            line = self.responses.get(timeout=90)
            assert line is not None, "MCP process exited before replying"
            response = json.loads(line)
            self.transcript.append({"response": response})
            if response.get("id") == request["id"]:
                return response

    def close(self, evidence):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.log.close()
        (evidence / "protocol.json").write_text(json.dumps(self.transcript, indent=2), encoding="utf-8")


def plain(text):
    return re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text).replace("\r", "").strip("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/mcp-evidence/render_url"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file(), binary
    evidence = args.output.resolve() / f"run-{time.time_ns()}"
    evidence.mkdir(parents=True, exist_ok=True)
    fixture = evidence / "pattern.png"
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                    "testsrc=size=160x120:rate=1", "-frames:v", "1", str(fixture)],
                   check=True, timeout=30)
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
        command = [str(binary), "--no-check-update", "mirror", "--url", url, "--snapshot",
                   "--snapshot-delay", "0", "--width", "40", "--height", "16",
                   "--color-mode", "none", "--strip-ansi=true", "--audio=false", "--splash-screen=false"]
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


if __name__ == "__main__":
    main()
