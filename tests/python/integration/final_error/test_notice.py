import pytest
"""Check the error retained after an interactive session exits."""
from pathlib import Path
import argparse
import importlib.util
import os
import re
import sys
import time

from ascii_chat_tests.cli import cli_command

from ascii_chat_tests.terminal import Terminal
from ascii_chat_tests.process import free_port


pytestmark = [pytest.mark.integration, pytest.mark.terminal, pytest.mark.network]


def check(binary, options, unicode, color, diagnostics):
    port = free_port()
    term = Terminal(cli_command(str(binary.resolve()), *options, "--discovery-service", "127.0.0.1",
                     "--discovery-service-port", str(port), "--test-pattern", "--audio=false"),
                    rows=35, cols=120, environment={"ASCII_CHAT_DISABLE_FIRST_USE": "1", "TERM": "xterm-256color",
                                                    "LANG": "C.UTF-8"})
    try:
        deadline = time.monotonic() + 60
        while term.process.isalive() and time.monotonic() < deadline:
            term.pump(.2)
        term.pump(.5)
        assert not term.process.isalive(), f"Discovery did not exit after its connection failed: {''.join(term.raw)[-3000:]}"
        output = "".join(term.raw)
        final = output[output.rfind("ERROR_NETWORK"):]
        assert "Network error" in final and "Details:" in final, final
        assert "Thread:" in final, final
        if diagnostics:
            assert "Source:" in final and "Backtrace" in final, final
            plain = re.sub(r"\x1b\[[0-9;]*m", "", final)
            assert re.search(r"\[0\].*parallel_connect\(\)", plain), plain
            assert "backtrace_capture()" not in plain and "errno_set_with_message()" not in plain, plain
        else:
            assert "Backtrace" not in output, "Release output included a backtrace"
        assert ("│" in final and "└" in final) == unicode, final
        assert ("\x1b[" in final) == color, final
    finally:
        term.close()


@pytest.mark.parametrize("options,unicode,color", [
    (["--color=true", "--color-mode=truecolor"], True, True),
    (["--utf8=false", "--color=false"], False, False),
])
def test_final_error_notice(binary, request, options, unicode, color):
    check(binary, options, unicode, color, request.config.getoption("--ui-diagnostics"))
