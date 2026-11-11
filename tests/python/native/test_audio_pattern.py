"""Run sanitizer-sensitive native audio checks in an isolated interpreter."""
import os
import subprocess
import sys

import pytest

pytestmark = pytest.mark.unit


def test_audio_pattern_native(shared_library):
    environment = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1")
    result = subprocess.run([sys.executable, "-m", "ascii_chat_tests.native_audio_probe", str(shared_library)],
                            env=environment, capture_output=True, timeout=30)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")
