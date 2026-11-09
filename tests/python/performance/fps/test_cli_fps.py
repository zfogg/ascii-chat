"""Regression coverage for native test-pattern frame pacing."""

import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.media, pytest.mark.performance]


def test_luminance_test_pattern_sustains_60_fps(binary, test_environment):
    height = 40
    duration = 20
    command = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern",
        "--width", "80", "--height", str(height), "--splash-screen=false", "--audio=false",
        "--snapshot", "--snapshot-delay", str(duration),
    ]
    result = subprocess.run(command, env=test_environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=30, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) % height == 0, "incomplete ASCII frame"
    frames = len(lines) // height
    # A 20-second window catches intermittent cadence collapses that a short
    # average can hide, while leaving margin for shared CI hosts.
    assert frames >= 1000, f"only {frames} frames in {duration} seconds; expected approximately 60 FPS"
