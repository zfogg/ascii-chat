"""Verify graceful and panic quit shortcuts through a real terminal."""

import pytest


pytestmark = [pytest.mark.integration, pytest.mark.terminal]


def test_quit_shortcuts_remain_available_in_help(binary, terminal_factory):
    term = terminal_factory(
        [str(binary), "mirror", "--test-pattern", "--audio=false", "--splash-screen=false"],
        rows=30,
        cols=100,
        environment={"TERM": "xterm-256color"},
    )
    try:
        term.write("?")
        term.expect(lambda text: "q / Esc" in text and "panic exit" in text,
                    "quit shortcuts were not shown in help", timeout=15)
        term.write("Q")
        term.expect(lambda _text: not term.process.isalive(),
                    "Shift-Q did not panic-quit while help was open", timeout=10)
        assert term.process.exitstatus == 1
    finally:
        term.close()

