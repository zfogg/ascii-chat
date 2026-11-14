"""Regression coverage for the real FFT-to-webcam terminal transition."""

import re
import time
from pathlib import Path

import pytest

from ascii_chat_tests.terminal import CaptureScreen, CaptureStream, Terminal


pytestmark = [pytest.mark.integration, pytest.mark.terminal]
ROOT = Path(__file__).resolve().parents[5]

FRAME_END = "\x1b[?2026l"
FRAME_START = "\x1b[?2026h"


def _frames(raw: str, columns: int, rows: int) -> list[tuple[str, ...]]:
    """Decode synchronized terminal frames into screen snapshots."""
    snapshots: list[tuple[str, ...]] = []
    for match in re.finditer(
        re.escape(FRAME_START) + r".*?" + re.escape(FRAME_END), raw, re.DOTALL
    ):
        screen = CaptureScreen(columns, rows)
        CaptureStream(screen).feed(match.group(0))
        snapshots.append(tuple(screen.display))
    return snapshots


def _pattern_similarity(left: tuple[str, ...], right: tuple[str, ...]) -> float:
    pairs = zip("".join(left), "".join(right))
    equal = sum(a == b for a, b in pairs)
    return equal / max(1, len("".join(left)))


def _has_video_content(frame: tuple[str, ...]) -> bool:
    return sum(bool(line.strip()) for line in frame) >= 10


def _mirror_args(binary: Path, log_file: Path, *extra: str) -> list[str]:
    return [
        str(binary),
        "--quiet",
        "--no-check-update",
        "--log-level=INFO",
        "--log-file",
        str(log_file),
        "mirror",
        "--render-mode",
        "background",
        "--renderer",
        "glyph",
        "--color-mode",
        "none",
        "--splash-screen=false",
        *extra,
    ]


def test_disabling_fft_does_not_flash_the_test_pattern(
    binary: Path, test_environment: dict[str, str], tmp_path: Path
) -> None:
    """The first frame after KEY_3 must be webcam, never synthetic test video."""
    rows, columns = 24, 80

    pattern = Terminal(
        _mirror_args(binary, tmp_path / "pattern.log", "--audio=false", "--test-pattern=0"),
        rows=rows,
        cols=columns,
        environment=test_environment,
        cwd=ROOT,
    )
    try:
        pattern.expect(
            lambda _: bool(_frames("".join(pattern.raw), columns, rows)),
            "explicit test-pattern mirror did not render",
            timeout=15,
        )
        pattern.pump(1.0)
        test_pattern_frames = [
            frame for frame in _frames("".join(pattern.raw), columns, rows) if _has_video_content(frame)
        ]
        assert test_pattern_frames, "explicit test-pattern mirror rendered no video frames"
    finally:
        pattern.close()
        time.sleep(2.0)

    live = Terminal(
        _mirror_args(
            binary,
            tmp_path / "live.log",
            "--fft",
        ),
        rows=rows,
        cols=columns,
        environment=test_environment,
        cwd=ROOT,
    )
    try:
        # Let the webcam and visualization establish their real terminal
        # snapshots before toggling. The bug is a queued snapshot that can be
        # presented after the option changes, not merely the capture call.
        log_path = tmp_path / "live.log"
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            live.pump(0.02)
            log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            if "Webcam OS media type accepted" in log:
                break
        else:
            raise AssertionError("webcam did not initialize")
        live.pump(4.0)
        raw_before_toggle = len("".join(live.raw))
        fft_frames = [
            frame for frame in _frames("".join(live.raw), columns, rows) if _has_video_content(frame)
        ]
        assert fft_frames, "FFT mirror rendered no video frames"
        live.write("3")

        deadline = time.monotonic() + 10
        webcam_seen = False
        toggle_seen = False
        while time.monotonic() < deadline:
            live.pump(0.05)
            log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            if "Audio frequency display: disabled" in log:
                toggle_seen = True
            if not toggle_seen:
                continue
            post_toggle = _frames("".join(live.raw)[raw_before_toggle:], columns, rows)
            for current in post_toggle:
                visualization_best = max((_pattern_similarity(current, frame) for frame in fft_frames), default=0.0)
                source_best = max((_pattern_similarity(current, frame) for frame in test_pattern_frames), default=0.0)
                if _has_video_content(current) and max(visualization_best, source_best) >= 0.90:
                    pytest.fail(
                        "KEY_3 rendered a cached FFT/test-pattern frame "
                        f"(visualization_similarity={visualization_best:.3f}, source_similarity={source_best:.3f})\n"
                        + "\n".join(current)
                    )
                if _has_video_content(current):
                    webcam_seen = True
            if webcam_seen:
                break

        assert toggle_seen, "KEY_3 was not processed by the live CLI"

        assert webcam_seen, "no post-FFT webcam frame rendered"
    finally:
        live.close()
