"""Regression coverage for native test-pattern frame pacing."""

import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.media, pytest.mark.performance]


def test_luminance_test_pattern_sustains_60_fps(binary, test_environment):
    height = 80
    duration = 20
    command = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "--renderer", "luminance",
        "--stabilize=false",
        "--width", "240", "--height", str(height), "--splash-screen=false", "--audio=false",
        "--snapshot", "--snapshot-delay", str(duration),
    ]
    for run in range(1, 6):
        result = subprocess.run(command, env=test_environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=30, check=True)
        lines = result.stdout.splitlines()
        assert len(lines) % height == 0, f"run {run}: incomplete ASCII frame"
        frames = len(lines) // height
        # Repeat the long window so an intermittent 12-25 FPS collapse cannot
        # hide behind a healthy average from another run.
        assert frames >= 1000, (
            f"run {run}: only {frames} frames in {duration} seconds; expected approximately 60 FPS"
        )
