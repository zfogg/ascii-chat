#!/usr/bin/env python3
"""Linux/tmux rendering matrix. Run inside the test Docker image with tmux installed."""
import argparse
import itertools
import json
import os
import re
from pathlib import Path
import shlex
import subprocess
import time


SOCKET = "ascii390"


def tmux(*args):
    return subprocess.check_output(["tmux", "-L", SOCKET, *map(str, args)], text=True)


class Pane:
    def __init__(self, name, argv, root, cols=80, rows=8):
        self.name, self.root = name, root
        self.allowed_exit_codes = {"0"}
        self.directory = root / name
        self.directory.mkdir(parents=True, exist_ok=True)
        previous = self.directory / ("previous-" + str(time.time_ns()))
        for old in [*self.directory.glob("sanitizer.*"), self.directory / "application.log"]:
            if old.exists():
                previous.mkdir(exist_ok=True)
                old.rename(previous / old.name)
        (self.directory / "exit").unlink(missing_ok=True)
        # Only synthetic fixtures and loopback services are used.
        command = "env -u CLAUDECODE -u ASCII_CHAT_QUESTION_PROMPT_RESPONSE "
        command += shlex.quote("ASAN_OPTIONS=log_path=" + str(self.directory / "sanitizer")) + " " + shlex.join(argv)
        command += "; rc=$?; printf '%s' \"$rc\" > " + shlex.quote(str(self.directory / "exit"))
        tmux("new-session", "-d", "-s", name, "-x", cols, "-y", rows, "bash", "-c", command)

    def capture(self, label=None):
        text = tmux("capture-pane", "-p", "-t", self.name)
        if label:
            (self.directory / (label + ".txt")).write_text(text)
            (self.directory / (label + ".ansi")).write_text(tmux("capture-pane", "-e", "-p", "-t", self.name))
        return text

    def expect(self, predicate, label, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.capture()
            if predicate(text):
                self.capture(label)
                return text
            if (self.directory / "exit").exists():
                break
            time.sleep(.1)
        raise AssertionError(label + "\n" + self.capture("failure"))

    def resize(self, cols, rows):
        tmux("resize-window", "-t", self.name, "-x", cols, "-y", rows)

    def key(self, *keys):
        tmux("send-keys", "-t", self.name, *keys)

    def text(self, text):
        tmux("send-keys", "-t", self.name, "-l", text)

    def close(self):
        if not (self.directory / "exit").exists():
            self.key("C-c")
            deadline = time.monotonic() + 20
            while not (self.directory / "exit").exists() and time.monotonic() < deadline:
                time.sleep(.1)
        self.capture("final")
        (self.directory / "history.txt").write_text(tmux("capture-pane", "-p", "-S", "-", "-t", self.name))
        tmux("kill-session", "-t", self.name)
        status = (self.directory / "exit").read_text() if (self.directory / "exit").exists() else "timeout"
        log = (self.directory / "application.log").read_text(errors="replace") if (self.directory / "application.log").exists() else ""
        assert status in self.allowed_exit_codes, "Exit status: " + status
        assert "ERROR: AddressSanitizer" not in log and "runtime error:" not in log, "Sanitizer finding"
        assert not list(self.directory.glob("sanitizer.*")), "Sanitizer report emitted"


def matrix():
    colors = ["auto", "none", "mono", "16", "256", "truecolor"]
    renders = ["foreground", "background", "half-block"]
    for render, color in itertools.product(renders, colors):
        yield f"render-{render}-{color}", ["--render-mode", render, "--color-mode", color]
    for alias in ["fg", "bg"]:
        yield "render-alias-" + alias, ["--render-mode", alias, "--color-mode", "truecolor"]
    for palette in ["standard", "blocks", "digital", "minimal", "cool", "custom"]:
        extra = ["--palette-chars", " .oO#"] if palette == "custom" else []
        yield "palette-" + palette, ["--palette", palette, "--color-mode", "truecolor", *extra]
    for color_filter in ["none", "black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow"]:
        for render in renders:
            yield f"filter-{color_filter}-{render}", ["--color-filter", color_filter, "--render-mode", render]
    for render, color in itertools.product(renders, ["256", "truecolor"]):
        yield f"matrix-{render}-{color}", ["--matrix", "--color-mode", color, "--render-mode", render]
    for render in renders:
        yield "fps-" + render, ["--fps-counter", "--render-mode", render, "--color-mode", "truecolor"]
    for mode, source in itertools.product(["waveform", "fft"], ["all", "call", "mic", "media"]):
        yield f"audio-{mode}-{source}", ["--" + mode, "--audio-source", source, "--color-mode", "truecolor"]
    yield "stretch-flip", ["--stretch", "--flip-x", "--flip-y", "--color-mode", "truecolor"]
    yield "utf8-off", ["--utf8=false", "--palette", "blocks"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--filter", default="", help="Regular expression selecting case names")
    parser.add_argument("--results", default="matrix-results.json")
    parser.add_argument("--log-level", default="warn")
    args = parser.parse_args()
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    results = []
    for name, flags in matrix():
        if not re.search(args.filter, name):
            continue
        pane = None
        start = time.monotonic()
        silent_fft = name in {"audio-fft-call", "audio-fft-mic"}
        try:
            argv = [str(args.binary.resolve()), "--no-check-update", "--log-file", str(root / name / "application.log"),
                    "--log-level", args.log_level, "mirror", "--file", str(root / "media.mp4"), "--loop", "--fps", "10",
                    "--audio=true" if name.startswith("audio-") else "--audio=false", "--splash-screen=false", *flags]
            pane = Pane(name, argv, root)
            pane.expect(lambda s: "Terminal too small" in s, "startup-small")
            pane.resize(80, 40)
            pane.expect(lambda s: "Terminal too small" not in s and (silent_fft or len(s.strip()) > 30), "normal")
            if name.startswith("fps-"):
                pane.expect(lambda s: "FPS:" in s, "fps-overlay")
            pane.resize(80, 8)
            pane.expect(lambda s: "Terminal too small" in s, "resized-small")
            pane.text("?m ")
            time.sleep(.25)
            pane.resize(80, 40)
            pane.expect(lambda s: "Terminal too small" not in s and "Keyboard Shortcuts" not in s and (silent_fft or len(s.strip()) > 30), "restored")
            # Allow the input consumer to observe recovery and discard covered keys.
            time.sleep(.5)
            pane.text("?")
            pane.expect(lambda s: "Keyboard Shortcuts" in s, "help")
            pane.text("?")
            pane.expect(lambda s: "Keyboard Shortcuts" not in s, "help-closed")
            results.append({"case": name, "status": "pass"})
        except Exception as error:
            results.append({"case": name, "status": "fail", "error": str(error)})
        finally:
            if pane:
                try:
                    pane.close()
                except Exception as error:
                    results[-1]["status"] = "fail"
                    results[-1]["shutdown_error"] = str(error)
        results[-1]["seconds"] = round(time.monotonic() - start, 2)
        print(results[-1]["status"].upper(), name, flush=True)
        (root / args.results).write_text(json.dumps(results, indent=2))
    raise SystemExit(any(r["status"] == "fail" for r in results))


if __name__ == "__main__":
    main()
