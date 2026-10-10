"""Measure mirror process CPU with the statistics overlay closed and open (Linux)."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import stats_tui as ui

p = argparse.ArgumentParser()
p.add_argument("--binary", required=True)
p.add_argument("--artifacts", required=True)
p.add_argument("--build-label", default="unspecified")
a = p.parse_args()
root = Path(a.artifacts).resolve()
root.mkdir(parents=True, exist_ok=True)
ui.SOCKET = "stats273-overhead"
pane = ui.Pane(
    "overhead",
    [
        "mirror",
        "--test-pattern",
        "--audio=false",
        "--splash-screen=false",
        "--fps",
        "30",
    ],
    str(Path(a.binary).resolve()),
    root,
)
try:
    pane.wait_media()
    shell = int(ui.tmux("display-message", "-p", "-t", "overhead", "#{pane_pid}"))
    pid = int(subprocess.check_output(["pgrep", "-P", str(shell)], text=True).strip())

    def ticks():
        fields = (
            Path("/proc/" + str(pid) + "/stat").read_text().rsplit(")", 1)[1].split()
        )
        return int(fields[11]) + int(fields[12])

    def sample():
        before = ticks()
        start = time.monotonic()
        time.sleep(5)
        elapsed = time.monotonic() - start
        return 100 * (ticks() - before) / os.sysconf("SC_CLK_TCK") / elapsed

    closed = sample()
    pane.open()
    opened = sample()
    size = ui.tmux(
        "display-message", "-p", "-t", "overhead", "#{pane_width}x#{pane_height}"
    ).strip()
    result = {
        "build": a.build_label,
        "source": f"test pattern, 30 fps, {size} terminal",
        "sample_seconds": 5,
        "closed_cpu_percent_one_core": round(closed, 2),
        "open_cpu_percent_one_core": round(opened, 2),
    }
    (root / "overhead.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
finally:
    pane.close()
