"""Native invitation and splash behavior in isolated interpreters."""
import os
from pathlib import Path
import subprocess
import sys

import pytest

PROBES = Path(__file__).resolve().parents[3] / "tests/ui"
pytestmark = [pytest.mark.unit, pytest.mark.native]


@pytest.mark.parametrize("case", [
    "test_instruction_immediately_below_string", "test_centered_block", "test_compact_terminal",
    "test_long_string_wraps_without_losing_characters", "test_creation_and_joining", "test_only_logo_animates",
    "test_resize_bounds_and_stale_content_erasure", "test_short_terminal_prioritizes_invitation",
])
def test_invitation(shared_library, case):
    result = subprocess.run([sys.executable, str(PROBES / "probe_invitation.py"), str(shared_library),
                             "InvitationTests." + case], env=os.environ.copy(), capture_output=True, timeout=30)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-8000:]


@pytest.mark.parametrize("case", ["snapshot", "blocked-pipe", "cleanup-during-render"])
def test_splash_lifecycle(shared_library, case):
    result = subprocess.run([sys.executable, str(PROBES / "probe_splash_lifecycle.py"), str(shared_library), case],
                            env=os.environ.copy(), capture_output=True, timeout=20)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-8000:]
