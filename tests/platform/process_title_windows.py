#!/usr/bin/env python3
"""Read back all mode titles from the built DLL in a hidden Windows console.

Usage: python tests/platform/process_title_windows.py [build/bin/asciichat.dll]
Also checks the real terminal wrapper's detached-console failure path produces
no OSC title sequence on redirected stdout.
"""

import ctypes
import os
from pathlib import Path
import subprocess
import sys
import socket
import tempfile
import time


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
    for mode in ("server", "client", "mirror", "discovery-service", "discovery"):
        title = f"ascii-chat: {mode} mode".encode()
        assert library.platform_process_title_set(title) == 0
        assert kernel.GetConsoleTitleA(title_buffer, len(title_buffer)) == len(title)
        assert title_buffer.value == title
        print(f"PASS: {title.decode()}")
    cases = [
        ([b"ascii-chat", b"server", b"--port", b"27224", b"--password", b"secret-split",
          b"--password=secret-inline", b"--websocket-tls-key-password=tls-secret",
          b"--turn-credential", b"turn-secret", b"--turn-secret=shared-secret", b"--key", b"/keys/id_ed25519"],
         1, b"ascii-chat: server mode - --port 27224 --password [redacted] --password=[redacted] "
         b"--websocket-tls-key-password=[redacted] --turn-credential [redacted] --turn-secret=[redacted] "
         b"--key /keys/id_ed25519"),
        ([b"ascii-chat", b"--log-file", b"server", b"server", b"--key", b"-----BEGIN PRIVATE KEY-----"],
         3, b"ascii-chat: server mode - --log-file server --key [redacted]"),
        ([b"ascii-chat", b"server"], 1, b"ascii-chat: server mode"),
        ([b"ascii-chat", b"blue-mountain-tiger"], -1, b"ascii-chat: server mode - blue-mountain-tiger"),
    ]
    for arguments, mode_index, expected in cases:
        argv = (ctypes.c_char_p * len(arguments))(*arguments)
        assert library.platform_process_title_set_args(b"server", len(arguments), argv, mode_index) == 0
        kernel.GetConsoleTitleA(title_buffer, len(title_buffer))
        assert title_buffer.value == expected, title_buffer.value
        assert list(argv) == arguments
    print("PASS: argument formatting/redaction, mode index, no-argument separator, preserved argv")
    assert library.terminal_set_title(None) != 0
    assert kernel.FreeConsole()
    assert library.terminal_set_title(b"detached console") != 0
    print("PASS: NULL and detached-console failures")


def application_smoke(executable):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.FreeConsole()
    title_buffer = ctypes.create_string_buffer(32768)

    def unused_port():
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            return str(listener.getsockname()[1])

    with tempfile.TemporaryDirectory(prefix="ascii-chat-title-") as temporary:
        directory = Path(temporary)
        media = ["--test-pattern", "--audio=false", "--splash-screen=false"]
        discovery = media + ["--discovery-service", "127.0.0.1",
                             "--discovery-service-port", unused_port()]
        cases = [
            ("server", ["server", "127.0.0.1", "--port", unused_port(),
                        "--websocket-port", unused_port(), "--status-screen=false", "--password", "split-secret"]),
            ("client", ["client", "127.0.0.1", "--port", unused_port(), "--password=inline-secret"] + media),
            ("mirror", ["mirror"] + media),
            ("discovery-service", ["discovery-service", "--port", unused_port(),
                                   "--websocket-port", unused_port(),
                                   "--database", str(directory / "discovery.db"),
                                   "--status-screen=false"]),
            ("discovery", discovery),
        ]
        for index, (mode, arguments) in enumerate(cases):
            with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
                with subprocess.Popen(
                    [str(executable), "--log-file", str(directory / f"{index}.log")] + arguments,
                    cwd=directory, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                ) as child:
                    observed = b""
                    try:
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline and child.poll() is None:
                            if kernel.AttachConsole(child.pid):
                                kernel.GetConsoleTitleA(title_buffer, len(title_buffer))
                                observed = title_buffer.value
                                kernel.FreeConsole()
                            if observed.startswith(f"ascii-chat: {mode} mode - ".encode()):
                                break
                            time.sleep(0.01)
                        stderr.seek(0)
                        assert observed.startswith(f"ascii-chat: {mode} mode - ".encode()), (
                            arguments, observed, stderr.read().decode(errors="replace"))
                        assert b"split-secret" not in observed and b"inline-secret" not in observed
                        if mode == "server":
                            assert b"--password [redacted]" in observed
                        if mode == "client":
                            assert b"--password=[redacted]" in observed
                        print(f"PASS: application dispatch {arguments[0]} -> {observed.decode()}")
                    finally:
                        if child.poll() is None:
                            child.terminate()
                            child.wait(timeout=10)
        snapshot = subprocess.run(
            [str(executable), "--log-file", str(directory / "snapshot.log"), "mirror"]
            + media + ["--snapshot", "--snapshot-delay", "0", "--width", "40",
                       "--height", "12", "--color-mode", "none", "--strip-ansi"],
            cwd=directory, stdin=subprocess.DEVNULL, capture_output=True, timeout=30,
        )
        assert snapshot.returncode == 0, snapshot.stderr
        assert snapshot.stdout and b"\x1b]0;" not in snapshot.stdout, snapshot.stdout
        print("PASS: piped mirror snapshot exits successfully without OSC title output")


if __name__ == "__main__":
    if "--worker" in sys.argv:
        worker(Path(sys.argv[1]).resolve())
    else:
        dll = Path(sys.argv[1] if len(sys.argv) > 1 else "build/bin/asciichat.dll").resolve()
        result = subprocess.run([sys.executable, __file__, str(dll), "--worker"],
                                creationflags=subprocess.CREATE_NO_WINDOW,
                                capture_output=True, timeout=30)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert b"\x1b]0;" not in result.stdout, result.stdout
        print(result.stdout.decode(), end="")
        application_smoke(dll.parent / "ascii-chat.exe")
