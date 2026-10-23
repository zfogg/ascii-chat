"""Run the console resize fixture in a hidden, isolated Windows console."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import subprocess
import sys

startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
result = subprocess.run(
    [sys.argv[1]], startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
)
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
