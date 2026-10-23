"""Export original renderer sources for EMISSION_BENCHMARK_BASELINE without changing a checkout."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("revision")
parser.add_argument("destination", type=Path)
args = parser.parse_args()
if args.destination.exists():
    parser.error("destination must be a new directory")
paths = subprocess.check_output([
    "git", "ls-tree", "-r", "--name-only", args.revision, "--",
    "lib/video/ascii", "lib/video/terminal/ansi.c",
    "include/ascii-chat/video/ascii/neon/common.h",
], text=True).splitlines()
for name in paths:
    output = args.destination / name
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(subprocess.check_output(["git", "show", f"{args.revision}:{name}"]))
print(f"Exported {len(paths)} files from {args.revision} to {args.destination.resolve()}")
