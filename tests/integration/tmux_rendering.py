#!/usr/bin/env python3
"""Linux/tmux rendering matrix. Run inside the test Docker image with tmux installed."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401
from ascii_chat_tests.cli import cli_command

import argparse
import itertools
import json
import re
from pathlib import Path
import time


from ascii_chat_tests.tmux import Pane


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
            argv = cli_command(str(args.binary.resolve()), "--log-file", str(root / name / "application.log"),
                    "--log-level", args.log_level, "mirror", "--file", str(root / "media.mp4"), "--loop", "--fps", "10",
                    "--audio=true" if name.startswith("audio-") else "--audio=false", "--splash-screen=false", *flags)
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
