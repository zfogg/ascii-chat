"""Verify C log routes retain history independently of UI and terminal output."""
import ctypes as C
import os
from pathlib import Path
import sys
import tempfile

library = Path(sys.argv[1]).resolve()
dll_directory = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
lib = C.CDLL(str(library))
lib.asciichat_shared_init.argtypes = [C.c_char_p, C.c_bool, C.c_bool]
lib.log_get_history_buffer.restype = C.c_void_p
lib.ui_session_create.restype = C.c_void_p
lib.ui_session_destroy.argtypes = [C.c_void_p]
lib.ui_session_capture_logs.argtypes = [C.c_void_p]
lib.ui_session_input.argtypes = [C.c_void_p, C.c_int]
lib.ui_session_resize.argtypes = [C.c_void_p, C.c_int, C.c_int]
lib.ui_session_tick.argtypes = [C.c_void_p, C.c_uint64]
lib.ui_session_tick.restype = C.c_char_p
lib.ui_session_loading.argtypes = [C.c_void_p, C.c_bool, C.c_char_p]
lib.log_set_terminal_output.argtypes = [C.c_bool]
class Entry(C.Structure):
    _fields_ = [("message", C.c_char * 1024), ("sequence", C.c_uint64)]
lib.session_log_buffer_get_recent.argtypes = [C.c_void_p, C.POINTER(Entry), C.c_size_t]
lib.session_log_buffer_get_recent.restype = C.c_size_t
with tempfile.TemporaryDirectory() as root:
    assert lib.asciichat_shared_init(str(Path(root) / "app.log").encode(), True, True) == 0
    lib.log_set_terminal_output(False)
    for function in ("log_plain_msg", "log_plain_stderr_msg", "log_plain_stdout_msg", "log_file_msg"):
        getattr(lib, function)(function.encode())
    entries = (Entry * 4)()
    assert lib.session_log_buffer_get_recent(lib.log_get_history_buffer(), entries, 4) == 4
    assert [e.message.decode() for e in entries] == ["log_plain_msg", "log_plain_stderr_msg", "log_plain_stdout_msg", "log_file_msg"]
    s = lib.ui_session_create()
    lib.ui_session_capture_logs(s)
    lib.ui_session_resize(s, 140, 30)
    lib.ui_session_loading(s, True, b"Starting client")
    frame = lib.ui_session_tick(s, 500000000)
    assert b"Video chat in your terminal" in frame
    assert b"log_file_msg" not in frame and b"/: search logs" not in frame
    assert lib.ui_session_input(s, ord("]")) == 9
    frame = lib.ui_session_tick(s, 1000000000)
    assert b"log_file_msg" in frame and b"/: search logs" in frame
    lib.ui_session_input(s, ord("]"))
    frame = lib.ui_session_tick(s, 1500000000)
    assert b"Video chat in your terminal" in frame and b"log_file_msg" not in frame
    lib.ui_session_destroy(s)
    # Detaching/destroying an overlay must not stop capture.
    lib.log_file_msg(b"after overlay destruction")
    assert lib.session_log_buffer_get_recent(lib.log_get_history_buffer(), entries, 1) == 1
    assert entries[0].message == b"after overlay destruction"
    lib.asciichat_shared_destroy()
print("PASS C history: plain/stdout/stderr/file-only routes with console disabled, latest tail, overlay destruction")
