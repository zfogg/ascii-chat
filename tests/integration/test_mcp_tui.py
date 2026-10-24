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
    term = Terminal([str(args.binary.resolve()), "--no-check-update", "mcp-server", "--server",
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
        request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {
            "name": "render_file", "arguments": {"options": {"width": 99999}}}}
        req = urllib.request.Request(f"http://127.0.0.1:{port}/mcp", data=json.dumps(request).encode(),
                                     headers={"Content-Type": "application/json",
                                              "Accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(req, timeout=5) as response:
            assert json.load(response)["result"]["isError"], "Invalid request was accepted"
        term.expect(lambda text: "Error (1)" in text and "ERROR_INVALID_PARAM" in text,
                    "MCP error popup", timeout=10)
        capture(term, args.artifacts, "popup")
        term.interrupt()
    finally:
        term.close()
    print("MCP status, logs, error stacks, sync primitives, popup and shutdown passed")


if __name__ == "__main__":
    main()
