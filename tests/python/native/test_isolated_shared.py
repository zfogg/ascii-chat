"""Native-library contracts run in sanitizer-compatible interpreters."""
import os
import shutil
import subprocess
import sys

import pytest

pytestmark = pytest.mark.unit


@pytest.mark.parametrize("module,requirements", [
    ("native_ssh_probe", ("ssh-keygen",)),
    ("native_encoder_probe", ("ffmpeg", "ffprobe")),
    ("native_log_probe", ()),
])
def test_shared_library_contract(shared_library, module, requirements):
    missing = [name for name in requirements if not shutil.which(name)]
    if missing:
        pytest.skip("Missing required programs: " + ", ".join(missing))
    result = subprocess.run([sys.executable, "-m", "ascii_chat_tests." + module, str(shared_library)],
                            env=os.environ.copy(), capture_output=True, timeout=110)
    if result.returncode == 77:
        pytest.skip(result.stdout.decode("utf-8", errors="replace"))
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")


def test_offline_symbolizer(shared_library, c_compiler):
    if sys.platform != "linux":
        pytest.skip("Linux symbolizer fixture")
    result = subprocess.run([sys.executable, "-m", "ascii_chat_tests.native_symbolizer_probe",
                             str(shared_library), "--cc", c_compiler],
                            env=os.environ.copy(), capture_output=True, timeout=100)
    if result.returncode == 77:
        pytest.skip(result.stdout.decode("utf-8", errors="replace"))
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")


def test_native_kitty(shared_library):
    if sys.platform == "win32":
        pytest.skip("POSIX C stdio fixture")
    result = subprocess.run([sys.executable, "-m", "ascii_chat_tests.native_kitty_probe", str(shared_library)],
                            env=os.environ.copy(), capture_output=True, timeout=45)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")
