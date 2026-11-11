"""A disabled splash must not wait for the background update check."""

import subprocess
import time

import pytest

from ascii_chat_tests.terminal import Terminal

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


@pytest.fixture(autouse=True)
def require_debug_update_hooks(request):
    if not request.config.getoption("--debug-updates"):
        pytest.skip("The deterministic update-check delay requires a Debug build")


def test_no_splash_shows_video_while_update_check_runs(binary, test_environment, tmp_path):
    env = test_environment.copy()
    env.pop("ASCII_CHAT_NO_CHECK_UPDATE", None)
    gate = tmp_path / "finish-update"
    env.update(
        ASCII_CHAT_TEST_UPDATE_LATEST_VERSION="v99.0.0",
        ASCII_CHAT_TEST_UPDATE_GATE=str(gate),
        ASCII_CHAT_UPDATE_CACHE_DIR=str(tmp_path / "update-cache"),
    )
    args = [
        str(binary), "--quiet", "mirror", "--test-pattern", "0", "--width", "40", "--height", "20",
        "--palette", "custom", "--palette-chars", "@", "--color-mode", "none",
        "--splash-screen=false", "--audio=false",
    ]
    term = Terminal(args, rows=30, cols=80, terminal_replies=True, environment=env)
    try:
        term.expect(lambda _: any("@" * 30 in line for line in term.screen.display),
                    "Video did not appear while the update check was running", timeout=15)
        assert "Update available" not in "\n".join(term.screen.display)
        gate.touch()
        term.expect(lambda text: "Update available" in text,
                    "The completed background update was not shown", timeout=12)
        term.write("\x1b")
        term.expect(lambda text: "Update available" not in text and "@" * 30 in text,
                    "Video did not resume after dismissing the update overlay", timeout=8)
    finally:
        term.close()


def test_no_splash_exit_while_update_check_runs(binary, test_environment, tmp_path):
    env = test_environment.copy()
    env.pop("ASCII_CHAT_NO_CHECK_UPDATE", None)
    env.update(ASCII_CHAT_TEST_UPDATE_LATEST_VERSION="v0.0.1", ASCII_CHAT_TEST_UPDATE_GATE=str(tmp_path / "finish-update"),
               ASCII_CHAT_UPDATE_CACHE_DIR=str(tmp_path / "update-cache"))
    args = [str(binary), "--quiet", "mirror", "--test-pattern", "0", "--width", "40", "--height", "20",
            "--palette", "custom", "--palette-chars", "@", "--color-mode", "none", "--splash-screen=false",
            "--audio=false"]
    term = Terminal(args, rows=30, cols=80, terminal_replies=True, environment=env)
    try:
        term.expect(lambda _: any("@" * 30 in line for line in term.screen.display), "Video did not appear", timeout=15)
        started = time.monotonic()
        term.interrupt()
        assert time.monotonic() - started < 2, "Exit waited for the background update check"
    finally:
        term.close()


def test_enabled_splash_still_shows_startup_update_prompt(binary, test_environment, tmp_path):
    env = test_environment.copy()
    env.pop("ASCII_CHAT_NO_CHECK_UPDATE", None)
    env.update(ASCII_CHAT_TEST_UPDATE_LATEST_VERSION="v99.0.0", ASCII_CHAT_TEST_UPDATE_SLOW="1",
               ASCII_CHAT_UPDATE_CACHE_DIR=str(tmp_path / "update-cache"))
    args = [str(binary), "--quiet", "mirror", "--test-pattern", "0", "--width", "40", "--height", "20",
            "--palette", "custom", "--palette-chars", "@", "--color-mode", "none", "--splash-screen=true",
            "--audio=false"]
    term = Terminal(args, rows=30, cols=80, terminal_replies=True, environment=env)
    try:
        term.expect(lambda text: "Update available" in text and "Update now" in text,
                    "The splash startup update prompt did not appear", timeout=12)
        term.write("n")
        term.expect(lambda text: "Update available" not in text and "@" * 30 in text,
                    "Video did not start after dismissing the update prompt", timeout=8)
    finally:
        term.close()


def test_no_splash_snapshot_does_not_wait_for_update(binary, test_environment, tmp_path):
    env = test_environment.copy()
    env.pop("ASCII_CHAT_NO_CHECK_UPDATE", None)
    env.update(ASCII_CHAT_TEST_UPDATE_LATEST_VERSION="v99.0.0", ASCII_CHAT_TEST_UPDATE_GATE=str(tmp_path / "finish-update"),
               ASCII_CHAT_UPDATE_CACHE_DIR=str(tmp_path / "update-cache"))
    args = [str(binary), "--quiet", "mirror", "--test-pattern", "0", "--width", "40", "--height", "20",
            "--palette", "custom", "--palette-chars", "@", "--color-mode", "none", "--splash-screen=false",
            "--audio=false", "--snapshot", "--snapshot-delay", "0"]
    result = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10, check=True)
    rows = result.stdout.splitlines()
    assert len(rows) == 20 and all(row.startswith(b"@") for row in rows)
