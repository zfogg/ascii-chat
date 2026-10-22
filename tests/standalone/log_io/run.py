"""Compile real LOG_IO capture with a recording logger; verify output beyond pipe capacity.
Usage: python tests/standalone/log_io/run.py [--cc clang]
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests" / "scripts"))
from standalone_coverage import coverage_flags, standalone_output_directory
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="clang")
args = parser.parse_args()
with standalone_output_directory("log-io") as out:
    for name in ("log/log.h", "platform/abstraction.h"):
        dest = out / "ascii-chat" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / "stubs.h", dest)
    dest = out / "ascii-chat/log/io.h"
    shutil.copyfile(ROOT / "include/ascii-chat/log/io.h", dest)
    exe = out / "test.exe"
    subprocess.run([args.cc, *coverage_flags(), "-I", str(out), str(ROOT / "lib/log/io.c"),
                    str(HERE / "test.c"), "-o", str(exe)], check=True, cwd=out)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15, check=True)
    assert result.stdout == "stdout restored\n", result.stdout[:200]
    assert result.stderr == "stderr restored\n", result.stderr[:200]
    print("PASS: 2 MiB captured and replayed, repeated captures, failed redirection rollback, stream restoration")
