"""Run native CLI and shared-library test-pattern checks in isolation."""
import os
from pathlib import Path
import queue
import subprocess
import sys
import time

import pytest
from ascii_chat_tests.terminal import Terminal

pytestmark = [pytest.mark.integration, pytest.mark.native]
PROBE = Path(__file__).resolve().parents[4] / "tests/platform/probe_pattern.py"


def test_native_pattern(binary, shared_library, tmp_path):
    result = subprocess.run([sys.executable, str(PROBE), "--binary", str(binary), "--library", str(shared_library),
                             "--output", str(tmp_path / "evidence")], env=os.environ.copy(),
                            capture_output=True, timeout=120)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")[-10000:]


def test_test_pattern_stdout_is_valid_utf8_for_five_seconds(binary, test_environment):
    """Validate every captured render byte before any terminal decoding occurs."""
    result = subprocess.run([
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "--audio=false",
        "--snapshot", "--snapshot-delay", "5", "--splash-screen=false", "--color-mode", "none", "--strip-ansi",
    ], env=test_environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")[-4000:]
    assert result.stdout, "test-pattern produced no frame bytes"
    try:
        rendered = result.stdout.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        start = max(0, error.start - 16)
        end = min(len(result.stdout), error.end + 16)
        pytest.fail(f"invalid UTF-8 at byte {error.start}: {result.stdout[start:end].hex(' ')}")
    rows = [row for row in rendered.splitlines() if row]
    assert rows, "test-pattern produced no ASCII rows"
    assert len(rows) >= 24, f"test-pattern produced only {len(rows)} ASCII rows"
    assert any(any(not char.isspace() for char in row) for row in rows), "ASCII frames were blank"

    # Exercise the real interactive mirror route too; the pipe/snapshot check
    # above cannot prove that a TTY actually receives and renders a frame.
    term = Terminal([
        str(binary), "--no-check-update", "mirror", "--test-pattern", "--audio=false",
        "--splash-screen=false",
    ], rows=24, cols=80, environment=test_environment)
    try:
        deadline = time.monotonic() + 5.0
        rendered_tty = False
        while time.monotonic() < deadline:
            try:
                chunk = term.output.get(timeout=0.1)
            except queue.Empty:
                continue
            assert "\ufffd" not in chunk, "PTY decoded invalid UTF-8 as U+FFFD"
            term.raw.append(chunk)
            term.stream.feed(chunk)
            if any(any(not char.isspace() for char in row) for row in term.screen.display):
                rendered_tty = True
                break
        assert rendered_tty, "live mirror TTY never displayed ASCII art"
    finally:
        term.close()


def test_test_pattern_200x100_has_stable_vertical_bounds(binary, test_environment):
    """A large live frame must not bounce when diagnostics interleave output."""
    term = Terminal([
        str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern", "--audio=false",
        "--width", "200", "--height", "100", "--splash-screen=false",
    ], rows=100, cols=200, environment=test_environment)
    started = False
    samples = 0
    try:
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            try:
                chunk = term.output.get(timeout=0.1)
            except queue.Empty:
                continue
            assert not any(marker in chunk for marker in (
                "AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:"
            )), "sanitizer output was interleaved with the rendered frame"
            term.raw.append(chunk)
            term.stream.feed(chunk)
            nonblank = [index for index, row in enumerate(term.screen.display)
                        if any(not char.isspace() for char in row)]
            if not started:
                if len(nonblank) == 100:
                    started = True
                continue
            samples += 1
            assert nonblank == list(range(100)), \
                f"200x100 frame moved vertically: first={nonblank[0] if nonblank else None} " \
                f"last={nonblank[-1] if nonblank else None} rows={len(nonblank)}"
        assert started, "200x100 test-pattern never produced a complete frame"
        assert samples >= 10, f"insufficient stable 200x100 frame samples: {samples}"
    finally:
        term.close()
