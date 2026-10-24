"""Exercise MCP HTTP status, log viewer and diagnostics through PTY/ConPTY."""
import argparse
import json
import re
from pathlib import Path
import socket
import urllib.request
from terminal_ui import Terminal
from error_popup import capture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    term = Terminal([str(args.binary.resolve()), "mcp-server", "--http-server",
                     "--port", str(port), "--status-screen"], rows=40, cols=130,
                    environment={"TERM": "xterm-256color"})
    try:
        term.expect(lambda text: "MCP Server" in text, "MCP status screen")
        capture(term, args.artifacts, "status")
        for key, label, expected in (("]", "logs", "search logs"),
                                     ("`", "errors", "errno stacks"),
                                     ("0", "sync", "Sync primitives")):
            term.write(key)
            term.expect(lambda text: expected in text, f"MCP {label} screen", timeout=5)
            if label == "sync":
                term.expect(lambda text: re.search(r"Sync primitives \| [1-9][0-9]* objects", text), "MCP named synchronization objects", timeout=10)
            capture(term, args.artifacts, label)
        term.write("0")
        term.pump(.2)
        rendered = {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
            "name": "render_file", "arguments": {"options": {"test-pattern": True, "width": 40, "height": 16}}}}
        request = urllib.request.Request(f"http://127.0.0.1:{port}/mcp", data=json.dumps(rendered).encode(),
                                         headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(request, timeout=10) as response:
            assert not json.load(response)["result"]["isError"]
        term.write("=")
        term.expect(lambda text: "LIVE STATS" in text and re.search(r"mcp_renders\s+1", text), "MCP live statistics", timeout=10)
        capture(term, args.artifacts, "stats")
        term.write("\t")
        term.expect(lambda text: "Transport: HTTP" in text, "MCP statistics transport details", timeout=5)
        capture(term, args.artifacts, "stats-details")
        term.write("=")
        term.expect(lambda text: "MCP Server" in text, "MCP status after statistics", timeout=5)
        request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {
            "name": "render_file", "arguments": {"options": {"width": 99999}}}}
        req = urllib.request.Request(f"http://127.0.0.1:{port}/mcp", data=json.dumps(request).encode(),
                                     headers={"Content-Type": "application/json",
                                              "Accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(req, timeout=5) as response:
            assert json.load(response)["result"]["isError"], "Invalid request was accepted"
        term.expect(lambda text: "Error (1)" in text and ("ERROR_INVALID_PARAM" in text or "ERROR_USAGE" in text),
                    "MCP error popup", timeout=10)
        capture(term, args.artifacts, "popup")
        term.interrupt()
    finally:
        term.close()
    print("MCP status, logs, error stacks, sync primitives, stats, popup and shutdown passed")


if __name__ == "__main__":
    main()
