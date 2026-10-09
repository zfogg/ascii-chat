#!/usr/bin/env python3
"""Verify status keyboard input with stdout redirected and stdin/stderr on a PTY.

Uses terminal_ui's pyte + pywinpty/pexpect dependencies. No camera or audio.
"""

import argparse
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return str(sock.getsockname()[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--child", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--redirect", action="store_true")
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    if args.child:
        argv = [binary, "--no-check-update", "--log-level", "warn", "--log-file",
                str(args.output / "status.log"), "server", "--status-screen", "--port",
                unused_port(), "--websocket-port", unused_port()]
        # Only redirect the child's stdout. Its stdin and stderr inherit the PTY.
        with open(args.output / "stdout.log", "wb") as output:
            raise SystemExit(subprocess.call(argv, stdout=output if args.redirect else None))

    from terminal_ui import Terminal

    for redirected in [False, True]:
        with tempfile.TemporaryDirectory(prefix="ascii-status-input-") as directory:
            argv = [sys.executable, str(Path(__file__).resolve()), "--binary", binary,
                    "--child", "--output", directory]
            if redirected:
                argv.append("--redirect")
            terminal = Terminal(argv, rows=30, cols=100)
            try:
                terminal.expect(lambda text: "ascii-chat " in text and "|" in text,
                                "Status header did not render")
                terminal.write("/cpu_review_probe")
                terminal.expect(lambda text: "/cpu_review_probe" in text,
                                "Grep did not receive keyboard input")
                terminal.write("\x1b")
                terminal.expect(lambda text: "/cpu_review_probe" not in text,
                                "Escape did not cancel grep")
                terminal.write("/second_probe")
                terminal.expect(lambda text: "/second_probe" in text,
                                "Keyboard stopped working after Escape")
                terminal.write("\x1b")
                terminal.expect(lambda text: "/second_probe" not in text,
                                "Escape did not cancel the second search")
                terminal.interrupt()
                if redirected:
                    assert b"/cpu_review_probe" not in (Path(directory) / "stdout.log").read_bytes(), \
                        "Interactive grep leaked into redirected stdout"
                print(f"PASS server: stdout {'redirected' if redirected else 'terminal'}, grep + Escape")
            finally:
                terminal.close()


if __name__ == "__main__":
    main()
