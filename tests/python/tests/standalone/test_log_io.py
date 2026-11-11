"""Compile and run descriptor-redirection tests in a separate process."""
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
from ascii_chat_tests.standalone import coverage_flags, standalone_output_directory

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / "tests/standalone/log_io"
pytestmark = pytest.mark.unit


def test_log_io(c_compiler):
    if sys.platform == "win32":
        pytest.skip("POSIX descriptor fixture")
    with standalone_output_directory("log-io") as out:
        for name in ("log/log.h", "platform/abstraction.h"):
            dest = out / "ascii-chat" / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(HERE / "stubs.h", dest)
        shutil.copyfile(ROOT / "include/ascii-chat/log/io.h", out / "ascii-chat/log/io.h")
        exe = out / "test"
        subprocess.run([c_compiler, *coverage_flags(), "-I", str(out), str(ROOT / "lib/log/io.c"),
                        str(HERE / "test.c"), "-o", str(exe)], check=True, cwd=out, timeout=60)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15, check=True)
        assert result.stdout == "stdout restored\n"
        assert result.stderr == "stderr restored\n"
