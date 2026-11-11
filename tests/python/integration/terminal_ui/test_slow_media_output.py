"""Slow terminal writes must release synchronized output at row boundaries."""

import queue
import random
import time

import pytest

from ascii_chat_tests.terminal import Terminal

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


def test_slow_media_output_releases_complete_rows(binary, test_environment, tmp_path, request):
    if not request.config.getoption("--debug-updates"):
        pytest.skip("The controlled terminal-write delay requires a Debug build")
    image = tmp_path / "noise.ppm"
    image.write_bytes(b"P6\n640 360\n255\n" + random.Random(42).randbytes(640 * 360 * 3))
    env = test_environment.copy()
    env["ASCII_CHAT_TEST_SLOW_MEDIA_OUTPUT"] = "1"
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--file", str(image), "--loop",
        "--width", "280", "--height", "70", "--renderer", "glyph", "--palette", "custom",
        "--palette-chars", "@", "--color-mode", "truecolor", "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=70, cols=280, environment=env)
    output = ""
    segments = []
    try:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                output += term.output.get(timeout=0.2)
            except queue.Empty:
                continue
            parts = output.split("\x1b[?2026h")
            segments = [part.split("\x1b[?2026l", 1)[0] for part in parts
                        if "@" in part and "\x1b[?2026l" in part]
            if sum(segment.count("@") for segment in segments) >= 280 * 70:
                break
        assert len(segments) >= 2, "Expected a slow media frame to be split into synchronized updates"
        assert 0 < segments[0].count("\n") < 69, "The first update should end before the complete frame"
        assert segments[0].endswith("\n"), "The terminal update split a row or an escape sequence"
        assert "\x1b[H" not in segments[1], "The second update should continue the same frame"
        cells = 0
        for segment in segments:
            cells += segment.count("@")
            if cells >= 280 * 70:
                break
        assert cells == 280 * 70, "Synchronized updates lost or duplicated media cells"
    finally:
        term.close()


def test_sustained_glyph_rendering_has_no_sanitizer_reports(binary, test_environment, request):
    if not request.config.getoption("--debug-updates"):
        pytest.skip("Sustained sanitizer coverage requires a Debug build")
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "1",
        "--width", "80", "--height", "40", "--renderer", "glyph", "--stabilize",
        "--color-mode", "truecolor", "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=40, cols=100, environment=test_environment)
    tail = ""
    updates = 0
    try:
        deadline = time.monotonic() + 31
        while time.monotonic() < deadline:
            try:
                chunk = term.output.get(timeout=0.2)
            except queue.Empty:
                continue
            output = tail + chunk
            for report in ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:"):
                assert report not in output, f"Unexpected sanitizer report: {report}"
            updates += chunk.count("\x1b[?2026h")
            tail = output[-128:]
        assert updates >= 100, "Rendering did not continue throughout the capture"
    finally:
        term.close()
