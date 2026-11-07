"""Height-only media clipping must retain complete glyphs and colors."""

import queue
import re
import time

import pytest

from ascii_chat_tests.terminal import Terminal

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


@pytest.mark.parametrize("color_mode", ["none", "truecolor"])
@pytest.mark.parametrize("counter", [False, True])
def test_height_only_clipping_preserves_visible_rows(binary, test_environment, color_mode, counter):
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "0",
        "--width", "40", "--height", "30", "--renderer", "glyph", "--palette", "custom",
        "--palette-chars", "@", "--color-mode", color_mode, f"--fps-counter={'true' if counter else 'false'}",
        "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=20, cols=80, terminal_replies=True, environment=test_environment)
    try:
        term.expect(
            lambda _: all(line[:40] == "@" * 40 for line in term.screen.display),
            "Height-only clipping did not retain all visible media rows",
            timeout=20,
        )
        if color_mode == "truecolor":
            colors = {term.screen.buffer[row][col].fg for row in range(20) for col in range(40)}
            assert len(colors) > 1, "truecolor attributes were lost during clipping"
    finally:
        term.close()


def test_large_height_only_frame_keeps_presenting(binary, test_environment):
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "1",
        "--width", "200", "--height", "100", "--renderer", "glyph", "--stabilize",
        "--fps-counter", "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=70, cols=280, environment=test_environment)
    values = []
    tail = ""
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                chunk = term.output.get(timeout=0.2)
            except queue.Empty:
                continue
            text = tail + chunk
            if "\x1b]11;?\x1b\\" in text:
                term.write("\x1b]11;rgb:0000/0000/0000\x1b\\")
            values.extend(int(value) for value in re.findall(r"FPS:\s*(\d{1,3})", text))
            tail = text[-16:]
        # A low floor catches the multi-second presentation stalls without
        # requiring a particular frame rate from a shared test machine.
        assert any(value >= 10 for value in values), f"FPS counter never reached 10: {values[-20:]}"
    finally:
        term.close()
