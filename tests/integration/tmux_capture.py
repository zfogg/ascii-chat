#!/usr/bin/env python3
"""Prove live presentation survives process-wide LOG_IO redirection."""
import argparse
from pathlib import Path
import time
from tmux_rendering import Pane

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--probe", type=Path, required=True)
ap.add_argument("--artifacts", type=Path, required=True)
args = ap.parse_args()
root = args.artifacts.resolve()
p = Pane("capture-render", [str(args.probe.resolve()), "capture-render",
                          str(root / "capture-render" / "application.log")], root)
try:
    p.expect(lambda s: "Terminal too small" in s, "capturing-small")
    p.resize(80, 40)
    # LOG_IO remains active for five seconds; the frame must arrive before restore.
    p.expect(lambda s: "CAPTURE FRAME" in s, "capturing-frame", timeout=2)
    p.expect(lambda s: "PROBE PASSED" in s, "passed")
    deadline = time.monotonic() + 10
    while not (p.directory / "exit").exists() and time.monotonic() < deadline:
        time.sleep(.1)
finally:
    p.close()
print("PASS capture-render", flush=True)
