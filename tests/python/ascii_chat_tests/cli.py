"""Command construction and per-test context for native processes."""

from hashlib import sha1
import os
from pathlib import Path
import re
import shutil
import socket
import tempfile
from itertools import count


def cli_command(binary, *args, _apply_test_policy=True):
    """Build an invocation with update checks disabled for reproducible tests."""
    command = [str(binary), "--no-check-update", *args]
    return prepare_test_command(command) if _apply_test_policy else command


def prepare_test_command(command):
    """Apply the shared native-test policy to an arbitrary subprocess argv."""
    command = list(command)
    if ("--no-check-update" in command and "--log-level=debug" in command and
            ("--log-file" in command or any(str(value).startswith("--log-file=") for value in command))):
        return command
    configured = os.environ.get("ASCII_CHAT_TEST_BINARY") or os.environ.get("ASCII_CHAT_BINARY")
    if not configured:
        return command
    configured_path = Path(configured).resolve()
    if not any(Path(str(value)).resolve() == configured_path for value in command):
        return command
    if (os.environ.get("PYTEST_CURRENT_TEST") and
            os.environ.get("ASCII_CHAT_TEST_LOG_ROOT") and
            (os.environ.get("ASCII_CHAT_TEST_BINARY") or os.environ.get("ASCII_CHAT_BINARY"))):
        mode = next((value for value in command[1:] if value in _MODES), None)
        if mode is None:
            return command
        # Preserve deliberately invalid destinations so option-validation tests
        # still exercise the native error path instead of being redirected to
        # the shared artifact directory.
        requested_log = _requested_log(command)
        if requested_log and not Path(requested_log).parent.exists():
            return command
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
        flags = ["--no-check-update", "--log-level=debug", "--log-file", str(log)]
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
        flags = ["--no-check-update", "--log-level=debug", "--log-file", str(log_file)]
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
        return cli_command(self.binary, *self.flags(log_file, mode), *args,
                           _apply_test_policy=False)

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


def native_test_environment(directory, environment=None):
    """Return an environment isolated from the developer's ascii-chat state."""
    env = dict(os.environ if environment is None else environment)
    root = str(directory)
    env.update(
        HOME=root,
        USERPROFILE=root,
        APPDATA=root,
        LOCALAPPDATA=root,
        XDG_CONFIG_HOME=root,
        XDG_DATA_HOME=root,
        XDG_CACHE_HOME=root,
        ASCII_CHAT_TEST_HOME=root,
        ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1",
    )
    return env


def is_test_binary(command):
    """Whether argv launches the configured ascii-chat test binary."""
    configured = os.environ.get("ASCII_CHAT_TEST_BINARY") or os.environ.get("ASCII_CHAT_BINARY")
    if not configured or not command:
        return False
    configured_path = Path(configured).resolve()
    try:
        return Path(str(command[0])).resolve() == configured_path
    except (OSError, ValueError):
        return False


def run_cli(binary, *args, **subprocess_options):
    """Run the native binary with file-backed logs and captured render output."""
    import subprocess
    requested_log = _requested_log(args)
    if requested_log is None:
        test_id = os.environ.get("PYTEST_CURRENT_TEST", "native-cli").split(" ", 1)[0]
        root = os.environ.get("ASCII_CHAT_TEST_LOG_ROOT")
        if root:
            log_dir = Path(root)
            log_dir.mkdir(parents=True, exist_ok=True)
        else:
            log_dir = Path(tempfile.gettempdir()) / "ascii-chat-test-logs"
            log_dir.mkdir(parents=True, exist_ok=True)
        invocation = _next_invocation(test_id)
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", test_id).strip("_")[:120]
        digest = sha1(f"{test_id}:{invocation}".encode(), usedforsecurity=False).hexdigest()[:8]
        requested_log = log_dir / f"{stem}-{digest}.log"
        _CLI_LOGS.setdefault(test_id, []).append(requested_log)
        args = ("--log-file", str(requested_log), *args)
    command = cli_command(binary, *args)
    result = subprocess.run(command, **subprocess_options)
    _copy_log_alias(command, requested_log)
    return result


def start_cli(binary, *args, **subprocess_options):
    """Start the native binary with subprocess.Popen options."""
    import subprocess
    return subprocess.Popen(cli_command(binary, *args), **subprocess_options)


def _requested_log(args):
    values = list(args)
    for index, value in enumerate(values[:-1]):
        if value == "--log-file":
            return Path(values[index + 1])
        if str(value).startswith("--log-file="):
            return Path(str(value).split("=", 1)[1])
    return None


def _copy_log_alias(command, requested):
    if requested is None or "--log-file" not in command:
        return
    generated = Path(command[command.index("--log-file") + 1])
    if generated != requested and generated.exists():
        requested.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(generated, requested)
