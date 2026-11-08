#!/usr/bin/env python3
"""Find C sources that might contain a defer invocation during CMake configuration."""

from __future__ import annotations

import io
import re
import sys
from pathlib import Path
from typing import cast


def main() -> int:
    cast(io.TextIOWrapper, sys.stdin).reconfigure(encoding="utf-8")
    cast(io.TextIOWrapper, sys.stdout).reconfigure(encoding="utf-8")
    pattern = re.compile(
        rb"(?<![A-Za-z0-9_])defer(?:[ \t\r\n]|/\*.*?\*/|//[^\r\n]*|\\\r?\n)*\(",
        re.DOTALL,
    )
    for name in sys.stdin.read().splitlines():
        if not name:
            continue
        path = Path(name)
        if pattern.search(re.sub(rb"\\\r?\n", b"", path.read_bytes())):
            print(path.as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
