"""Height-only media clipping must retain complete glyphs and colors."""

import queue
import re
import time

import pytest

from ascii_chat_tests.terminal import Terminal

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


def assert_media_rows_are_not_erased(term):
    term.pump(0.3)
    updates = [part.split("\x1b[?2026l", 1)[0] for part in "".join(term.raw).split("\x1b[?2026h")
               if "@" in part and "\x1b[?2026l" in part]
    assert len(updates) >= 2, "Expected multiple media frames"
    assert all("\x1b[2K" not in update for update in updates[-2:]), "Media rows were erased before repainting"


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
        # A prior overlay can leave text in the unused right margin. The next
        # frame must clear it without first blanking the visible media row.
        term.screen.cursor.y = 5
        term.screen.cursor.x = 60
        term.screen.draw("stale")
        term.expect(lambda _: term.screen.display[5][60:65] == " " * 5,
                    "The unused right margin was not cleared", timeout=5)
        assert_media_rows_are_not_erased(term)
        if color_mode == "truecolor":
            colors = {term.screen.buffer[row][col].fg for row in range(20) for col in range(40)}
            assert len(colors) > 1, "truecolor attributes were lost during clipping"
    finally:
        term.close()


@pytest.mark.parametrize("counter", [False, True])
def test_width_clipping_does_not_blank_media_rows(binary, test_environment, counter):
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "0",
        "--width", "100", "--height", "30", "--renderer", "glyph", "--palette", "custom",
        "--palette-chars", "@", "--color-mode", "none", f"--fps-counter={'true' if counter else 'false'}",
        "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=20, cols=80, terminal_replies=True, environment=test_environment)
    try:
        term.expect(lambda _: all(line[:60] == "@" * 60 for line in term.screen.display),
                    "Width-clipped frames did not fill the visible rows", timeout=20)
        assert_media_rows_are_not_erased(term)
    finally:
        term.close()


def test_fps_overlay_fits_without_wrapping(binary, test_environment):
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "1",
        "--width", "40", "--height", "20", "--fps-counter=true", "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=20, cols=80, terminal_replies=True, environment=test_environment)
    try:
        term.expect(lambda text: "FPS:" in text, "FPS overlay was not rendered", timeout=15)
        raw = "".join(term.raw)
        assert "FPS:" in raw
        assert "\x1b[1;70H" in raw, "FPS overlay did not reserve its full 11-column width"
        assert "\x1b[1;71H" not in raw, "FPS overlay starts too far right and wraps"
    finally:
        term.close()


@pytest.mark.parametrize("source_args", [
    ("--test-pattern", "1"),
    ("--test-audio", "--fft"),
])
def test_fps_toggle_does_not_halve_media_rate(binary, test_environment, source_args):
    environment = test_environment.copy()
    environment["ASCII_CHAT_FPS_COUNTER"] = "false"
    args = [
        str(binary), "--quiet", "--no-check-update", "mirror", *source_args,
        "--width", "40", "--height", "20", "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=20, cols=80, terminal_replies=True, environment=environment)
    try:
        term.expect(lambda _: any(row.strip() for row in term.screen.display), "Media did not start rendering", timeout=15)
        term.pump(1)
        term.pump(1)
        term.raw.clear()
        term.write("-")
        term.pump(0.5)
        if "FPS:" not in "".join(term.raw):
            # WinPTY can lose the first byte while the input worker finishes
            # entering raw mode. The second write is only a transport retry;
            # the rendering observation below is never restarted.
            term.write("-")
        term.expect(lambda text: "FPS:" in text, "FPS counter did not appear after '-'", timeout=5)
        start = time.monotonic()
        frame_counts = []
        synchronized_overlay_counts = []
        previous_raw_length = 0
        while time.monotonic() - start < 25:
            term.pump(1.0)
            raw = "".join(term.raw)
            new_output = raw[previous_raw_length:]
            previous_raw_length = len(raw)
            frame_counts.append(new_output.count("\x1b[r\x1b[H"))
            synchronized_frames = re.findall(r"\x1b\[\?2026h(.*?)\x1b\[\?2026l", new_output, re.DOTALL)
            synchronized_overlay_counts.append(sum("FPS:" in frame for frame in synchronized_frames))
        raw_values = [int(value) for value in re.findall(r"FPS:\s*(\d{1,3})", "".join(term.raw))]
        values = [value for index, value in enumerate(raw_values) if index == 0 or value != raw_values[index - 1]]
        assert values, "FPS counter did not appear after '-'"
        assert sum(frame_counts[-5:]) >= 5, f"Media rendering stopped after toggle: {frame_counts}"
        assert sum(synchronized_overlay_counts[-5:]) >= sum(frame_counts[-5:]) - 5, (
            f"FPS overlay was outside synchronized frames: frames={frame_counts}, overlays={synchronized_overlay_counts}"
        )
        assert len(values) >= 10, f"FPS counter did not produce a sustained reading: {values}"
        steady = values[10:]
        expected_min = 40 if source_args[0] == "--test-pattern" else 25
        assert min(steady or values) >= expected_min, f"FPS value dipped after toggle: {values}"
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
