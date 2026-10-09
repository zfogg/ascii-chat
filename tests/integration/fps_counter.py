#!/usr/bin/env python3
"""FPS acceptance and benchmark checks; run in the Linux Docker image with tmux/ffmpeg."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

import tmux_rendering
from tmux_rendering import Pane as BasePane


class Pane(BasePane):
    def text(self, text):
        # End option parsing before sending literal keyboard input.
        tmux_rendering.tmux("send-keys", "-t", self.name, "-l", "--", text)

    def close(self):
        try:
            if "Keyboard Shortcuts" in self.capture():
                self.text("?")
                self.expect(lambda s: "Keyboard Shortcuts" not in s, "closing-help")
        finally:
            super().close()


def fps(text):
    match = re.search(r"FPS:\s*(\d+)\s*$", text.splitlines()[0])
    return int(match[1]) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    tmux_rendering.SOCKET = "ascii378"
    subprocess.run(["tmux", "-L", tmux_rendering.SOCKET, "new-session", "-d", "-s", "fps-keeper"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tmux_rendering.tmux("set-option", "-g", "remain-on-exit", "on")
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    binary = str(args.binary.resolve())
    media = root / "media.mp4"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=640x360:rate=60", "-t", "3", "-c:v", "mpeg4", str(media)], check=True)
    help_text = subprocess.check_output([binary, "mirror", "--help"], text=True)
    (root / "help.txt").write_text(help_text)
    options = re.findall(r"^\s+(?:-[^\s,]+,\s+)?(--[\w-]+)", help_text, re.M)
    assert options.index("--fps-counter") == options.index("--fps") + 1, options
    assert "Press - to toggle the FPS counter" in help_text
    results = []
    for render in ["foreground", "background", "half-block"]:
        name = "controls-" + render
        argv = [binary, "--no-check-update", "--log-level", "debug", "--log-file",
                str(root / name / "application.log"), "mirror", "--file", str(media), "--loop",
                "--fps", "60", "--audio=false", "--splash-screen=false", "--render-mode", render,
                "--color-mode", "truecolor"]
        pane = Pane(name, argv, root, cols=120, rows=50)
        try:
            pane.expect(lambda s: len(s.strip()) > 1000 and "[" not in s and fps(s) is None, "default-off")
            time.sleep(1)
            pane.text("-")
            text = pane.expect(lambda s: (fps(s) or 0) > 0, "enabled")
            assert len(text.splitlines()[0]) == 120
            pane.text("-")
            pane.expect(lambda s: fps(s) is None, "disabled")
            pane.text("?")
            pane.expect(lambda s: "Keyboard Shortcuts" in s and (fps(s) or 0) > 0
                        and "Toggle FPS counter" in s, "help-forced")
            pane.text("?")
            pane.expect(lambda s: "Keyboard Shortcuts" not in s and fps(s) is None, "help-restored-off")
            pane.text("-")
            pane.expect(lambda s: (fps(s) or 0) > 0, "enabled-again")
            pane.text(" ")
            pane.expect(lambda s: fps(s) == 0, "paused", timeout=6)
            pane.text("-")
            pane.expect(lambda s: fps(s) is None, "paused-hidden")
            pane.text("-")
            pane.expect(lambda s: fps(s) == 0, "paused-visible", timeout=6)
            pane.text(" ")
            pane.expect(lambda s: (fps(s) or 0) > 0, "resumed")
            pane.resize(80, 40)
            text = pane.expect(lambda s: (fps(s) or 0) > 0 and len(s.splitlines()[0]) == 80, "resized")
            pane.resize(80, 8)
            pane.expect(lambda s: "Terminal too small" in s and fps(s) is None, "too-small")
            pane.resize(120, 50)
            pane.expect(lambda s: (fps(s) or 0) > 0, "recovered")
            pane.text("?")
            pane.expect(lambda s: "Keyboard Shortcuts" in s and (fps(s) or 0) > 0, "help-enabled")
            pane.text("-")
            pane.expect(lambda s: "FPS Counter : X" in s and (fps(s) or 0) > 0, "help-toggle-off")
            pane.text("?")
            pane.expect(lambda s: "Keyboard Shortcuts" not in s and fps(s) is None, "help-toggle-restored")
            results.append({"case": name, "status": "pass"})
        finally:
            pane.close()
    for cols, rows in [(120, 40), (160, 50)]:
        name = f"benchmark-{cols}x{rows}"
        argv = [binary, "--no-check-update", "--log-level", "debug", "--log-file",
                str(root / name / "application.log"), "mirror", "--file", str(media), "--loop",
                "--fps", "60", "--fps-counter=true", "--audio=false", "--splash-screen=false",
                "--color-mode", "truecolor"]
        pane = Pane(name, argv, root, cols=cols, rows=rows)
        try:
            pane.expect(lambda s: (fps(s) or 0) > 0, "startup")
            time.sleep(13)
            pane.capture("steady")
        finally:
            pane.close()
        log = (root / name / "application.log").read_text()
        samples = re.findall(r"FPS_OUTPUT: frames=(\d+) elapsed_ms=([\d.]+) write_ms=([\d.]+)", log)
        assert len(samples) >= 3, samples
        # Exclude startup and report completed presentations and time inside write calls.
        samples = [(int(n), float(t), float(w)) for n, t, w in samples[1:]]
        frames = sum(s[0] for s in samples)
        results.append({"case": name, "fps": frames * 1000 / sum(s[1] for s in samples),
                        "mean_write_ms": sum(s[2] for s in samples) / frames, "samples": samples})
    output = subprocess.check_output([binary, "--no-check-update", "--log-level", "error", "mirror",
                                      "--file", str(media), "--audio=false", "--snapshot", "--snapshot-delay", "0",
                                      "--fps-counter", "--width", "80", "--height", "24", "--color-mode", "none"],
                                     stderr=subprocess.DEVNULL)
    # The renderer may use ANSI repeat-character compression even without color.
    assert b"FPS:" not in output and b"\x1b7" not in output and b"\x1b8" not in output
    (root / "redirected.txt").write_bytes(output)
    (root / "results.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))
    tmux_rendering.tmux("kill-session", "-t", "fps-keeper")


if __name__ == "__main__":
    main()
