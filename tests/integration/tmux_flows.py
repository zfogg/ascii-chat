#!/usr/bin/env python3
"""End-to-end tmux status, overwrite, snapshot and recording checks."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import time
from tmux_rendering import Pane


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--filter", default="")
    args = parser.parse_args()
    root = args.artifacts.resolve()
    binary = str(args.binary.resolve())
    results = []

    def run(name, flags, check, rows=8):
        if args.filter not in name:
            return
        pane = None
        try:
            base = [binary, "--no-check-update", "--log-file", str(root / name / "application.log"), "--log-level", "warn"]
            pane = Pane(name, base + flags, root, 100, rows)
            check(pane)
            results.append(dict(case=name, status="pass"))
        except Exception as error:
            results.append(dict(case=name, status="fail", error=str(error)))
        finally:
            if pane:
                try:
                    pane.close()
                except Exception as error:
                    results[-1].update(status="fail", shutdown_error=str(error))
        print(results[-1]["status"].upper(), name, flush=True)
        (root / "flow-results.json").write_text(json.dumps(results, indent=2))

    def resize(pane, label):
        pane.resize(19, 6)
        pane.expect(lambda s: "Terminal too small" in s, "small")
        pane.text("ignored")
        time.sleep(.2)
        pane.resize(100, 45)
        pane.expect(lambda s: "Terminal too small" not in s and label in s, "restored")
        time.sleep(.2)

    def exited(pane):
        deadline = time.monotonic() + 20
        while not (pane.directory / "exit").exists() and time.monotonic() < deadline:
            time.sleep(.1)
        assert (pane.directory / "exit").exists(), "process failed to finish"
        pane.capture("completed")

    ws = port()
    def status(pane):
        resize(pane, "127.0.0.1")
        pane.text("/")
        time.sleep(.2)
        pane.capture("grep")
        pane.key("Escape")
        pane.expect(lambda s: not s.rstrip().endswith("/"), "grep-cancelled")
        pane.resize(2, 2)
        time.sleep(.2)
        pane.capture("tiny")
        pane.resize(19, 6)
        pane.expect(lambda s: "Terminal too small" in s, "small-again")
        with socket.create_connection(("127.0.0.1", ws), timeout=5) as sock:
            sock.settimeout(5)
            sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n")
            assert b"101" in sock.recv(4096), "handshake stalled behind warning"
    run("server-status", ["server", "127.0.0.1", "--port", str(port()), "--websocket-port", str(ws), "--status-screen"], status)
    # ACDS currently has no status callback; verify its actual plain startup output.
    run("discovery-service-startup", ["discovery-service", "127.0.0.1", "--port", str(port()), "--websocket-port", str(port()), "--database", str(root / "acds.db")], lambda p: p.expect(lambda s: "Server fingerprint:" in s, "startup"), rows=45)

    for action, options in [("config", ["--config-create"]), ("manpage", ["--man-page-create"]), ("completions", ["--completions", "bash"])]:
        for answer in ["y", "n"]:
            output = root / (action + "-" + answer + ".out")
            output.write_text("SENTINEL")
            def overwrite(pane, output=output, answer=answer, action=action):
                if answer == "n":
                    pane.allowed_exit_codes = {str({"config": 4, "manpage": 105, "completions": 0}[action])}
                resize(pane, "Overwrite")
                pane.text(answer)
                pane.key("Enter")
                exited(pane)
                assert (output.read_text() == "SENTINEL") == (answer == "n")
            run("overwrite-" + action + "-" + answer, [*options, str(output)], overwrite)

    def disclosure(pane):
        resize(pane, "PUBLIC IP DISCLOSURE")
        pane.text("n")
        pane.key("Enter")
        pane.expect(lambda s: "PUBLIC IP DISCLOSURE" not in s and "127.0.0.1" in s, "declined")
    run("public-ip-disclosure", ["server", "127.0.0.1", "--port", str(port()), "--websocket-port", str(port()), "--status-screen", "--discovery", "--discovery-service", "127.0.0.1", "--discovery-expose-ip"], disclosure)

    mirror = ["mirror", "--file", str(root / "media.mp4"), "--loop", "--audio=false", "--splash-screen=false", "--fps", "10"]
    def media_controls(pane):
        resize(pane, "")
        pane.expect(lambda s: len(s.strip()) > 100, "media")
        pane.text(" ")
        time.sleep(.5)
        paused = pane.capture("paused")
        time.sleep(.4)
        assert pane.capture() == paused, "playback did not pause"
        pane.resize(19, 6)
        pane.expect(lambda s: "Terminal too small" in s, "paused-small")
        pane.text("?m ")
        time.sleep(.2)
        pane.resize(100, 45)
        pane.expect(lambda s: s == paused, "paused-restored")
        time.sleep(.4)
        assert pane.capture() == paused, "resize resumed paused capture"
        pane.text(" ")
        pane.expect(lambda s: s != paused, "resumed")
        pane.text("?")
        pane.expect(lambda s: "Keyboard Shortcuts" in s, "help")
        pane.resize(19, 6)
        pane.expect(lambda s: "Terminal too small" in s, "help-small")
        pane.text("?")
        time.sleep(.2)
        pane.resize(100, 45)
        pane.expect(lambda s: "Keyboard Shortcuts" in s, "help-restored")
        time.sleep(.2)
        pane.key("C-c")
        pane.expect(lambda s: "Keyboard Shortcuts" not in s, "help-signal-closed")
        pane.text(" ")
        time.sleep(.5)
        pane.text("?")
        pane.expect(lambda s: "Keyboard Shortcuts" in s, "paused-help")
        pane.key("C-c")
        pane.expect(lambda s: "Keyboard Shortcuts" not in s, "paused-help-signal-closed")
        assert not (pane.directory / "exit").exists(), "help cancellation exited playback"
    run("media-controls", mirror + ["--width", "60", "--height", "20"], media_controls)
    for theme in ["dark", "light", "auto"]:
        output = root / ("recording-" + theme + ".mp4")
        output.unlink(missing_ok=True)
        def record(pane, output=output):
            pane.expect(lambda s: "Terminal too small" in s, "recording-covered")
            time.sleep(2)
            pane.key("C-c")
            exited(pane)
            frames = subprocess.check_output(["ffprobe", "-v", "error", "-show_entries", "stream=nb_frames", "-of", "csv=p=0", str(output)], text=True)
            assert any(s.isdigit() and int(s) > 1 for s in frames.splitlines()), frames
        run("recording-" + theme, mirror + ["--render-file", str(output), "--render-theme", theme], record)
    raise SystemExit(any(r["status"] == "fail" for r in results))


if __name__ == "__main__":
    main()
