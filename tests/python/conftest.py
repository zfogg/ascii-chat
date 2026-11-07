"""Fixtures shared by the migrated, import-safe pytest suites."""
from pathlib import Path
import os
import sys

import pytest
from ascii_chat_tests.cli import clean_environment


def pytest_addoption(parser):
    parser.addoption("--ascii-chat-binary", help="Path to the built ascii-chat executable")
    parser.addoption("--terminal-probe", help="Path to the native terminal fixture")


def pytest_configure(config):
    # Match the policy applied by the legacy CTest wrapper in every worker.
    os.environ["TESTING"] = "1"
    os.environ["ASCII_CHAT_OTEL_ENABLED_TELEMETRY"] = "none"
    os.environ["ASCII_CHAT_DISABLE_FIRST_USE"] = "1"


@pytest.fixture(scope="session")
def binary(request):
    value = request.config.getoption("--ascii-chat-binary") or os.environ.get("ASCII_CHAT_TEST_BINARY")
    if not value:
        pytest.fail("Pass --ascii-chat-binary or set ASCII_CHAT_TEST_BINARY to the built executable")
    path = Path(value).resolve()
    if not path.is_file():
        pytest.fail(f"Built ascii-chat binary does not exist: {path}")
    return path


@pytest.fixture
def test_environment(tmp_path):
    env = clean_environment()
    env.update(TESTING="1", ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none", ASCII_CHAT_DISABLE_FIRST_USE="1",
               ASCII_CHAT_NO_CHECK_UPDATE="1", HOME=str(tmp_path), USERPROFILE=str(tmp_path),
               APPDATA=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path), TERM="dumb", NO_COLOR="1")
    return env


@pytest.fixture
def service_factory(binary, tmp_path):
    from ascii_chat_tests.process import running
    def create(name, mode, options):
        return running(binary, tmp_path, name, mode, options)
    return create


@pytest.fixture(scope="session")
def terminal_probe(request):
    value = request.config.getoption("--terminal-probe")
    if not value:
        pytest.skip("Pass --terminal-probe to run the native terminal fixture")
    path = Path(value).resolve()
    if not path.is_file():
        pytest.fail(f"Native terminal fixture does not exist: {path}")
    return path


@pytest.fixture
def terminal_factory():
    pytest.importorskip("pyte")
    pytest.importorskip("winpty" if sys.platform == "win32" else "pexpect")
    from ascii_chat_tests.terminal import Terminal
    terminals = []
    def create(argv, **kwargs):
        terminal = Terminal(argv, **kwargs)
        terminals.append(terminal)
        return terminal
    yield create
    for terminal in reversed(terminals):
        terminal.close()
