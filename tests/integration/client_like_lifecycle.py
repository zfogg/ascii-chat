#!/usr/bin/env python3
"""Run the compiled client_like_lifecycle.c probe in isolated processes.

Usage: python tests/integration/client_like_lifecycle.py path/to/probe
The probe must link to the same build's asciichat shared library.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


probe = Path(sys.argv[1]).resolve()
scenarios = [
    "success", "mirror", "prepare-error", "shutdown", "retry-success",
    "backoff", "no-retry", "veto", "interrupt", "limit", "discovery-destroy",
]
scenarios += ["fps-source-client", "fps-source-mirror", "fps-override-client",
              "fps-override-mirror", "fps-override-60"]
with tempfile.TemporaryDirectory(prefix="ascii-chat-fps-") as directory:
    # A tiny 24 FPS Y4M fixture avoids an external encoder dependency.
    media = Path(directory) / "source.y4m"
    frame = b"FRAME\n" + bytes([128]) * (16 * 16 * 3 // 2)
    media.write_bytes(b"YUV4MPEG2 W16 H16 F24:1 Ip A1:1 C420jpeg\n" + frame * 3)
    for scenario in scenarios:
        arguments = [str(probe), scenario]
        if scenario.startswith("fps-"):
            arguments.append(str(media))
        result = subprocess.run(
            arguments, capture_output=True, timeout=20,
            env={**os.environ, "CLAUDECODE": "1"},
        )
        if result.returncode:
            sys.stderr.write((result.stdout + result.stderr).decode("utf-8", errors="replace"))
            raise SystemExit(f"FAIL: {scenario} (exit {result.returncode})")
        print(f"PASS: {scenario}")
print(f"All {len(scenarios)} session lifecycle checks passed.")
