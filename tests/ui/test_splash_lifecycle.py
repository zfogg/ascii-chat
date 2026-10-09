"""Exercise production splash timing and shutdown through the shared library.

Usage: python tests/ui/test_splash_lifecycle.py build/bin/asciichat.dll
Each case runs in a subprocess so a blocked writer cannot hang the test runner.
"""
import ctypes as c
import os
from pathlib import Path
import subprocess
import sys
import threading
import time


def run_case(library, case):
    """Start a real splash, then measure its completion or shutdown wait."""
    dll_directory = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
    lib = c.CDLL(str(library))
    args = [b"ascii-chat", b"mirror", b"--width", b"80", b"--height", b"24", b"--splash-screen=true"]
    if case == "snapshot":
        args += [b"--snapshot", b"--snapshot-delay", b"0"]
    argv = (c.c_char_p * len(args))(*args)
    assert lib.options_init(len(args), argv) == 0
    lib.log_set_terminal_output(False)
    lib.splash_is_running.restype = c.c_bool

    if case == "cleanup-during-render":
        class Size(c.Structure):
            _fields_ = [("rows", c.c_int), ("cols", c.c_int)]

        header_fn = c.CFUNCTYPE(None, c.c_void_p, Size, c.c_void_p)

        class Config(c.Structure):
            _fields_ = [("lines", c.c_int), ("header", header_fn), ("user", c.c_void_p),
                        ("hide_cursor", c.c_bool), ("show_logs", c.c_bool)]

        entered, release = threading.Event(), threading.Event()
        lib.frame_buffer_append.argtypes = [c.c_void_p, c.c_char_p, c.c_size_t]

        @header_fn
        def header(buf, size, user):
            entered.set()
            release.wait(3)
            # The renderer lent this buffer to the callback. Cleanup must not
            # invalidate it while the callback is still composing the frame.
            lib.frame_buffer_append(buf, b"lifetime-ok", 11)

        config = Config(1, header, None, False, False)
        worker = threading.Thread(target=lambda: lib.terminal_screen_render(c.byref(config)))
        worker.start()
        assert entered.wait(2)
        try:
            start = time.monotonic()
            lib.terminal_screen_cleanup()
            lib.terminal_screen_cleanup()
            assert time.monotonic() - start < .25
        finally:
            release.set()
        worker.join(3)
        assert not worker.is_alive()
        lib.terminal_screen_cleanup()
        # A subsequent render must be able to allocate and own a fresh buffer.
        lib.terminal_screen_render(c.byref(config))
        lib.terminal_screen_cleanup()
        print("PASS: cleanup defers buffer destruction until active rendering finishes")
        return

    if case == "snapshot":
        lib.splash_intro_start(None)
        assert lib.splash_is_running(), "Expected an explicitly enabled splash"
        time.sleep(.05)
        start = time.monotonic()
        lib.splash_intro_done()
        lib.splash_wait_for_animation()
        elapsed = time.monotonic() - start
        assert elapsed < .75, f"Immediate snapshot waited {elapsed:.3f}s for splash"
        assert not lib.splash_is_running()
        print(f"PASS: immediate snapshot handoff ({elapsed:.3f}s)")
        return

    # Redirect the library's stderr to an undrained pipe, as in a subprocess
    # whose parent has stopped consuming output. Keep stdout available for results.
    read_fd, write_fd = os.pipe()
    if sys.platform == "linux":
        import fcntl
        fcntl.fcntl(write_fd, fcntl.F_SETPIPE_SZ, 4096)
    saved_stderr = os.dup(2)
    stop = threading.Event()
    callback = c.CFUNCTYPE(c.c_bool)(stop.is_set)
    lib.shutdown_register_callback(callback)
    os.dup2(write_fd, 2)
    try:
        lib.splash_intro_start(None)
        time.sleep(.5)
        assert lib.splash_is_running()
        lib.splash_intro_done()
        # Request shutdown after the normal handoff has already entered its wait.
        timer = threading.Timer(.1, stop.set)
        timer.start()
        start = time.monotonic()
        lib.splash_wait_for_animation()
        elapsed = time.monotonic() - start
        timer.join()
        assert elapsed < .75, f"Shutdown waited {elapsed:.3f}s for blocked splash"
        assert lib.splash_is_running(), "Expected the writer to remain blocked in the full pipe"

        # Shared teardown must return without freeing the active writer's buffer.
        start = time.monotonic()
        lib.terminal_screen_cleanup()
        lib.terminal_screen_cleanup()  # Repeated cleanup must also be safe.
        assert time.monotonic() - start < .25
        assert lib.splash_is_running()

        def drain():
            """Release the writer so the test can join it before unloading the DLL."""
            while os.read(read_fd, 65536):
                pass

        reader = threading.Thread(target=drain, daemon=True)
        reader.start()
        deadline = time.monotonic() + 3
        while lib.splash_is_running() and time.monotonic() < deadline:
            time.sleep(.01)
        assert not lib.splash_is_running(), "Writer did not stop after draining output"
        lib.splash_wait_for_animation()
        lib.terminal_screen_cleanup()
    finally:
        os.dup2(saved_stderr, 2)
        os.close(saved_stderr)
        os.close(write_fd)
    reader.join(timeout=3)
    assert not reader.is_alive()
    os.close(read_fd)
    print(f"PASS: shutdown during blocked splash handoff ({elapsed:.3f}s)")


if __name__ == "__main__":
    library = Path(sys.argv[1]).resolve()
    if len(sys.argv) == 3:
        run_case(library, sys.argv[2])
    else:
        for case in ("snapshot", "blocked-pipe", "cleanup-during-render"):
            result = subprocess.run([sys.executable, __file__, str(library), case],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=8)
            assert result.returncode == 0, result.stdout + result.stderr
            print("PASS: " + result.stdout.split("PASS:", 1)[1].strip())
