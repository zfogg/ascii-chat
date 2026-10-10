#!/usr/bin/env python3
"""Verify volume feedback in a real PTY, including retained paused frames.

Requires the same dependencies as terminal_ui.py (pyte, pywinpty/pexpect, ffmpeg).
"""

import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time

from terminal_ui import Terminal, free_port


def network_smoke(binary, media, artifacts, directory):
    """Exercise the shared feedback through TCP client and discovery mode input."""
    # Loopback UI fixtures do not need credentials or changes to the user's known_hosts.
    base = [str(binary.resolve()), "--no-check-update", "--no-encrypt", "--no-auth"]
    port, ws_port, acds_port, acds_ws = (free_port() for _ in range(4))
    services = []
    terms = []
    os.environ["ASCII_CHAT_QUESTION_PROMPT_RESPONSE"] = "y;y;y;y"

    def launch(name, argv):
        log = (artifacts / f"{name}.log").resolve()
        log.write_text("", encoding="utf-8")
        term = Terminal(base + ["--log-file", str(log)] + argv,
                        rows=24, cols=100)
        terms.append((name, term))
        return term

    def check(term, name):
        term.expect(lambda text: "dddddddd" in text and "........" in text,
                    f"{name}: no test-pattern media", timeout=30)
        term.write("\x1b[B")
        term.expect(lambda text: "90%" in text, f"{name}: no volume feedback", timeout=10)
        (artifacts / f"{name}-volume.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
        term.expect(lambda text: "90%" not in text, f"{name}: feedback did not expire", timeout=5)
        term.write("m")
        term.expect(lambda text: "MUTE" in text, f"{name}: no mute feedback")
        print(f"PASS {name}: volume, expiry, mute")

    try:
        for name, listen, argv in (
            ("server", port, ["server", "127.0.0.1", "--port", str(port),
                              "--websocket-port", str(ws_port), "--status-screen=false"]),
            ("acds", acds_port, ["discovery-service", "127.0.0.1", "--port", str(acds_port),
                                "--websocket-port", str(acds_ws), "--status-screen=false",
                                "--database", str(Path(directory) / "acds.db")]),
        ):
            service = subprocess.Popen(base + ["--log-file", str((artifacts / f"{name}.log").resolve())] + argv,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            services.append(service)
            deadline = time.monotonic() + 15
            while True:
                assert service.poll() is None, f"{name} exited during startup"
                try:
                    with socket.create_connection(("127.0.0.1", listen), timeout=0.2):
                        break
                except OSError:
                    assert time.monotonic() < deadline, f"{name} did not listen"
                    time.sleep(0.1)
        source = ["--file", str(media), "--loop", "--audio=false", "--color-mode", "none", "--fps", "10"]
        client = launch("client", ["client", "127.0.0.1", "--port", str(port),
                                   "--splash-screen=false", *source])
        client.pump(2)
        check(client, "client")
        common = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port), *source]
        host = launch("discovery-host", [*common, "--port", str(free_port()), "--splash-screen=true"])
        deadline = time.monotonic() + 20
        host_log = artifacts / "discovery-host.log"
        while True:
            host.pump(0.2)
            invitation = host_log.read_text(encoding="utf-8", errors="replace") if host_log.exists() else ""
            match = re.search(r"Run: ascii-chat ([a-z0-9-]+)", invitation)
            if match:
                session = match.group(1)
                break
            assert time.monotonic() < deadline, "Discovery host did not register a session"
        peer = launch("discovery-peer", [session, *common, "--splash-screen=false"])
        check(host, "discovery-host")
        check(peer, "discovery-peer")
    finally:
        for name, term in terms:
            (artifacts / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
            term.close()
        for service in services:
            service.terminate()
            service.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--modes", nargs="*", default=["none", "16", "truecolor"])
    parser.add_argument("--network", action="store_true")
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="volume-bar-") as directory:
        media = Path(directory) / "test.mp4"
        subprocess.run([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
            "testsrc2=size=160x120:rate=10", "-t", "5", "-c:v", "libx264",
            "-pix_fmt", "yuv420p", str(media)
        ], check=True)
        for mode in args.modes:
            term = Terminal([
                str(args.binary.resolve()), "--no-check-update", "--log-file",
                str((args.artifacts / f"{mode}.log").resolve()), "mirror", "--file", str(media),
                "--loop", "--splash-screen=false", "--audio=false", "--color-mode", mode,
                "--width", "100" if mode == "16" else "60", "--height", "20",
            ], rows=24, cols=100)

            def save(name):
                (args.artifacts / f"{mode}-{name}.txt").write_text(
                    "\n".join(term.screen.display), encoding="utf-8")

            def absent(text):
                return "%" not in text and "MUTE" not in text

            def volume(label):
                text = term.expect(lambda text: label in text, f"Expected volume {label}", timeout=3)
                assert any(label in line[94:98] for line in term.screen.display), text
                return text

            try:
                term.expect(lambda text: len(text.strip()) > 100, "Expected media")
                assert absent(term.pump(0.5)), "Bar visible before volume interaction"
                term.write("\x1b[B")
                volume("90%")
                save("playing")
                if mode != "none":
                    colors = {term.screen.buffer[y][95].fg for y in range(7, 16)}
                    assert {"red", "brown", "green"} <= colors, colors
                assert absent(term.pump(1.8)), "Playing overlay did not expire"
                term.write(" ")
                term.pump(0.5)
                clean = term.pump(0.3)
                assert absent(clean), "Pausing alone showed the bar"
                assert term.pump(0.3) == clean, "Media did not pause"
                term.write("\x1b[B")
                volume("80%")
                save("paused")
                term.expect(absent, "Paused overlay did not expire", timeout=3)
                assert term.pump(0.2) == clean, "Expiry did not restore the paused frame"
                term.write("m")
                volume("MUTE")
                term.write("m")
                volume("80%")
                term.write("\x1b[A\x1b[A\x1b[A")
                volume("100%")
                term.pump(0.8)
                term.write("\x1b[A")
                term.pump(0.9)
                assert "100%" in term.pump(0.05), "Repeated control did not refresh expiry at maximum"
                term.expect(absent, "Refreshed overlay did not expire", timeout=3)
                term.write("\x1b[B" * 12)
                volume("MUTE")
                term.expect(absent, "Minimum overlay did not expire", timeout=3)
                assert term.pump(0.2) == clean, "Volume keys resumed or damaged paused media"
                term.write("\x1b[A")
                volume("10%")
                term.resize(18, 80)
                term.expect(lambda text: "10%" in text, "Resize lost active overlay", timeout=1)
                assert any("10%" in line[74:78] for line in term.screen.display)
                save("resized")
                term.resize(24, 100)
                term.write("\x1b[A")
                volume("20%")
                term.write("?")
                term.expect(lambda text: "Keyboard Shortcuts" in text, "Expected help")
                assert not any("20%" in line[94:98] for line in term.screen.display), "Bar covered help"
                term.pump(1.7)
                term.write("?")
                term.expect(lambda text: "Keyboard Shortcuts" not in text and absent(text),
                            "Expired bar returned after help")
                term.resize(24, 100)
                term.pump(0.3)
                term.write(" ")
                term.expect(lambda text: text != clean, "Expected resumed playback")
                print(f"PASS {mode}: playback, pause, mute, limits, timeout refresh, restoration, resize, help")
            finally:
                save("final")
                (args.artifacts / f"{mode}.ansi").write_text("".join(term.raw), encoding="utf-8")
                term.close()
        if args.network:
            network_smoke(args.binary, media, args.artifacts, directory)


if __name__ == "__main__":
    main()
