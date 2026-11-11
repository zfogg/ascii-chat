"""Fixtures shared by the migrated, import-safe pytest suites."""
from pathlib import Path
import os
import sys
import tempfile
import uuid

import pytest
from ascii_chat_tests.cli import clean_environment, native_test_environment, cli_command
from ascii_chat_tests.terminal import Terminal

ASCII_PALETTE_STANDARD = ".',;:clodxkO0KXNWM"
ASCII_PALETTE_GLYPH = " .-+*#"
ASCII_RENDER_WIDTH = 120
ASCII_RENDER_HEIGHT = 80


def pytest_addoption(parser):
    parser.addoption("--ascii-chat-binary", "--binary", dest="ascii_chat_binary",
                     help="Path to the built ascii-chat executable")
    parser.addoption("--terminal-probe", help="Path to the native terminal fixture")
    parser.addoption("--lifecycle-probe", help="Path to the client lifecycle fixture")
    parser.addoption("--media-capture-probe", help="Path to the media capture fixture")
    parser.addoption("--shared-library", help="Path to the built ascii-chat shared library")
    parser.addoption("--crypto-logging-probe", help="Path to the crypto logging fixture")
    parser.addoption("--terminal-resize-probe", help="Path to the Windows terminal resize fixture")
    parser.addoption("--acip-websocket-probe", help="Path to the ACIP WebSocket fixture")
    parser.addoption("--c-compiler", default="clang", help="C compiler for standalone build matrices")
    parser.addoption("--native-include", action="append", default=[], help="Additional native header directory")
    parser.addoption("--nmake", help="Windows NMake executable")
    parser.addoption("--clang-cl", help="Windows clang-cl executable")
    parser.addoption("--llvm-lib", help="Windows llvm-lib executable")
    parser.addoption("--webcam-device", type=int, help="Opt in to physical webcam checks")
    parser.addoption("--run-manual-discovery", action="store_true", help="Run legacy terminal discovery scenarios")
    parser.addoption("--run-hardware-audio", action="store_true", help="Run audio hardware scenario")
    parser.addoption("--speech-fixture", help="Speech audio for the hardware audio scenario")
    parser.addoption("--audio-speaker", type=int, help="Speaker device index for hardware audio")
    parser.addoption("--run-spaniel", action="store_true", help="Run external Spaniel session checks")
    parser.addoption("--mac-ssh", help="SSH target for physical LAN discovery")
    parser.addoption("--mac-binary", help="Remote native binary for physical LAN discovery")
    parser.addoption("--mac-lan-address", help="Remote LAN address for physical discovery")
    parser.addoption("--discovery-ipv6", action="store_true", help="Use IPv6 loopback for discovery ICE")
    parser.addoption("--discovery-debug-ice", action="store_true", help="Enable discovery ICE debug logs")
    parser.addoption("--recording-transitions-only", action="store_true", help="Run recording transition checks only")
    parser.addoption("--otel-contract-probe", help="Path to native telemetry capture fixture")
    parser.addoption("--spaniel-assert", help="Path to the offline Spaniel verifier")
    parser.addoption("--panic-report-probe", help="Path to the panic report executable")
    parser.addoption("--ui-native-probe", help="Path to the native UI fixture")
    parser.addoption("--ffmpeg", default="ffmpeg", help="FFmpeg executable for media fixtures")
    parser.addoption("--error-popup-probe", help="Path to the native error popup fixture")
    parser.addoption("--popup-diagnostics", action="store_true", help="Exercise debug diagnostics")
    parser.addoption("--ui-diagnostics", action="store_true", help="Exercise debug UI diagnostics")
    parser.addoption("--debug-sync", action="store_true", help="Require debug synchronization diagnostics")
    parser.addoption("--debug-updates", action="store_true", help="Exercise update-screen diagnostics")
    parser.addoption("--full-terminal-logs", action="store_true", help="Run all terminal log scenarios")
    parser.addoption("--codec-fixture", help="Unsupported optional audio codec fixture")
    parser.addoption("--failed-codec-fixture", help="Corrupt compressed frame fixture")
    parser.addoption("--errno-probe", help="Path to the native errno-stack fixture")
    parser.addoption("--sync-probe", help="Path to the native synchronization fixture")
    parser.addoption("--errno-tui-probe", help="Path to the native errno TUI fixture")
    parser.addoption("--backpressure-probe", help="Path to the live backpressure fixture")
    parser.addoption("--backpressure-transports", default="tcp,websocket,webrtc")
    parser.addoption("--backpressure-codecs", default="raw,h264,h265")
    parser.addoption("--backpressure-output", help="Directory for backpressure artifacts")
    parser.addoption("--yt-dlp-probe", help="Path to the fake yt-dlp fixture")
    parser.addoption("--extractor-audio-probe", help="Path to the native extractor audio fixture")


