"""Native audio regression probes run outside the pytest process for ASan."""
import os
from pathlib import Path
import subprocess
import sys

import pytest

PROBES = Path(__file__).resolve().parents[3] / "tests/audio"
pytestmark = [pytest.mark.unit, pytest.mark.native]


@pytest.mark.parametrize("name,extra", [
    ("aec3_echo", ()), ("aec3_echo", ("--stereo",)), ("audio_pattern", ()),
    ("playout_recovery", ()), ("spectral_defaults", ()), ("spectral_integration", ()),
    ("spectral", ()), ("processor", ()),
])
def test_native_audio_probe(shared_library, name, extra):
    result = subprocess.run([sys.executable, str(PROBES / ("probe_" + name + ".py")), str(shared_library), *extra],
                            env=os.environ.copy(), capture_output=True, timeout=300)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-12000:]


@pytest.mark.parametrize("mode", ["client", "discovery"])
@pytest.mark.parametrize("source", ["auto", "mic", "media", "both", "remote"])
@pytest.mark.parametrize("test_audio", ["false", "true"])
@pytest.mark.parametrize("channels", ["1", "2"])
def test_opus_profile(shared_library, mode, source, test_audio, channels):
    result = subprocess.run([sys.executable, str(PROBES / "probe_opus_profiles.py"), str(shared_library),
                             mode, source, test_audio, channels], env=os.environ.copy(), capture_output=True, timeout=30)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-12000:]
