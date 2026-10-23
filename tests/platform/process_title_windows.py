#!/usr/bin/env python3
"""Verify process naming preserves terminal titles in a hidden Windows console.

Usage: python tests/platform/process_title_windows.py [build/bin/asciichat.dll]
Also checks redirected terminal calls produce no OSC title output.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import ctypes
import os
from pathlib import Path
import subprocess
import sys


def worker(path):
    with os.add_dll_directory(str(path.parent)):
        library = ctypes.CDLL(str(path))
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    library.platform_process_title_set.argtypes = [ctypes.c_char_p]
    library.platform_process_title_set.restype = ctypes.c_int
    library.platform_process_title_set_args.argtypes = [ctypes.c_char_p, ctypes.c_int,
                                                       ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]
    library.platform_process_title_set_args.restype = ctypes.c_int
    library.terminal_set_title.argtypes = [ctypes.c_char_p]
    library.terminal_set_title.restype = ctypes.c_int
    kernel.GetConsoleTitleA.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    kernel.GetConsoleTitleA.restype = ctypes.c_uint
    title_buffer = ctypes.create_string_buffer(32768)
    original = b"terminal title owned by the UI controller"
    assert kernel.SetConsoleTitleA(original)
    for mode in ("server", "client", "mirror", "discovery-service", "discovery"):
        title = f"ascii-chat: {mode} mode".encode()
        assert library.platform_process_title_set(title) == 0
        assert kernel.GetConsoleTitleA(title_buffer, len(title_buffer)) == len(original)
        assert title_buffer.value == original
        print(f"PASS: process title preserves console title for {mode}")
    cases = [
        ([b"ascii-chat", b"server", b"--password", b"secret"], 1),
        ([b"ascii-chat", b"--log-file", b"server", b"server"], 3),
        ([b"ascii-chat", b"server"], 1),
        ([b"ascii-chat", b"blue-mountain-tiger"], -1),
    ]
    for arguments, mode_index in cases:
        argv = (ctypes.c_char_p * len(arguments))(*arguments)
        assert library.platform_process_title_set_args(b"server", len(arguments), argv, mode_index) == 0
        kernel.GetConsoleTitleA(title_buffer, len(title_buffer))
        assert title_buffer.value == original, title_buffer.value
        assert list(argv) == arguments
    print("PASS: process argument titles preserve console title and argv")
    assert library.platform_process_title_set(None) != 0
    assert library.terminal_set_title(None) != 0
    assert kernel.FreeConsole()
    assert library.terminal_set_title(b"detached console") == 0
    print("PASS: NULL rejection and detached-console no-op")


if __name__ == "__main__":
    if "--worker" in sys.argv:
        worker(Path(sys.argv[1]).resolve())
    else:
        dll = Path(sys.argv[1] if len(sys.argv) > 1 else "build/bin/asciichat.dll").resolve()
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        result = subprocess.run([sys.executable, __file__, str(dll), "--worker"],
                                creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup,
                                capture_output=True, timeout=30)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert b"\x1b]0;" not in result.stdout, result.stdout
        assert b"\x1b]2;" not in result.stdout, result.stdout
        print(result.stdout.decode(), end="")