def pytest_configure(config):
    # Match the policy applied by the legacy CTest wrapper in every worker.
    os.environ["TESTING"] = "1"
    os.environ["ASCII_CHAT_OTEL_ENABLED_TELEMETRY"] = "none"
    os.environ["ASCII_CHAT_DISABLE_FIRST_USE"] = "1"
    configured_binary = config.getoption("ascii_chat_binary")
    if configured_binary:
        os.environ["ASCII_CHAT_TEST_BINARY"] = configured_binary
    os.environ.setdefault("ASCII_CHAT_TEST_RUN_ID", os.environ.get("PYTEST_XDIST_TESTRUNUID", uuid.uuid4().hex))
    log_root = Path(tempfile.gettempdir()) / "ascii-chat-test-logs"
    log_root.mkdir(parents=True, exist_ok=True)
    os.environ["ASCII_CHAT_TEST_LOG_ROOT"] = str(log_root)


@pytest.fixture(autouse=True)
def native_process_policy(monkeypatch, tmp_path):
    """Apply isolated environment and native policy to every child process."""
    import subprocess
    from ascii_chat_tests.cli import _copy_log_alias, _requested_log, is_test_binary, prepare_test_command
    original_run = subprocess.run
    original_popen = subprocess.Popen

    # Make inherited environments safe too. This covers tests that invoke the
    # binary with raw subprocess APIs instead of the shared CLI helpers.
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.setenv("USERPROFILE", str(tmp_path))
    monkeypatch.setenv("APPDATA", str(tmp_path))
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    monkeypatch.setenv("ASCII_CHAT_TEST_HOME", str(tmp_path))
    monkeypatch.setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1")

    class AliasedProcess:
        def __init__(self, process, requested, command):
            self._process = process
            self._requested = requested
            self._command = command

        def _sync_log(self):
            _copy_log_alias(self._command, self._requested)

        def communicate(self, *args, **kwargs):
            result = self._process.communicate(*args, **kwargs)
            self._sync_log()
            return result

        def wait(self, *args, **kwargs):
            result = self._process.wait(*args, **kwargs)
            self._sync_log()
            return result

        def __enter__(self):
            self._process.__enter__()
            return self

        def __exit__(self, *args):
            return self._process.__exit__(*args)

        def __getattr__(self, name):
            return getattr(self._process, name)

    def popen(command, *args, **kwargs):
        if isinstance(command, (list, tuple)):
            requested = _requested_log(command)
            if is_test_binary(command):
                command = prepare_test_command(command)
            kwargs["env"] = native_test_environment(tmp_path, kwargs.get("env"))
            return AliasedProcess(original_popen(command, *args, **kwargs), requested, command)
        return original_popen(command, *args, **kwargs)

    def run(command, *args, **kwargs):
        if isinstance(command, (list, tuple)):
            kwargs["env"] = native_test_environment(tmp_path, kwargs.get("env"))
        return original_run(command, *args, **kwargs)

    monkeypatch.setattr(subprocess, "Popen", popen)
    monkeypatch.setattr(subprocess, "run", run)
    yield


