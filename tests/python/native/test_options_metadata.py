"""Run option metadata checks in a sanitizer-compatible interpreter."""
import os
import subprocess
import sys

import pytest

pytestmark = pytest.mark.unit


def test_options_metadata(shared_library):
    result = subprocess.run([sys.executable, "-m", "ascii_chat_tests.native_options_probe", str(shared_library)],
                            env=os.environ.copy(), capture_output=True, timeout=60)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")
