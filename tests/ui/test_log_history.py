"""Verify C log routes retain history independently of UI and terminal output."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import ctypes as C
import os
import re
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
assert lib.options_state_init() == 0
lib.options_set_int.argtypes = [C.c_char_p, C.c_int]
assert lib.options_set_int(b"color_mode", 3) == 0
C.c_bool.in_dll(lib, "g_color_flag_passed").value = True
C.c_bool.in_dll(lib, "g_color_flag_value").value = True
with tempfile.TemporaryDirectory() as root:
    assert lib.asciichat_shared_init(str(Path(root) / "app.log").encode(), True, True) == 0
    try:
        arguments = [b"log-history-test", b"--color=true", b"--color-mode", b"truecolor", b"mirror", b"--audio=false", b"--splash-screen=false"]
        argv = (C.c_char_p * len(arguments))(*arguments)
        lib.options_init.argtypes = [C.c_int, C.POINTER(C.c_char_p)]
        assert lib.options_init(len(arguments), argv) == 0
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
        frame = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", lib.ui_session_tick(s, 500000000))
        assert b"Video chat in your terminal" in frame
        assert b"log_file_msg" not in frame and b"/: search logs" not in frame
        assert lib.ui_session_input(s, ord("]")) == 9
        frame = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", lib.ui_session_tick(s, 1000000000))
        assert b"log_file_msg" in frame and b"/: search logs" in frame
        lib.ui_session_input(s, ord("]"))
        frame = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", lib.ui_session_tick(s, 1500000000))
        assert b"Video chat in your terminal" in frame and b"log_file_msg" not in frame
        lib.ui_session_destroy(s)
        # Detaching/destroying an overlay must not stop capture.
        lib.log_file_msg(b"after overlay destruction")
        assert lib.session_log_buffer_get_recent(lib.log_get_history_buffer(), entries, 1) == 1
        assert entries[0].message == b"after overlay destruction"
        # Exercise the real row renderer with explicit ANSI colors and wrapped matches.
        import pyte
        class Size(C.Structure):
            _fields_ = [("rows", C.c_int), ("cols", C.c_int)]
        Header = C.CFUNCTYPE(None, C.c_void_p, Size, C.c_void_p)
        class Config(C.Structure):
            _fields_ = [("screen", C.c_int), ("fd", C.c_int), ("data_size", C.c_size_t),
                        ("minimum_cols", C.c_int), ("header_lines", C.c_int),
                        ("header", Header), ("data", C.c_void_p), ("hide_cursor", C.c_bool),
                        ("show_logs", C.c_bool)]
        lib.frame_buffer_create.argtypes = [C.c_int, C.c_int]
        lib.frame_buffer_create.restype = C.c_void_p
        lib.frame_buffer_append.argtypes = [C.c_void_p, C.c_char_p, C.c_size_t]
        lib.frame_buffer_get_content.argtypes = [C.c_void_p]
        lib.frame_buffer_get_content.restype = C.c_char_p
        lib.frame_buffer_destroy.argtypes = [C.c_void_p]
        lib.session_log_buffer_create.restype = C.c_void_p
        lib.session_log_buffer_append.argtypes = [C.c_void_p, C.c_char_p]
        lib.session_log_buffer_destroy.argtypes = [C.c_void_p]
        lib.terminal_screen_compose.argtypes = [C.c_void_p, Size, C.POINTER(Config), C.c_void_p]
        @Header
        def header(frame, size, context):
            lib.frame_buffer_append(frame, b"header\n", 7)
        config = Config(0, 2, 0, 1, 1, header, None, False, True)
        logs = lib.session_log_buffer_create()
        message = "é [WARN] abcdefghijklmnopqrstuvwxyz"
        lib.session_log_buffer_append(logs, ("\x1b[33m" + message + "\x1b[0m").encode())
        def render():
            frame = lib.frame_buffer_create(10, 16)
            lib.terminal_screen_compose(frame, Size(10, 16), C.byref(config), logs)
            screen = pyte.Screen(16, 10)
            pyte.Stream(screen).feed(lib.frame_buffer_get_content(frame).decode())
            lib.frame_buffer_destroy(frame)
            return screen
        assert lib.log_search_init() == 0
        for pattern in (None, "WARN", "/WARN/g", "WARN.*abcdefghijk"):
            if pattern:
                lib.log_search_enter_mode()
                for key in pattern:
                    assert lib.log_search_handle_key(ord(key)) == 0
                lib.log_search_exit_mode(True)
            screen = render()
            cells = [screen.buffer[row][col] for row in range(1, 4) for col in range(16)]
            visible = [cell for cell in cells if cell.data != " "]
            assert visible, "Log renderer returned no text"
            assert all(cell.fg == "brown" for cell in visible), (pattern, visible)
            highlighted = "".join(cell.data for cell in cells if cell.bg != "default")
            assert highlighted == ("" if pattern is None else "WARN] abcdefghijk" if pattern == "WARN.*abcdefghijk" else "WARN"), (pattern, highlighted)
            if pattern:
                lib.log_search_exit_mode(False)
        # Render the actual viewer popup and inspect its terminal cell rows.
        popup = lib.ui_session_create()
        lib.ui_session_capture_logs(popup)
        lib.ui_session_resize(popup, 100, 30)
        lib.ui_session_input(popup, ord("]"))
        lib.ui_session_input(popup, ord("?"))
        popup_screen = pyte.Screen(100, 30)
        pyte.Stream(popup_screen).feed(lib.ui_session_tick(popup, 2000000000).decode())
        popup_rows = popup_screen.display
        title_row = next(i for i, row in enumerate(popup_rows) if "Log Viewer Keyboard Controls" in row)
        binding_row = next(i for i, row in enumerate(popup_rows) if "newer line" in row)
        assert binding_row == title_row + 2, popup_rows
        lib.ui_session_destroy(popup)
        lib.session_log_buffer_destroy(logs)

        lib.log_init_colors()
        # Numeric binding characters use the same key color as Esc.
        lib.ui_session_help_render.argtypes = [Size, C.c_bool, C.c_bool, C.c_void_p]
        lib.ui_session_help_render.restype = C.c_void_p
        lib.ui_session_help_free.argtypes = [C.c_void_p]
        minimum = Size()
        help_text = lib.ui_session_help_render(Size(45, 140), True, False, C.byref(minimum))
        assert help_text
        help_screen = pyte.Screen(140, 45)
        try:
            pyte.Stream(help_screen).feed(C.string_at(help_text).decode())
        finally:
            lib.ui_session_help_free(help_text)
        def binding_colors(binding):
            row = next(i for i, text in enumerate(help_screen.display) if binding in text and (binding != "0 " or "C code sync" in text))
            col = help_screen.display[row].index(binding)
            return [help_screen.buffer[row][col + i].fg for i in range(len(binding))]
        key_color = binding_colors("Esc")[0]
        assert key_color != "default", "Help key colors were not enabled"
        assert binding_colors("F12") == [key_color] * 3
        if any("C code sync" in row for row in help_screen.display):
            assert binding_colors("0 ")[:1] == [key_color]
        assert binding_colors("(1)")[1] == key_color
        # A worker's first formatted record must resolve its registered name on Windows too.
        lib.log_template_parse.argtypes = [C.c_char_p, C.c_bool]
        lib.log_template_parse.restype = C.c_void_p
        lib.log_template_free.argtypes = [C.c_void_p]
        lib.thread_current_id.restype = C.c_uint64
        lib.log_template_apply.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_int,
            C.c_char_p, C.c_char_p, C.c_int, C.c_char_p, C.c_uint64, C.c_char_p, C.c_bool, C.c_uint64]
        Worker = C.CFUNCTYPE(C.c_void_p, C.c_void_p)
        observed = []
        debug_build = hasattr(lib, "debug_free")
        @Worker
        def worker(context):
            template = lib.log_template_parse(b"%tname", False)
            output = C.create_string_buffer(256)
            lib.log_template_apply(template, output, len(output), 2, b"", b"test", 1,
                                   b"worker", lib.thread_current_id(), b"", False, 0)
            # Release builds omit the named-object registry and print the thread ID.
            tid = lib.thread_current_id()
            expected = b"log-regression.0" if debug_build else str(tid).encode()
            observed.append((output.value, expected))
            lib.log_template_free(template)
            return None
        lib.acthread_create.argtypes = [C.c_void_p, C.c_char_p, Worker, C.c_void_p]
        lib.thread_join.argtypes = [C.c_void_p, C.c_void_p]
        thread = C.c_void_p()
        assert lib.acthread_create(C.byref(thread), b"log-regression", worker, None) == 0
        assert lib.thread_join(C.byref(thread), None) == 0
        assert len(observed) == 1 and observed[0][0] == observed[0][1], observed
    finally:
        lib.log_search_destroy()
        lib.asciichat_shared_destroy()
print("PASS colors, exact UTF-8 search spans across wraps, help spacing, first-record thread name; C history: plain/stdout/stderr/file-only routes with console disabled, latest tail, overlay destruction")