@pytest.fixture(autouse=True)
def serialize_shared_resources(request, tmp_path_factory):
    # Server topologies use large worker pools, while terminal scenarios need
    # exclusive access to interactive resources. Other cases remain parallel.
    if (request.node.get_closest_marker("network") or request.node.get_closest_marker("terminal") or
            (sys.platform == "win32" and request.node.get_closest_marker("native"))):
        from filelock import FileLock
        with FileLock(str(tmp_path_factory.getbasetemp().parent / "native-processes.lock"), timeout=300):
            yield
    else:
        yield


@pytest.fixture(scope="session")
def binary(request):
    value = (request.config.getoption("ascii_chat_binary") or
             os.environ.get("ASCII_CHAT_TEST_BINARY") or
             os.environ.get("ASCII_CHAT_BINARY"))
    if not value:
        pytest.fail("Pass --binary/--ascii-chat-binary or set ASCII_CHAT_BINARY/ASCII_CHAT_TEST_BINARY")
    path = Path(value).resolve()
    if not path.is_file():
        pytest.fail(f"Built ascii-chat binary does not exist: {path}")
    return path


@pytest.fixture
def test_environment(tmp_path):
    env = clean_environment()
    env = native_test_environment(tmp_path, env)
    env.update(TESTING="1", ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none", ASCII_CHAT_DISABLE_FIRST_USE="1",
               ASCII_CHAT_NO_CHECK_UPDATE="1", TERM="dumb", NO_COLOR="1")
    return env


@pytest.fixture
def ensure_can_render(binary, test_environment, tmp_path):
    """Render mirror output in a PTY and require a reset plus palette glyphs."""
    def render(palette="standard", delay="1", *extra_args):
        palette_chars = ASCII_PALETTE_STANDARD if palette == "standard" else ASCII_PALETTE_GLYPH
        term = Terminal(
            cli_command(str(binary), "--log-file", str(tmp_path / "ascii-render.log"), "mirror", "-S", "-D", delay,
                        "--test-pattern", "--audio=false", "--palette-chars", palette_chars,
                        "--splash-screen=false", *extra_args),
            rows=ASCII_RENDER_HEIGHT, cols=ASCII_RENDER_WIDTH, environment=test_environment)
        try:
            import queue
            import time
            raw = ""
            deadline = time.monotonic() + max(8.0, float(delay) + 5.0)
            while time.monotonic() < deadline and term.process.isalive():
                try:
                    raw += term.output.get(timeout=0.1)
                except queue.Empty:
                    continue
            while True:
                try:
                    raw += term.output.get_nowait()
                except queue.Empty:
                    break
            frame_start = raw.find("\x1b[H")
            assert "\ufffd" not in raw, "terminal stream contained invalid UTF-8"
            raw.encode("utf-8", errors="strict").decode("utf-8", errors="strict")
            assert frame_start >= 0, "mirror emitted no terminal frame reset"
            frame = raw[frame_start:]
            present = set(frame) & set(palette_chars.replace(" ", ""))
            minimum = 0.20 if palette == "standard" else 0.05
            assert len(present) / max(1, len(set(palette_chars.replace(" ", "")))) >= minimum
            return frame
        finally:
            term.close()
    return render


@pytest.fixture
def ascii_chat_test(request, tmp_path, binary, test_environment):
    """Shared invocation context for every native ascii-chat test process."""
    from ascii_chat_tests.cli import TestContext
    return TestContext(binary, tmp_path, request.node, test_environment)


@pytest.fixture
def ascii_chat_flags(ascii_chat_test):
    """Return the standard reproducible flags for a new binary invocation."""
    return ascii_chat_test.flags


@pytest.fixture
def test_ports(ascii_chat_test):
    """Allocate independent loopback TCP, WebSocket, and WebRTC ports."""
    return ascii_chat_test.ports()


@pytest.fixture
def service_factory(ascii_chat_test):
    from ascii_chat_tests.process import running
    def create(name, mode, options):
        return running(ascii_chat_test.binary, ascii_chat_test.directory, name, mode, options,
                       context=ascii_chat_test)
    return create


