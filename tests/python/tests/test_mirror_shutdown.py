"""Regression coverage for Windows Ctrl-C telemetry shutdown."""

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest


def _send_ctrl_c(pid: int) -> None:
    helper = r'''
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
    _fields_ = [
        ("bKeyDown", wintypes.BOOL),
        ("wRepeatCount", wintypes.WORD),
        ("wVirtualKeyCode", wintypes.WORD),
        ("wVirtualScanCode", wintypes.WORD),
        ("UnicodeChar", wintypes.WCHAR),
        ("dwControlKeyState", wintypes.DWORD),
    ]

class INPUT_RECORD(ctypes.Structure):
    _fields_ = [("EventType", wintypes.WORD), ("KeyEvent", KEY_EVENT_RECORD)]

LEFT_CTRL_PRESSED = 0x0008
VK_C = 0x43
events = (INPUT_RECORD * 2)(
    INPUT_RECORD(1, KEY_EVENT_RECORD(True, 1, VK_C, 0, "c", LEFT_CTRL_PRESSED)),
    INPUT_RECORD(1, KEY_EVENT_RECORD(False, 1, VK_C, 0, "c", LEFT_CTRL_PRESSED)),
)
written = wintypes.DWORD()
if not kernel.WriteConsoleInputW(handle, events, 2, ctypes.byref(written)) or written.value != 2:
    raise ctypes.WinError(ctypes.get_last_error())
kernel.CloseHandle(handle)
kernel.FreeConsole()
'''
    subprocess.run([sys.executable, "-c", helper, str(pid)], check=True, timeout=5)


@pytest.mark.terminal
def test_mirror_ctrl_c_cancels_unreachable_telemetry(binary: Path, tmp_path: Path) -> None:
    log = tmp_path / "mirror-shutdown.log"
    command = [
        str(binary),
        "--splash-screen=false",
        "--no-check-update",
        "--log-file",
        str(log),
        "mirror",
        "--test-pattern",
        "--render-mode",
        "background",
        "--renderer",
        "glyph",
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
        creationflags=subprocess.CREATE_NEW_CONSOLE | subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    try:
        time.sleep(3)
        startup_log = log.read_text(encoding="utf-8", errors="replace")
        assert "Using test pattern" in startup_log, startup_log[-4000:]
        assert "Telemetry enabled:" in startup_log, startup_log[-4000:]
        started = time.monotonic()
        _send_ctrl_c(process.pid)
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            text = log.read_text(encoding="utf-8", errors="replace")
            pytest.fail("ascii-chat did not exit within 1 second after Ctrl-C\n" + text[-4000:])
        elapsed = time.monotonic() - started
        text = log.read_text(encoding="utf-8", errors="replace")
        assert process.returncode in {0, 1, 2}, text[-4000:]
        assert elapsed <= 1.0, text[-4000:]
        assert "Telemetry worker did not stop within its deadline" not in text
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
