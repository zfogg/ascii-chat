#!/usr/bin/env python3
"""Capture all procedural animations from real tmux panes and verify motion/recovery."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import time
from tmux_rendering import Pane

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--binary", type=Path, required=True)
ap.add_argument("--probe", type=Path, required=True)
ap.add_argument("--artifacts", type=Path, required=True)
args = ap.parse_args()
root = args.artifacts.resolve()
root.mkdir(parents=True, exist_ok=True)
# Stationary input makes changes in Matrix/rainbow output attributable to the effect.
fixture = root / "still.ppm"
fixture.write_bytes(b"P6\n160 120\n255\n" + bytes(v for y in range(120) for x in range(160)
    for v in ((x * 255 // 159), (y * 255 // 119), (x + y) * 255 // 278)))
video = root / "still.mp4"
subprocess.run(["ffmpeg", "-v", "error", "-y", "-loop", "1", "-i", str(fixture),
                "-t", "6", "-r", "20", "-pix_fmt", "yuv420p", str(video)], check=True)
results = []
for name, flags in [("splash", []), ("matrix", ["--matrix"]),
                    ("rainbow", ["--color-filter", "rainbow"]),
                    ("matrix-rainbow", ["--matrix", "--color-filter", "rainbow"]),
                    ("test-pattern-0", ["--test-pattern=0"]),
                    ("test-pattern-1", ["--test-pattern=1"])]:
    directory = root / ("anim-" + name)
    directory.mkdir(parents=True, exist_ok=True)
    base = [str(args.binary.resolve()), "--no-check-update", "--log-level", "warn",
            "--log-file", str(directory / "application.log")]
    source = flags if name.startswith("test-pattern-") else ["--file", str(video), "--loop", *flags]
    argv = ([str(args.probe.resolve()), "splash", str(directory / "application.log")] if name == "splash" else
            base + ["mirror", "--fps", "20", "--audio=false", "--splash-screen=false",
                    "--color-mode", "truecolor", "--render-mode", "foreground", *source])
    pane = Pane("anim-" + name, argv, root, 80, 30)
    hashes = []
    try:
        pane.expect(lambda s: ("Video chat in your terminal" in s if name == "splash" else len(s.strip()) > 100 and "[DEBUG]" not in s and "[INFO ]" not in s), "ready")
        for i in range(16):
            if i == 8:
                pane.resize(80, 6)
                pane.expect(lambda s: "Terminal too small" in s, "covered")
                time.sleep(.3)
                pane.resize(80, 30)
                pane.expect(lambda s: "Terminal too small" not in s and len(s.strip()) > 100, "restored")
            pane.capture(f"frame-{i:02}")
            raw = (pane.directory / f"frame-{i:02}.ansi").read_bytes()
            # Only inspect the logo for splash, excluding time-varying log/status text.
            if name == "splash": raw = b"\n".join(raw.splitlines()[1:5])
            hashes.append(hashlib.sha256(raw).hexdigest())
            time.sleep(.2)
        assert len(set(hashes[:8])) >= 4, "Animation did not advance before resize"
        assert len(set(hashes[8:])) >= 4, "Animation did not resume after resize"
    finally:
        if name == "splash":
            deadline = time.monotonic() + 10
            while not (pane.directory / "exit").exists() and time.monotonic() < deadline:
                time.sleep(.1)
        pane.close()
    results.append({"case": name, "status": "pass", "distinct_frames": len(set(hashes)), "frames": 16})
    (root / "results.json").write_text(json.dumps(results, indent=2))
    print("PASS", name, len(set(hashes)), "distinct frames", flush=True)
