#!/usr/bin/env python3
"""Repeat small-terminal startup to expose presentation initialization races."""
import argparse
from pathlib import Path
import time
from tmux_rendering import Pane

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--binary", type=Path, required=True)
ap.add_argument("--artifacts", type=Path, required=True)
ap.add_argument("--count", type=int, default=20)
args = ap.parse_args()
root = args.artifacts.resolve()
for i in range(args.count):
    name = "startup-stress-" + str(i)
    p = Pane(name, [str(args.binary.resolve()), "--no-check-update", "--log-file", str(root/name/"application.log"),
                   "--log-level", "debug", "mirror", "--file", str(root/"media.mp4"), "--loop",
                   "--audio=false", "--splash-screen=false", "--fps", "10"], root)
    try:
        p.expect(lambda s: "Terminal too small" in s, "small")
    except Exception:
        print("FAILED; retained tmux pane", name, flush=True)
        raise
    time.sleep(.2)
    p.close()
    print("PASS", name, flush=True)
