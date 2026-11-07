"""Fixtures shared by the migrated, import-safe pytest suites."""
from pathlib import Path
import os
import sys

import pytest
from ascii_chat_tests.cli import clean_environment


def pytest_addoption(parser):
    parser.addoption("--ascii-chat-binary", help="Path to the built ascii-chat executable")
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
