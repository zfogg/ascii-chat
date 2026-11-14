"""Regression coverage for Windows Ctrl-C telemetry shutdown."""

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest


def _send_ctrl_c(pid: int) -> None:
    helper = r"""
import ctypes
import sys
from ctypes import wintypes

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.FreeConsole()
target = int(sys.argv[1])
if not kernel.AttachConsole(target):
    raise ctypes.WinError(ctypes.get_last_error())
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 1
FILE_SHARE_WRITE = 2
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
handle = kernel.CreateFileW(
    "CONIN$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
    None, OPEN_EXISTING, 0, None)
if handle == INVALID_HANDLE_VALUE:
    raise ctypes.WinError(ctypes.get_last_error())
class KEY_EVENT_RECORD(ctypes.Structure):
    _fields_ = [("bKeyDown", wintypes.BOOL), ("wRepeatCount", wintypes.WORD), ("wVirtualKeyCode", wintypes.WORD), ("wVirtualScanCode", wintypes.WORD), ("UnicodeChar", wintypes.WCHAR), ("dwControlKeyState", wintypes.DWORD)]
class INPUT_RECORD(ctypes.Structure):
    _fields_ = [("EventType", wintypes.WORD), ("KeyEvent", KEY_EVENT_RECORD)]
events = (INPUT_RECORD * 2)(INPUT_RECORD(1, KEY_EVENT_RECORD(True, 1, 0x43, 0, "c", 0x0008)), INPUT_RECORD(1, KEY_EVENT_RECORD(False, 1, 0x43, 0, "c", 0x0008)))
written = wintypes.DWORD()
if not kernel.WriteConsoleInputW(handle, events, 2, ctypes.byref(written)) or written.value != 2:
    raise ctypes.WinError(ctypes.get_last_error())
kernel.CloseHandle(handle)
kernel.FreeConsole()
"""
    subprocess.run([sys.executable, "-c", helper, str(pid)], check=True, timeout=5)


@pytest.mark.terminal
@pytest.mark.parametrize("ctrl_c_count", [1, 2])
def test_mirror_ctrl_c_cancels_unreachable_telemetry(
    binary: Path, tmp_path: Path, ctrl_c_count: int
) -> None:
    log = tmp_path / "mirror-shutdown.log"
    command = [
        str(binary),
        "--splash-screen=false",
        "--no-check-update",
        "--log-file",
        str(log),
        "mirror",
        "--render-mode",
        "background",
        "--renderer",
        "glyph",
        "--fft",
    ]
    environment = os.environ.copy()
    environment.pop("TESTING", None)
    environment.pop("CRITERION_TEST", None)
    environment.update(
        ASCII_CHAT_OTEL_ENABLED_TELEMETRY="traces,logs,metrics",
        ASCII_CHAT_OTEL_ENDPOINT="http://192.0.2.1:9/v1/traces",
        ASCII_CHAT_OTEL_TIMEOUT="10000",
        ASCII_CHAT_DISABLE_FIRST_USE="1",
    )
    process = subprocess.Popen(
        command,
        cwd=tmp_path,
        env=environment,
        creationflags=subprocess.CREATE_NEW_CONSOLE
        | subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            startup_log = (
                log.read_text(encoding="utf-8", errors="replace")
                if log.exists()
                else ""
            )
            if "Mirror audio started on demand" in startup_log:
                break
            time.sleep(0.1)
        started = time.monotonic()
        for _ in range(ctrl_c_count):
            _send_ctrl_c(process.pid)
            time.sleep(0.05)
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            text = log.read_text(encoding="utf-8", errors="replace")
            pytest.fail(
                "ascii-chat did not exit within 1 second after Ctrl-C\n" + text[-4000:]
            )
        elapsed = time.monotonic() - started
        text = log.read_text(encoding="utf-8", errors="replace")
        assert process.returncode in {0, 1, 2, 130}, text[-4000:]
        assert elapsed <= 1.0, text[-4000:]
        assert "Telemetry worker did not stop within its deadline" not in text
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
