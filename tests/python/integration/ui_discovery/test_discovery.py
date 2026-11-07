"""Native discovery invitations through private terminal sessions."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

PROBES = Path(__file__).resolve().parents[4] / "tests/ui"
pytestmark = [pytest.mark.integration, pytest.mark.network, pytest.mark.terminal]


@pytest.mark.parametrize("kind", ["terminal", "tmux"])
def test_discovery_invitation(binary, tmp_path, request, kind):
    if not request.config.getoption("--run-manual-discovery"):
        pytest.skip("Legacy terminal discovery scenario is opt-in; pass --run-manual-discovery")
    if kind == "terminal" and sys.platform != "win32":
        pytest.skip("Windows ConPTY fixture")
    if kind == "tmux" and (sys.platform == "win32" or not shutil.which("tmux")):
        pytest.skip("POSIX tmux fixture")
    result = subprocess.run([sys.executable, str(PROBES / ("probe_discovery_" + kind + ".py")),
                             str(binary), str(tmp_path / "evidence")], env=os.environ.copy(),
                            capture_output=True, timeout=240)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-12000:]
