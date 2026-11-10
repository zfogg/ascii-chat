"""Command construction and per-test context for native processes."""

from hashlib import sha1
import os
from pathlib import Path
import re
import socket
from itertools import count


def cli_command(binary, *args):
    """Build an invocation with update checks disabled for reproducible tests."""
    command = [str(binary), "--no-check-update", *args]
    return prepare_test_command(command)


def prepare_test_command(command):
    """Apply the shared native-test policy to an arbitrary subprocess argv."""
    command = list(command)
    if not any("ascii-chat" in Path(str(value)).name.lower() for value in command):
        return command
    if (os.environ.get("PYTEST_CURRENT_TEST") and
            os.environ.get("ASCII_CHAT_TEST_LOG_ROOT") and
            (os.environ.get("ASCII_CHAT_TEST_BINARY") or os.environ.get("ASCII_CHAT_BINARY"))):
        test_id = os.environ["PYTEST_CURRENT_TEST"].split(" ", 1)[0]
        invocation = _next_invocation(test_id)
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", test_id).strip("_")[:120]
        digest = sha1(f"{test_id}:{invocation}".encode(), usedforsecurity=False).hexdigest()[:8]
        log = Path(os.environ["ASCII_CHAT_TEST_LOG_ROOT"]) / f"{stem}-{digest}.log"
        _CLI_LOGS.setdefault(test_id, []).append(log)
        filtered = []
        skip_next = False
        for value in command:
            if skip_next:
                skip_next = False
                continue
            if value in {"--log-file", "--log-level"}:
                skip_next = True
                continue
            if value.startswith("--log-file=") or value.startswith("--log-level="):
                continue
            filtered.append(value)
        command = filtered
        flags = ["--no-check-update", "--log-level=debug", f"--log-file={log}"]
        mode = next((value for value in command[1:] if value in _MODES), None)
        if mode in {"client", "mirror", "discovery"} or mode is None:
            flags.insert(1, "--splash-screen=false")
        mode_index = next((index for index, value in enumerate(command[1:], 1)
                           if value in _MODES), len(command))
        command[mode_index:mode_index] = flags
        return command
    return command


_CLI_INVOCATIONS = {}
_CLI_LOGS = {}
_MODES = {"server", "client", "mirror", "discovery", "discovery-service", "mcp-server"}


def _next_invocation(test_id):
    value = _CLI_INVOCATIONS.get(test_id, 0) + 1
    _CLI_INVOCATIONS[test_id] = value
    return value


def logs_for_test(test_id):
    return tuple(_CLI_LOGS.get(test_id, ()))


class TestContext:
    def __init__(self, binary, directory, node, environment):
        self.binary = Path(binary)
        self.directory = Path(directory)
        self.nodeid = node.nodeid
        self.environment = environment
        self.logs = []
        self._invocations = count(1)

    def flags(self, log_file=None, mode=None):
        log_file = log_file or self.log_path()
        flags = ["--no-check-update", "--log-level=debug", f"--log-file={log_file}"]
        if mode in {None, "client", "mirror", "discovery"}:
            flags.insert(1, "--splash-screen=false")
        return flags

    def log_path(self):
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", self.nodeid).strip("_")[:120]
        invocation = next(self._invocations)
        digest = sha1(f"{self.nodeid}:{invocation}".encode(),
                      usedforsecurity=False).hexdigest()[:8]
        path = self.directory / f"{stem}-{digest}.log"
        self.logs.append(path)
        return path

    def command(self, *args, log_file=None):
        mode = next((value for value in args if value in _MODES), None)
        return cli_command(self.binary, *self.flags(log_file, mode), *args)

    def ports(self):
        values = []
        while len(values) < 3:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            if port not in values:
                values.append(port)
        return tuple(values)
def clean_environment():
    """Copy the inherited environment without ascii-chat option overrides."""
    return {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}


def run_cli(binary, *args, **subprocess_options):
    """Run the native binary with subprocess.run options."""
    import subprocess
    return subprocess.run(cli_command(binary, *args), **subprocess_options)


def start_cli(binary, *args, **subprocess_options):
    """Start the native binary with subprocess.Popen options."""
    import subprocess
    return subprocess.Popen(cli_command(binary, *args), **subprocess_options)
