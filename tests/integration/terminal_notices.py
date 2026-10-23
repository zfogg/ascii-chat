"""Notice layout, logging, security decisions and real PTY/ConPTY presentation.

python tests/integration/terminal_notices.py --library build/bin/asciichat.dll
Requires the terminal_ui.py dependencies (pyte and pywinpty or pexpect).
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import ctypes as C
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from terminal_ui import Terminal, CaptureScreen, CaptureStream


class Size(C.Structure):
    _fields_ = [("rows", C.c_int), ("cols", C.c_int)]


def probe(library, kind, log):
    if kind == "dynamic-colors":
        os.environ.pop("TERM_BACKGROUND", None)
        os.environ["COLORTERM"] = "truecolor"
    dll_directory = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
    native = C.CDLL(str(library))
    native.asciichat_shared_init.argtypes = [C.c_char_p, C.c_bool, C.c_bool]
    assert native.asciichat_shared_init(str(log).encode(), True, True) == 0
    words = [b"notice-probe", b"--no-check-update", b"--color", b"false" if kind in ("log", "reject", "layout") else b"true", b"mirror"]
    if kind == "dynamic-colors":
        words[-1:-1] = [b"--color-mode", b"truecolor", b"--color-scheme", b"pastel"]
    if kind == "announce-quiet":
        words.insert(-1, b"--quiet")
    argv = (C.c_char_p * (len(words) + 1))(*words, None)
    assert native.options_init(len(words), argv) == 0
    native.log_set_terminal_output(True)
    native.ui_notice_present.argtypes = [C.c_int, C.c_char_p]
    native.ui_notice_log.argtypes = [C.c_int, C.c_char_p, C.c_int, C.c_char_p, C.c_char_p, C.c_char_p]
    native.ui_notice_confirm.argtypes = [C.c_int, C.c_char_p, C.c_uint]
    native.ui_notice_confirm.restype = C.c_bool
    native.ui_controller_present.argtypes = [C.c_int, C.c_int, Size, C.c_char_p, C.c_size_t]
    native.ui_controller_finish.argtypes = [C.c_int, C.c_char_p, C.c_size_t]
    if kind == "dynamic-colors":
        assert native.keyboard_init() == 0
        native.log_level_color.argtypes = [C.c_int]
        native.log_level_color.restype = C.c_char_p
        native.log_get_foreground_color.restype = C.c_char_p
        for step in range(5):
            while not log.with_suffix(f".start{step}").exists():
                time.sleep(.01)
            # Exercise automatic refresh without a notice forcing a synchronous query.
            keys = []
            deadline = time.monotonic() + .65
            while time.monotonic() < deadline:
                native.log_level_color(2)
                while True:
                    key = native.keyboard_read_nonblocking()
                    if not key: break
                    keys.append(key)
                time.sleep(.005)
            pre_colors = [(native.log_get_foreground_color() if role is None else native.log_level_color(role)).decode()
                          for role in (None, 3, 4, 5, 2)]
            codes = []
            for severity, role in enumerate((None, 3, 4, 5, 2)):
                assert native.ui_notice_present(severity, f"THEME STEP {step}\nTheme changed".encode()) == 0
                native.frame_buffer_create.argtypes = [C.c_int, C.c_int]
                native.frame_buffer_create.restype = C.c_void_p
                native.frame_buffer_get_content.argtypes = [C.c_void_p]
                native.frame_buffer_get_content.restype = C.c_char_p
                native.frame_buffer_destroy.argtypes = [C.c_void_p]
                native.ui_notice_render.argtypes = [C.c_void_p, C.c_char_p, C.c_int, C.c_int, C.c_bool, C.c_bool]
                buffer = native.frame_buffer_create(0, 0)
                try:
                    native.ui_notice_render(buffer, b"COLOR", severity, 80, True, True)
                    code = re.match(rb"\x1b\[1m(\x1b\[[0-9;]*m)", native.frame_buffer_get_content(buffer)).group(1)
                    codes.append(code.decode())
                    logger_code = native.log_get_foreground_color() if role is None else native.log_level_color(role)
                    assert logger_code == code, (step, severity, logger_code, code)
                finally:
                    native.frame_buffer_destroy(buffer)
            # Queries must preserve real shortcuts delivered beside OSC replies.
            for _ in range(100):
                key = native.keyboard_read_nonblocking()
                if not key: break
                keys.append(key)
            log.with_suffix(f".result{step}").write_text(json.dumps({"colors": codes, "pre_colors": pre_colors, "keys": keys}))
        native.keyboard_destroy()
    elif kind == "palette-nonblocking":
        assert native.keyboard_init() == 0
        native.frame_buffer_create.argtypes = [C.c_int, C.c_int]
        native.frame_buffer_create.restype = C.c_void_p
        native.frame_buffer_destroy.argtypes = [C.c_void_p]
        native.ui_notice_render.argtypes = [C.c_void_p, C.c_char_p, C.c_int, C.c_int, C.c_bool, C.c_bool]
        buffer = native.frame_buffer_create(0, 0)
        native.log_level_color.argtypes = [C.c_int]
        native.log_level_color.restype = C.c_char_p
        native.log_get_color_array.restype = C.c_void_p
        native.log_get_foreground_color.restype = C.c_char_p
        durations = []
        for _ in range(150):
            started = time.perf_counter()
            for role in range(8):
                native.log_level_color(role)
            native.log_get_color_array()
            native.log_get_foreground_color()
            native.ui_notice_render(buffer, b"ASYNC NOTICE\nNo terminal reply", 1, 80, True, True)
            durations.append(time.perf_counter() - started)
            time.sleep(.005)
        # An unanswered OSC query used to impose a 100ms input wait.
        assert max(durations) < .08, max(durations)
        log.with_suffix(".latency").write_text(str(max(durations) * 1000))
        native.frame_buffer_destroy(buffer)
        native.keyboard_destroy()
        print(f"Unanswered-query maximum call: {max(durations) * 1000:.3f}ms", flush=True)
    elif kind.startswith("announce-"):
        native.ui_notice_announce.argtypes = [C.c_char_p, C.c_int, C.c_char_p, C.c_char_p, C.c_char_p]
        native.log_set_level(5)
        if kind == "announce-filter":
            native.grep_init.argtypes = [C.c_char_p]
            assert native.grep_init(b"NO MATCH") == 0
        if kind == "announce-json":
            native.log_set_json_output(2)
        for level in (3, 4, 5):
            native.log_set_level(level)
            for scope in (b"LAN only via mDNS", b"globally"):
                body = b"Session String: blue-mountain-tiger\nShare " + scope + b" to join:\n   ascii-chat blue-mountain-tiger"
                native.ui_notice_announce(b"notice-test", 1, b"probe", b"SESSION READY", body)
        native.ui_notice_log(0, b"notice-test", 1, b"probe", b"FILTERED INFO", b"Must stay filtered")
        if kind == "announce-json":
            native.log_set_json_output(-1)
    elif kind == "success-log":
        native.ui_notice_log(4, b"notice-test", 1, b"probe", b"OPERATION COMPLETE", b"Successfully finished")
    elif kind == "fatal-log":
        native.log_msg.argtypes = [C.c_int, C.c_char_p, C.c_int, C.c_char_p, C.c_char_p]
        assert native.ui_controller_present(1, 1, Size(2, 40), b"INITIAL MEDIA", 13) == 0
        native.log_msg(5, b"notice-test", 1, b"probe", b"LWS_CALLBACK_CLIENT_ESTABLISHED")
        updated = b"MEDIA AFTER FATAL LOG"
        assert native.ui_controller_present(1, 1, Size(2, 40), updated, len(updated)) == 0
        while not log.with_suffix(".finish").exists():
            time.sleep(.01)
    elif kind == "forced-exit":
        assert native.ui_controller_present(2, 1, Size(2, 40), b"SPLASH", 6) == 0
        native.log_lock_terminal()
        native.ui_notice_log(1, b"notice-test", 1, b"probe", b"NO SERVERS FOUND", b"Use an address to connect manually.")
        native.ui_controller_finish(2, b"", 0)
        native.platform_force_exit(1)
    elif kind == "restore-output":
        native.log_set_terminal_output(False)
        native.log_set_terminal_output(True)
        native.ui_notice_log(1, b"notice-test", 1, b"probe", b"MIRROR FAILED", b"Mirror mode failed")
    elif kind in ("layout", "palette"):
        native.frame_buffer_create.argtypes = [C.c_int, C.c_int]
        native.frame_buffer_create.restype = C.c_void_p
        native.frame_buffer_get_content.argtypes = [C.c_void_p]
        native.frame_buffer_get_content.restype = C.c_void_p
        native.frame_buffer_get_length.argtypes = [C.c_void_p]
        native.frame_buffer_get_length.restype = C.c_size_t
        native.frame_buffer_destroy.argtypes = [C.c_void_p]
        native.ui_notice_render.argtypes = [C.c_void_p, C.c_char_p, C.c_int, C.c_int, C.c_bool, C.c_bool]
        native.display_width.argtypes = [C.c_char_p]
        native.display_width.restype = C.c_int
        native.log_level_color.argtypes = [C.c_int]
        native.log_level_color.restype = C.c_char_p
        native.log_get_foreground_color.restype = C.c_char_p
        text = "SERVER KEY CHANGED\nSHA256:" + "0123456789abcdef" * 4 + "\n日本語 café\nEND"
        for width in (2, 7, 8, 20, 40, 80, 120):
            for unicode in (False, True):
                for severity, role in enumerate((None, 3, 4, 5, 2)):
                    buffer = native.frame_buffer_create(0, 0)
                    try:
                        rows = native.ui_notice_render(buffer, text.encode(), severity, width, unicode, True)
                        data = C.string_at(native.frame_buffer_get_content(buffer), native.frame_buffer_get_length(buffer)).decode()
                        expected_color = (native.log_get_foreground_color() if role is None else native.log_level_color(role)).decode()
                        assert data.startswith("\x1b[1m" + expected_color), data
                        plain = re.sub(r"\x1b\[[0-9;]*m", "", data)
                        assert len(plain.splitlines()) == rows
                        assert native.ui_notice_render(None, text.encode(), severity, width, unicode, False) == rows
                        lines = plain.splitlines()
                        if width >= 9:
                            assert all(native.display_width(line.encode()) <= width - 1 for line in lines)
                            content = "".join(line[2:-2].rstrip() for line in lines[1:-1] if not line.startswith(("╠", "+")))
                        else:
                            content = "".join(lines)
                        label = ("INFO", "WARNING", "ERROR", "FATAL", "SUCCESS")[severity]
                        assert content.startswith(label), (severity, width, content)
                        if width >= 20:
                            assert lines[1][2:-2].rstrip() == label, lines
                            assert lines[2].startswith("╠" if unicode else "+"), lines
                            assert lines[3][2:-2].strip().startswith("Server Key"), lines
                        assert "0123456789abcdef" * 4 in content
                        assert "日本語" in content and "End" in content
                    finally:
                        native.frame_buffer_destroy(buffer)
        class RGB(C.Structure):
            _fields_ = [("r", C.c_uint8), ("g", C.c_uint8), ("b", C.c_uint8)]
        class SchemePrefix(C.Structure):
            _fields_ = [("name", C.c_char * 64), ("description", C.c_char * 256),
                        ("dark", RGB * 8), ("has_light", C.c_bool), ("light", RGB * 8)]
        class Palette(C.Structure):
            _fields_ = [("codes", (C.c_char * 32) * 8), ("foreground", C.c_char * 32), ("background", C.c_int)]
        native.colorscheme_current_palette.argtypes = [C.POINTER(Palette), C.c_int]
        class Compiled(C.Structure):
            _fields_ = [("codes_16", C.c_char_p * 8), ("codes_256", C.c_char_p * 8),
                        ("codes_truecolor", C.c_char_p * 8), ("foreground_16", C.c_char * 16),
                        ("foreground_256", C.c_char * 24), ("foreground_truecolor", C.c_char * 32)]
        native.colorscheme_get_active_scheme.restype = C.c_void_p
        native.colorscheme_set_active_scheme.argtypes = [C.c_char_p]
        native.colorscheme_compile_scheme.argtypes = [C.c_void_p, C.c_int, C.c_int, C.POINTER(Compiled)]
        native.colorscheme_cleanup_compiled.argtypes = [C.POINTER(Compiled)]
        for name in (b"pastel", b"nord", b"solarized", b"dracula", b"gruvbox", b"monokai", b"base16-default"):
            assert native.colorscheme_set_active_scheme(name) == 0
            pointer = native.colorscheme_get_active_scheme()
            scheme = SchemePrefix.from_address(pointer)
            native.log_set_color_scheme.argtypes = [C.c_void_p]
            native.log_set_color_scheme(pointer)
            for background in (1, 2):
                compiled = Compiled()
                try:
                    assert native.colorscheme_compile_scheme(pointer, 3, background, C.byref(compiled)) == 0
                    palette = scheme.light if background == 1 and scheme.has_light else scheme.dark
                    def ansi(rgb):
                        return f"\x1b[38;2;{rgb.r};{rgb.g};{rgb.b}m".encode()
                    assert compiled.foreground_truecolor == ansi(palette[7])
                    # Use an explicit override to exercise every scheme and depth without a PTY.
                    previous_background = os.environ.get("TERM_BACKGROUND")
                    os.environ["TERM_BACKGROUND"] = "light" if background == 1 else "dark"
                    try:
                        for mode, expected_codes, expected_foreground in (
                            (0, [b""] * 8, b""),
                            (1, compiled.codes_16, compiled.foreground_16),
                            (2, compiled.codes_256, compiled.foreground_256),
                            (3, compiled.codes_truecolor, compiled.foreground_truecolor)):
                            live = Palette()
                            native.colorscheme_current_palette(C.byref(live), mode)
                            assert live.background == background
                            assert live.foreground == expected_foreground
                            assert [bytes(code).split(b"\0")[0] for code in live.codes] == list(expected_codes)
                    finally:
                        if previous_background is None:
                            os.environ.pop("TERM_BACKGROUND", None)
                        else:
                            os.environ["TERM_BACKGROUND"] = previous_background
                    if background == native.detect_terminal_background():
                        assert native.log_get_foreground_color() in (
                            b"", compiled.foreground_16, compiled.foreground_256, compiled.foreground_truecolor)
                        for role in (2, 3, 4, 5):
                            assert native.log_level_color(role) in (
                                b"", compiled.codes_16[role], compiled.codes_256[role], compiled.codes_truecolor[role])
                    assert re.fullmatch(rb"\x1b\[(?:3|9)[0-7]m", compiled.foreground_16)
                    assert re.fullmatch(rb"\x1b\[38;5;\d+m", compiled.foreground_256)
                    for role in (2, 3, 4, 5):
                        assert compiled.codes_truecolor[role] == ansi(palette[role])
                    assert compiled.codes_truecolor[7] == b"\x1b[0m"
                finally:
                    native.colorscheme_cleanup_compiled(C.byref(compiled))
        # Presentation case must not alter paths, URLs, fingerprints or code fields.
        buffer = native.frame_buffer_create(0, 0)
        try:
            payload = b"MEDIA FAILED\nopen C:\\Users\\Zach\\Demo.webm https://example.com/Case SHA256:aBc123 (code=21)"
            rows = native.ui_notice_render(buffer, payload, 1, 80, True, False)
            data = C.string_at(native.frame_buffer_get_content(buffer), native.frame_buffer_get_length(buffer)).decode()
            content = "".join(line[2:-2].rstrip() for line in data.splitlines()
                              if line.startswith("║"))
            assert "Media Failed" in content and "Open" in content
            for token in ("C:\\Users\\Zach\\Demo.webm", "https://example.com/Case", "SHA256:aBc123", "(code=21)"):
                assert token in content, (token, content)
            assert rows == len(data.splitlines())
        finally:
            native.frame_buffer_destroy(buffer)
        # Untrusted strings cannot clear the screen or emit OSC terminal commands.
        buffer = native.frame_buffer_create(0, 0)
        native.ui_notice_render(buffer, b"remote\x1b[2J\x07END", 2, 80, False, False)
        data = C.string_at(native.frame_buffer_get_content(buffer), native.frame_buffer_get_length(buffer))
        assert b"\x1b" not in data and b"\x07" not in data and b"END" in data
        native.frame_buffer_destroy(buffer)
    elif kind == "buffer":
        class Entry(C.Structure):
            _fields_ = [("message", C.c_char * 1024), ("sequence", C.c_uint64)]
        native.session_log_buffer_create.restype = C.c_void_p
        native.session_log_buffer_append.argtypes = [C.c_void_p, C.c_char_p]
        native.session_log_buffer_get_recent.argtypes = [C.c_void_p, C.POINTER(Entry), C.c_size_t]
        native.session_log_buffer_get_recent.restype = C.c_size_t
        native.session_log_buffer_destroy.argtypes = [C.c_void_p]
        buffer = native.session_log_buffer_create()
        text = "FIRST\n" + "界" * 1100 + "\nLAST"
        native.session_log_buffer_append(buffer, text.encode())
        entries = (Entry * 20)()
        count = native.session_log_buffer_get_recent(buffer, entries, 20)
        recovered = "".join(entries[i].message.decode() for i in range(count))
        assert recovered == text.replace("\n", "")
        native.session_log_buffer_destroy(buffer)
    elif kind.startswith("filter-header-"):
        native.grep_init.argtypes = [C.c_char_p]
        patterns = {
            "level": b"ERROR", "file": b"auth-handshake.c", "function": b"verify_peer",
            "custom": b"SECURITY:ERROR", "invert": b"/ERROR/I", "long": b"LAST-BYTE",
        }
        variant = kind.removeprefix("filter-header-")
        if variant == "custom":
            native.log_set_format.argtypes = [C.c_char_p, C.c_bool]
            assert native.log_set_format(b"SECURITY:%level %message", False) == 0
        assert native.grep_init(patterns[variant]) == 0
        body = b"Invalid peer signature" if variant != "long" else b"a" * 6000 + b"LAST-BYTE"
        native.ui_notice_log(2, b"auth-handshake.c", 42, b"verify_peer", b"AUTHENTICATION REJECTED", body)
        native.ui_notice_log(1, b"other.c", 10, b"other", b"UNRELATED WARNING", b"Other body")
    elif kind in ("filter", "json"):
        if kind == "filter":
            native.grep_init.argtypes = [C.c_char_p]
            assert native.grep_init(b"KEEP THIS NOTICE") == 0
        else:
            native.log_set_json_output(2)
        native.ui_notice_log(2, b"notice-test", 1, b"probe", b"KEEP THIS NOTICE", b"Complete body")
        native.ui_notice_log(1, b"notice-test", 1, b"probe", b"FILTERED NOTICE", b"Other body")
        if kind == "json":
            native.log_set_json_output(-1)
    elif kind == "log":
        for severity in range(4):
            native.ui_notice_log(severity, b"notice-test", 1, b"probe", b"LOG NOTICE", b"Complete fingerprint: SHA256:abc123")
        native.ui_notice_log(2, b"notice-test", 1, b"probe", b"LONG NOTICE", ("a" * 6000 + "LAST-BYTE").encode())
    elif kind == "reject":
        native.display_mitm_warning.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p, C.c_void_p]
        native.display_mitm_warning.restype = C.c_bool
        old, new = (C.c_ubyte * 32)(*range(32)), (C.c_ubyte * 32)(*range(1, 33))
        assert not native.display_mitm_warning(b"203.0.113.10", 27224, old, new)
        native.discovery_keys_verify_change.argtypes = [C.c_char_p, C.c_void_p, C.c_void_p]
        assert native.discovery_keys_verify_change(b"discovery.test", old, new) != 0
    elif kind in ("unknown-host", "acds-key"):
        key = (C.c_ubyte * 32)(*range(32))
        if kind == "unknown-host":
            native.prompt_unknown_host.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p]
            native.prompt_unknown_host.restype = C.c_bool
            assert not native.prompt_unknown_host(b"203.0.113.10", 27224, key)
        else:
            native.discovery_keys_verify_change.argtypes = [C.c_char_p, C.c_void_p, C.c_void_p]
            other = (C.c_ubyte * 32)(*range(1, 33))
            assert native.discovery_keys_verify_change(b"discovery.test", key, other) != 0
    elif kind == "prompt":
        background = b"BACKGROUND MEDIA"
        assert native.ui_controller_present(1, 1, Size(10, 20), background, len(background)) == 0
        assert not native.ui_notice_confirm(2, b"SERVER IDENTITY CHANGED\nSHA256:0123456789abcdef\nAccept changed key", 15)
    elif kind == "pages":
        background = b"BACKGROUND MEDIA"
        assert native.ui_controller_present(1, 1, Size(2, 10), background, len(background)) == 0
        body = "LONG SECURITY NOTICE\n" + "\n".join(f"Detail {i:02d}" for i in range(15))
        assert native.ui_notice_present(2, body.encode()) == 0
        while not log.with_suffix(".finish").exists():
            time.sleep(.01)
    elif kind == "live":
        background = b"BACKGROUND MEDIA"
        assert native.ui_controller_present(1, 1, Size(10, 20), background, len(background)) == 0
        time.sleep(.2)
        assert native.ui_notice_present(2, b"DANGER NOTICE\nFingerprint: SHA256:abc123\nConnection rejected.") == 0
        assert native.ui_notice_present(1, b"QUEUED WARNING\nSecond notice remains available.") == 0
        while not log.with_suffix(".advance").exists():
            time.sleep(.01)
        native.ui_notice_dismiss()  # Reveal the next queued notice.
        while not log.with_suffix(".finish").exists():
            time.sleep(.01)
        native.ui_notice_present(3, b"FATAL NOTICE\nFinal message survives screen teardown.")
        time.sleep(1)
    elif kind.startswith("color-"):
        severity = int(kind[-1])
        native.log_level_color.argtypes = [C.c_int]
        native.log_level_color.restype = C.c_char_p
        native.log_get_foreground_color.restype = C.c_char_p
        role = (None, 3, 4, 5, 2)[severity]
        expected = native.log_get_foreground_color() if role is None else native.log_level_color(role)
        log.with_suffix(".color").write_text(expected.decode())
        native.ui_notice_present(severity, f"COLOR {severity}\nComplete notice body".encode())
        time.sleep(.5)
    native.ui_input_shutdown()
    native.ui_controller_shutdown()
    native.keyboard_destroy()
    native.asciichat_shared_destroy()
    if dll_directory:
        dll_directory.close()
    print("NOTICE PROBE PASSED", flush=True)


def check_nonblocking_colors(library, directory):
    log = (Path(directory) / "nonblocking.log").resolve()
    term = Terminal([sys.executable, "-X", "utf8", str(Path(__file__).resolve()),
                     "--library", str(Path(library).resolve()), "--probe", "palette-nonblocking", "--log", str(log)],
                    rows=30, cols=100, terminal_replies=False)
    try:
        term.expect(lambda text: "NOTICE PROBE PASSED" in text, "Unanswered queries blocked color rendering")
        maximum = float(log.with_suffix(".latency").read_text())
        print(f"PASS all log-color getters and notices with unanswered OSC 11: maximum {maximum:.3f}ms")
    finally:
        term.close()


def check_dynamic_colors(library, directory):
    """Emulate terminal theme changes and keys arriving beside OSC replies."""
    log = (Path(directory) / "dynamic.log").resolve()
    term = Terminal([sys.executable, "-X", "utf8", str(Path(__file__).resolve()),
                     "--library", str(Path(library).resolve()), "--probe", "dynamic-colors", "--log", str(log)],
                    rows=50, cols=100, terminal_replies=True)
    try:
        for step, light in enumerate((True, False, True, False, True)):
            value = "ffff" if light else "0000"
            term.background_reply = f"o\x1b]11;rgb:{value}/{value}/{value}\x1b\\"
            log.with_suffix(f".start{step}").touch()
            result = log.with_suffix(f".result{step}")
            deadline = time.monotonic() + 20
            while not result.exists() and time.monotonic() < deadline:
                term.pump(.05)
            assert result.exists(), ("Missing theme result", step, "\n".join(term.screen.display))
            data = json.loads(result.read_text())
            assert data["keys"] and all(key == ord("o") for key in data["keys"]), data
            palette = ([(0, 0, 0), (156, 132, 94), (150, 60, 72), (130, 104, 140), (93, 145, 72)] if light else
                       [(255, 255, 255), (240, 204, 145), (232, 93, 111), (200, 160, 216), (144, 224, 112)])
            expected = [f"\x1b[38;2;{r};{g};{b}m" for r, g, b in palette]
            assert data["colors"] == expected, (step, data, expected)
            assert data["pre_colors"] == expected, ("automatic logger refresh", step, data, expected)
        term.expect(lambda text: "NOTICE PROBE PASSED" in text, "Theme probe did not exit")
        print("PASS dynamic light/dark theme switching: five rounds, all five severities, keyboard preserved")
    finally:
        term.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--probe")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    if args.probe:
        probe(args.library.resolve(), args.probe, args.log)
        return
    with tempfile.TemporaryDirectory(prefix="ascii-notices-") as directory:
        def command(kind):
            return [sys.executable, "-X", "utf8", str(Path(__file__).resolve()), "--library", str(args.library.resolve()),
                    "--probe", kind, "--log", str(Path(directory) / (kind + ".log"))]
        check_dynamic_colors(args.library, directory)
        check_nonblocking_colors(args.library, directory)
        for kind in ("layout", "buffer", "log", "success-log", "reject", "filter", "json", "announce-levels", "announce-filter", "announce-quiet", "announce-json", "restore-output",
                     "filter-header-level", "filter-header-file", "filter-header-function", "filter-header-custom", "filter-header-invert", "filter-header-long"):
            result = subprocess.run(command(kind), input="", text=True, encoding="utf-8", capture_output=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "NOTICE PROBE PASSED" in result.stdout
            if kind.startswith("filter-header-"):
                inverted = kind == "filter-header-invert"
                assert ("AUTHENTICATION REJECTED" in result.stderr) != inverted
                assert ("UNRELATED WARNING" in result.stderr) == inverted
                if kind == "filter-header-long":
                    assert "LAST-BYTE" in result.stderr
            if kind.startswith("announce-"):
                output = result.stdout + result.stderr
                assert "FILTERED INFO" not in output
                if kind in ("announce-filter", "announce-quiet"):
                    assert "SESSION READY" not in output
                else:
                    assert output.count("SESSION READY") >= 6
                    assert "LAN only via mDNS" in output and "globally" in output
                    assert "ascii-chat blue-mountain-tiger" in output
                if kind == "announce-json":
                    records = [json.loads(line) for line in output.splitlines() if line.startswith("{")]
                    assert sum("SESSION READY" in str(record) for record in records) >= 6
                    assert "╔" not in output
            if kind == "success-log":
                assert "SUCCESS" in result.stderr and "Operation Complete" in result.stderr
                assert "[INFO" in (Path(directory) / "success-log.log").read_text(encoding="utf-8")
            if kind == "restore-output":
                assert "MIRROR FAILED" in result.stderr
            if kind == "log":
                contents = (Path(directory) / "log.log").read_text(encoding="utf-8")
                assert "LAST-BYTE" in contents and "Complete fingerprint: SHA256:abc123" in contents
                assert "╔" not in contents and "\x1b" not in contents
            if kind == "filter":
                assert "KEEP THIS NOTICE" in result.stderr and "FILTERED NOTICE" not in result.stderr
            if kind == "json":
                records = [json.loads(line) for line in result.stderr.splitlines() if line.startswith("{")]
                assert any("KEEP THIS NOTICE" in str(record) for record in records)
                assert "╔" not in result.stderr and "+---" not in result.stderr
            print("PASS", kind)
        for kind in ("color-0", "color-1", "color-2", "color-3", "color-4", "unknown-host", "acds-key", "prompt", "live", "pages", "fatal-log", "forced-exit"):
            term = Terminal(command(kind), rows=12 if kind == "pages" else 30, cols=80)
            try:
                if kind == "fatal-log":
                    term.expect(lambda text: "MEDIA AFTER FATAL LOG" in text, "Fatal-level logging retired media")
                    (Path(directory) / "fatal-log.finish").touch()
                elif kind == "forced-exit":
                    term.expect(lambda text: "No Servers Found" in text and "Connect Manually" in text, "Queued notice lost on forced exit")
                    print("PASS", kind)
                    continue
                elif kind.startswith("color"):
                    severity = int(kind[-1])
                    term.expect(lambda text: f"Color {severity}" in text, "Colored notice did not render")
                    label = ("INFO", "WARNING", "ERROR", "FATAL", "SUCCESS")[severity]
                    lines = term.screen.display
                    label_row = next(i for i, line in enumerate(lines) if line.strip(" ║|") == label)
                    title_row = next(i for i, line in enumerate(lines) if f"Color {severity}" in line)
                    assert title_row == label_row + 2
                    assert lines[label_row + 1].lstrip().startswith(("╠", "+"))
                    cells = [cell for row in term.screen.buffer.values() for cell in row.values() if cell.data == "C"]
                    expected_screen = CaptureScreen(4, 1)
                    CaptureStream(expected_screen).feed((Path(directory) / (kind + ".color")).read_text() + "C")
                    expected = expected_screen.buffer[0][0].fg
                    assert any(cell.fg == expected for cell in cells), [(c.data, c.fg) for c in cells]
                elif kind in ("unknown-host", "acds-key"):
                    title = "Remote Host Identification Not Known" if kind == "unknown-host" else "Acds Server Key Has Changed"
                    term.expect(lambda text: title in text and "SHA256:" in text, "Security details missing")
                    if kind == "acds-key":
                        assert "Old SHA256:" in term.pump() and "New SHA256:" in term.pump()
                    term.write("no\r")
                elif kind == "prompt":
                    term.expect(lambda text: "Server Identity Changed" in text, "Security prompt missing")
                    term.resize(8, 30)
                    term.expect(lambda text: "Terminal too small" in text, "Small-terminal protection missing")
                    term.write("yes\r")
                    term.pump(.3)
                    term.resize(30, 80)
                    term.expect(lambda text: "SHA256:0123456789abcdef" in text, "Fingerprint lost on resize")
                    term.write("\r")  # default No, including after covered input was discarded
                elif kind == "pages":
                    term.expect(lambda text: "Long Security Notice" in text, "First page missing")
                    term.expect(lambda text: "Detail 14" in text and "Page 2/2" in text, "Final page truncated", timeout=15)
                    (Path(directory) / "pages.finish").touch()
                else:
                    term.expect(lambda text: "Danger Notice" in text, "Notice hidden by background")
                    term.resize(30, 44)
                    term.expect(lambda text: "SHA256:abc123" in text, "Notice lost on resize")
                    (Path(directory) / "live.advance").touch()
                    term.expect(lambda text: "Queued Warning" in text, "Second notice lost")
                    (Path(directory) / "live.finish").touch()
                    term.expect(lambda text: "Fatal Notice" in text, "Fatal notice missing")
                term.expect(lambda text: "NOTICE PROBE PASSED" in text, "Probe did not complete")
                print("PASS", kind)
            finally:
                term.close()


if __name__ == "__main__":
    main()
