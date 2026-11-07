"""Run the console resize fixture in a hidden, isolated Windows console."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401
from ascii_chat_tests.process import hidden_console_options

import subprocess
import sys

result = subprocess.run(
    [sys.argv[1]], **hidden_console_options(),
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
)
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