@pytest.fixture
def server_factory(ascii_chat_test):
    """Start servers with shared flags, loopback-only listeners, and unique logs."""
    from ascii_chat_tests.process import running
    def create(options=(), name="server"):
        tcp, websocket, webrtc = ascii_chat_test.ports()
        base = ["127.0.0.1", "::1", "--port", str(tcp), "--websocket-port", str(websocket),
                "--webrtc-bind-address", "127.0.0.1", "--webrtc",
                "--status-screen=false"]
        return running(ascii_chat_test.binary, ascii_chat_test.directory, name,
                       "server", base + list(options), context=ascii_chat_test)
    return create


@pytest.fixture(autouse=True)
def report_native_logs(request):
    # Keep pure helper/unit tests usable without a native build. Native tests
    # opt into the context by requesting one of its fixtures.
    if not (request.config.getoption("ascii_chat_binary") or
            os.environ.get("ASCII_CHAT_TEST_BINARY") or
            os.environ.get("ASCII_CHAT_BINARY")):
        yield
        return
    yield
    from ascii_chat_tests.cli import logs_for_test
    logs = logs_for_test(request.node.nodeid)
    if logs:
        print(f"ascii-chat test logs ({request.node.nodeid}):")
        for path in logs:
            print(path)


def _probe_path(request, option, description):
    value = request.config.getoption(option)
    if not value:
        pytest.skip(f"Pass {option} to run the {description}")
    path = Path(value).resolve()
    if not path.is_file():
        pytest.fail(f"{description} does not exist: {path}")
    return path


@pytest.fixture(scope="session")
def terminal_probe(request):
    return _probe_path(request, "--terminal-probe", "native terminal fixture")


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


@pytest.fixture(scope="session")
def lifecycle_probe(request):
    return _probe_path(request, "--lifecycle-probe", "client lifecycle fixture")


@pytest.fixture(scope="session")
def media_capture_probe(request):
    return _probe_path(request, "--media-capture-probe", "native media capture fixture")


@pytest.fixture(scope="session")
def shared_library(request):
    return _probe_path(request, "--shared-library", "ascii-chat shared library")


@pytest.fixture(scope="session")
def crypto_logging_probe(request):
    return _probe_path(request, "--crypto-logging-probe", "crypto logging fixture")


@pytest.fixture(scope="session")
def terminal_resize_probe(request):
    return _probe_path(request, "--terminal-resize-probe", "Windows terminal resize fixture")


@pytest.fixture(scope="session")
def acip_websocket_probe(request):
    return _probe_path(request, "--acip-websocket-probe", "ACIP WebSocket fixture")


@pytest.fixture(scope="session")
def c_compiler(request):
    return request.config.getoption("--c-compiler")


@pytest.fixture(scope="session")
def otel_contract_probe(request):
    return _probe_path(request, "--otel-contract-probe", "native telemetry contract fixture")


@pytest.fixture(scope="session")
def panic_report_probe(request):
    return _probe_path(request, "--panic-report-probe", "panic report executable")


@pytest.fixture(scope="session")
def ui_native_probe(request):
    return _probe_path(request, "--ui-native-probe", "native UI fixture")


@pytest.fixture(scope="session")
def error_popup_probe(request):
    return _probe_path(request, "--error-popup-probe", "native error popup fixture")


@pytest.fixture(scope="session")
def errno_tui_probe(request):
    return _probe_path(request, "--errno-tui-probe", "native errno TUI fixture")


@pytest.fixture(scope="session")
def yt_dlp_probe(request):
    return _probe_path(request, "--yt-dlp-probe", "fake yt-dlp fixture")


@pytest.fixture(scope="session")
def extractor_audio_probe(request):
    return _probe_path(request, "--extractor-audio-probe", "native extractor audio fixture")


@pytest.fixture(scope="session")
def backpressure_probe(request):
    return _probe_path(request, "--backpressure-probe", "live backpressure fixture")


@pytest.fixture(scope="session")
def sync_probe(request):
    return _probe_path(request, "--sync-probe", "native synchronization fixture")
