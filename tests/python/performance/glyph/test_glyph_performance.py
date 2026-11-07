"""Catch severe glyph-rendering regressions in the actual native binary."""

import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.media]


@pytest.mark.parametrize("pattern", [0, 1])
@pytest.mark.parametrize("stabilize", [False, True])
def test_glyph_renderer_keeps_up_with_live_frames(binary, test_environment, pattern, stabilize):
    command = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", str(pattern),
        "--width", "200", "--height", "100", "--renderer", "glyph",
        f"--stabilize={'true' if stabilize else 'false'}", "--splash-screen=false", "--audio=false",
        "--snapshot", "--snapshot-delay", "2", "--color-mode", "truecolor",
    ]
    result = subprocess.run(
        command, env=test_environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=True,
    )
    assert b"runtime error:" not in result.stderr, result.stderr.decode(errors="replace")
    lines = result.stdout.splitlines()
    assert len(lines) % 100 == 0, "incomplete ASCII frame"
    assert all(lines) and b"\x1b[" in result.stdout, "expected truecolor ASCII frames"
    # This deliberately loose floor catches the multi-hundred-millisecond Debug
    # regression without requiring a particular frame rate on shared CI hosts.
    assert len(lines) >= 2000, f"only {len(lines) // 100} frames in two seconds"
