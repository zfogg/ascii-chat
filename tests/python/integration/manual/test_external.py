"""Opt-in scenarios requiring external services or physical audio hardware."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

import pytest
from ascii_chat_tests.process import free_port

ROOT = Path(__file__).resolve().parents[4]
pytestmark = [pytest.mark.integration, pytest.mark.network]


def test_network_audio_hardware(binary, tmp_path, request):
    speech = request.config.getoption("--speech-fixture")
    speaker = request.config.getoption("--audio-speaker")
    if not request.config.getoption("--run-hardware-audio") or not speech or speaker is None:
        pytest.skip("Pass --run-hardware-audio, --speech-fixture, and --audio-speaker")
    missing = [name for name in ("sounddevice", "soundfile") if importlib.util.find_spec(name) is None]
    if missing:
        pytest.skip("Install the audio uv extra: " + ", ".join(missing))
    result = subprocess.run([sys.executable, str(ROOT / "tests/manual/probe_network_audio.py"),
                             "--binary", str(binary), "--output", str(tmp_path / "evidence"),
                             "--speech", speech, "--speaker", str(speaker), "--port", str(free_port())],
                            env=os.environ.copy(), capture_output=True, timeout=180)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-10000:]


def test_spaniel_alert_pack(request):
    if not request.config.getoption("--run-spaniel"):
        pytest.skip("Pass --run-spaniel to exercise the local Spaniel service")
    if importlib.util.find_spec("yaml") is None:
        pytest.skip("PyYAML is required for Spaniel checks")
    result = subprocess.run([sys.executable, str(ROOT / "tests/manual/probe_spaniel_alert_pack.py")],
                            env=os.environ.copy(), capture_output=True, timeout=300)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-10000:]
