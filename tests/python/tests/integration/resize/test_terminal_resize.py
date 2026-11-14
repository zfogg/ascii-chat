"""Run the resize fixture in a private Windows console."""
import subprocess
import sys

import pytest
from ascii_chat_tests.process import hidden_console_options

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


def test_terminal_resize(terminal_resize_probe, test_environment):
    if sys.platform != "win32":
        pytest.skip("Windows console fixture")
    result = subprocess.run([str(terminal_resize_probe)], **hidden_console_options(),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30, env=test_environment)
    assert result.returncode == 0, result.stdout.decode("utf-8", errors="replace")
