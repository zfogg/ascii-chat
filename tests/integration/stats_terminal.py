"""Stats smoke coverage through native PTYs, including Windows ConPTY.
Run with the same dependencies as terminal_ui.py.
"""

import argparse
import os
from pathlib import Path
import time
from terminal_ui import Terminal, free_port


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    binary = str(args.binary.resolve())
    config = root / "config"
    (config / "ascii-chat").mkdir(parents=True, exist_ok=True)
    os.environ["APPDATA"] = str(config)
    os.environ["HOME"] = str(config)
    os.environ.pop("CLAUDECODE", None)
    os.environ["ASCII_CHAT_QUESTION_PROMPT_RESPONSE"] = "y;y;y;y;y"
    os.environ["TERM"] = "xterm-256color"
    # Ephemeral loopback test servers must not update the user's host-key file.
    os.environ["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
    panes = []

    def launch(name, argv):
        p = Terminal(
            [
                binary,
                "--no-check-update",
                "--log-file",
                str(root / (name + ".log")),
                *argv,
            ],
            46,
            110,
        )
        panes.append(p)
        return p

    def capture(p, name):
        (root / (name + ".txt")).write_text(p.pump(1), encoding="utf8")
        (root / (name + ".ansi")).write_text("".join(p.raw), encoding="utf8")

    def overlay(p, name):
        p.write("=")
        p.expect(lambda s: "LIVE STATS" in s, name + " stats")
        capture(p, name)
        p.resize(12, 60)
        p.expect(lambda s: "Terminal too small" in s, name + " measured minimum")
        p.resize(46, 110)
        p.expect(lambda s: "LIVE STATS" in s, name + " resize")
        p.write("\x1b")
        p.expect(lambda s: "LIVE STATS" not in s, name + " close")
        p.write("=")
        p.expect(lambda s: "LIVE STATS" in s, name + " reopen")
        print("PASS:", name, "toggle, resize, close, reopen", flush=True)

    try:
        mirror = launch(
            "mirror",
            ["mirror", "--test-pattern", "--audio=false", "--splash-screen=false"],
        )
        mirror.pump(5)
        overlay(mirror, "mirror")
        sp, wp = free_port(), free_port()
        server = launch(
            "server",
            [
                "server",
                "127.0.0.1",
                "--port",
                str(sp),
                "--websocket-port",
                str(wp),
                "--status-screen=false",
                "--stats-interval",
                "1",
            ],
        )
        server.expect(lambda s: "stats mode=server" in s, "server stdout")
        client = launch(
            "client",
            [
                "client",
                "127.0.0.1:" + str(sp),
                "--test-pattern",
                "--audio=false",
                "--splash-screen=false",
            ],
        )
        client.expect(
            lambda s: sum(len(line) > 70 and not any(c.isspace() for c in line)
                          for line in s.splitlines()) >= 20,
            "client media ready",
        )
        client.pump(0.5)
        overlay(client, "client")
        overlay(server, "server")
        ap = free_port()
        acds = launch(
            "acds",
            [
                "discovery-service",
                "127.0.0.1",
                "--port",
                str(ap),
                "--websocket-port",
                str(free_port()),
                "--database",
                str(root / "sessions.db"),
                "--status-screen=false",
                "--stats-interval",
                "1",
            ],
        )
        acds.expect(lambda s: "stats mode=discovery-service" in s, "acds stdout")
        discovery = launch(
            "discovery",
            [
                "--discovery-service",
                "127.0.0.1",
                "--discovery-service-port",
                str(ap),
                "--port",
                str(free_port()),
                "--test-pattern",
                "--audio=false",
                "--prefer-webrtc",
            ],
        )
        discovery.expect(lambda s: "Run: ascii-chat" in s, "invitation", 30)
        overlay(discovery, "discovery")
        overlay(acds, "acds")
        for p in reversed(panes):
            p.interrupt()
        print("PASS: all five modes shut down with stats open", flush=True)
    finally:
        for p in panes:
            p.close()


if __name__ == "__main__":
    main()
