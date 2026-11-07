"""Option parsing and validation contracts against a fresh native process."""
import os
import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.cli]


def cases():
    yield ["--help"], True, "POSITIONAL ARGUMENTS", {}
    yield ["mirror", "--help"], True, "ascii-chat mirror", {}
    yield ["--version"], True, "ascii-chat v", {}
    for mode in ("client", "server"):
        for address in ("127.0.0.1", "::1", ""):
            yield [mode, "--webrtc-bind-address", address, "--show-capabilities"], True, "Terminal Capabilities:", {}
        yield [mode, "--show-capabilities"], True, "Terminal Capabilities:", {"ASCII_CHAT_WEBRTC_BIND_ADDRESS": "::1"}
        for address in ("localhost", "[::1]", "127.0.0.1:5000", "256.0.0.1"):
            yield [mode, "--webrtc-bind-address", address, "--show-capabilities"], False, "webrtc-bind-address", {}
    for address in ("localhost", "[::1]", "127.0.0.1:5000", "256.0.0.1"):
        yield ["--webrtc-bind-address", address, "--test-pattern", "--audio=false"], False, "webrtc-bind-address", {}
    yield ["--test-pattern", "--audio=false"], False, "webrtc-bind-address", {"ASCII_CHAT_WEBRTC_BIND_ADDRESS": "localhost"}
    for arguments, message in [
        (["--log-level"], "--log-level requires a value"),
        (["--log-level", "bad"], "Invalid log level 'bad'"),
        (["--log-file"], "requires a file path"),
        (["--completions"], "--completions requires shell name"),
        (["mirror", "--quiet"], "must appear before the mode"),
        (["mirror", "--not-an-option"], "Unknown option: --not-an-option"),
        (["mirror", "--width", "bad"], "not a valid integer"),
        (["mirror", "--seek", "3"], "--seek requires --file or --url"),
        (["mirror", "--pause"], "--pause requires --file or --url"),
        (["mirror", "--file", "-", "--seek", "3"], "--seek cannot be used with stdin"),
        (["mirror", "--url", "bad url"], "--url must be a valid HTTP(S) URL"),
        (["client", "--key", "./missing-key"], "Key file not found"),
        (["--color=false", "mirror", "--color-filter", "rainbow"], "--color-filter cannot be used with --color=false"),
    ]:
        yield arguments, False, message, {}
    for mode in (["mirror"], ["client"], []):
        for sources in (["--test-pattern", "--file", "video.mp4"], ["--file", "video.mp4", "--test-pattern"],
                        ["--test-pattern=0", "--file", "video.mp4"], ["--test-pattern=1", "-f", "-"]):
            yield [*mode, *sources], False, "--test-pattern and --file are mutually exclusive", {}
        for sources in (["--test-pattern", "--url", "https://example.com/video.mp4"],
                        ["--url", "https://example.com/video.mp4", "--test-pattern"]):
            yield [*mode, *sources], False, "--test-pattern and --url are mutually exclusive", {}
        for visual in ("--waveform", "--fft"):
            yield [*mode, "--test-pattern", visual], False, "--test-pattern cannot be used with --waveform or --fft", {}
    yield ["--show-capabilities"], True, "Terminal Capabilities:", {}
    for mode in ("mirror", "client"):
        yield [mode, "--width", "100", "--height", "30", "--show-capabilities"], True, "Terminal Size: 100x30", {}
    for mode in ("server", "discovery-service"):
        yield [mode, "--show-capabilities"], True, "Terminal Capabilities:", {}
    for prefix in (["--no-check-update"], ["--quiet", "--verbose", "2"], ["--color=false"], ["--log-level", "error"]):
        yield [*prefix, "mirror", "--width", "100", "--height", "30", "--show-capabilities"], True, "Terminal Size: 100x30", {}
    yield ["mirror", "--width", "100", "--height", "30", "--show-capabilities"], True, "Terminal Size: 100x30", {"ASCII_CHAT_WIDTH": "80", "ASCII_CHAT_HEIGHT": "24"}
    yield ["mirror", "--palette-chars", "abc", "--show-capabilities"], True, "Terminal Capabilities:", {}
    if os.name != "nt":
        yield ["mirror", "--palette-chars", "░▒▓", "--utf8=false", "--show-capabilities"], False, "non-ASCII characters but --utf8=false", {}


@pytest.mark.parametrize("arguments,success,expected,extra_env", list(cases()))
def test_options(binary, tmp_path, test_environment, arguments, success, expected, extra_env):
    env = {**test_environment, "CLAUDECODE": "1", **extra_env}
    result = subprocess.run([str(binary), *arguments], cwd=tmp_path, env=env,
                            capture_output=True, encoding="utf-8", errors="replace", timeout=30)
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == success, (arguments, result.returncode, output)
    assert expected in output, (arguments, expected, output)


def test_snapshot_renders_a_frame(binary, tmp_path, test_environment):
    result = subprocess.run([str(binary), "mirror", "--test-pattern", "--audio=false", "--snapshot",
                             "--snapshot-delay", "0", "--width", "40", "--height", "12", "--color-mode", "none",
                             "--splash-screen=false"], cwd=tmp_path, env=test_environment,
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert result.stdout.strip(), "Snapshot should contain a rendered frame"
