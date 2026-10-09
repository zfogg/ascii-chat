#!/usr/bin/env python3
"""Check half-block animation before/after resize and beyond a ten-second fixture loop."""
import argparse
import hashlib
import json
from pathlib import Path
import time
from tmux_rendering import Pane

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--binary", type=Path, required=True)
ap.add_argument("--artifacts", type=Path, required=True)
args = ap.parse_args()
root = args.artifacts.resolve()
p = Pane("halfblock-animation", [str(args.binary.resolve()), "--no-check-update", "--log-level", "warn",
    "--log-file", str(root / "halfblock-animation" / "application.log"), "mirror", "--file", str(root / "media.mp4"),
    "--loop", "--audio=false", "--splash-screen=false", "--fps", "10", "--render-mode", "half-block",
    "--color-mode", "truecolor"], root, 80, 30)
frames = []
try:
    p.expect(lambda s: "▀" in s, "initial")
    for i in range(36):
        if i == 5:
            p.resize(80, 8)
            p.expect(lambda s: "Terminal too small" in s, "small")
            p.resize(80, 30)
            p.expect(lambda s: "Terminal too small" not in s and "▀" in s, "restored")
        p.capture(f"frame-{i:02}")
        raw = (p.directory / f"frame-{i:02}.ansi").read_bytes()
        frames.append(hashlib.sha256(raw).hexdigest())
        time.sleep(.4)
    for start, end in [(0, 5), (6, 15), (28, 36)]:
        assert len(set(frames[start:end])) >= 3, f"Frozen animation in samples {start}:{end}"
finally:
    p.close()
(p.directory / "animation-results.json").write_text(json.dumps({"status": "pass", "frames": len(frames),
    "distinct_frames": len(set(frames)), "interval_seconds": .4, "checks": ["initial", "resized", "loop boundary"]}, indent=2))
print("PASS halfblock-animation", len(set(frames)), "distinct frames", flush=True)
