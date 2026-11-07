"""Run native CLI and shared-library test-pattern checks in isolation."""
import os
from pathlib import Path
import subprocess
import sys

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.native]
PROBE = Path(__file__).resolve().parents[4] / "tests/platform/probe_pattern.py"


def test_native_pattern(binary, shared_library, tmp_path):
    result = subprocess.run([sys.executable, str(PROBE), "--binary", str(binary), "--library", str(shared_library),
                             "--output", str(tmp_path / "evidence")], env=os.environ.copy(),
                            capture_output=True, timeout=120)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-10000:]
