#!/usr/bin/env python3
"""Run the compiled client_like_lifecycle.c probe in isolated processes.

Usage: python tests/integration/client_like_lifecycle.py path/to/probe
The probe must link to the same build's asciichat shared library.
"""

import os
from pathlib import Path
import subprocess
import sys


probe = Path(sys.argv[1]).resolve()
scenarios = [
    "success", "mirror", "prepare-error", "shutdown", "retry-success",
    "backoff", "no-retry", "veto", "interrupt", "limit", "discovery-destroy",
]
for scenario in scenarios:
    result = subprocess.run(
        [str(probe), scenario], capture_output=True, timeout=20,
        env={**os.environ, "CLAUDECODE": "1"},
    )
    if result.returncode:
        sys.stderr.write((result.stdout + result.stderr).decode("utf-8", errors="replace"))
        raise SystemExit(f"FAIL: {scenario} (exit {result.returncode})")
    print(f"PASS: {scenario}")
print(f"All {len(scenarios)} session lifecycle checks passed.")
