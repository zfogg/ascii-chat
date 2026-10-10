"""Notice layout, logging, security decisions and real PTY/ConPTY presentation.

python tests/integration/terminal_notices.py --library build/bin/asciichat.dll
Requires the terminal_ui.py dependencies (pyte and pywinpty or pexpect).
"""
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

from terminal_ui import Terminal


class Size(C.Structure):
    _fields_ = [("rows", C.c_int), ("cols", C.c_int)]


def probe(library, kind, log):
    dll_directory = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
    native = C.CDLL(str(library))
    native.asciichat_shared_init.argtypes = [C.c_char_p, C.c_bool, C.c_bool]
    assert native.asciichat_shared_init(str(log).encode(), True, True) == 0
    words = [b"notice-probe", b"--no-check-update", b"--color", b"false" if kind in ("log", "reject", "layout") else b"true", b"mirror"]
    argv = (C.c_char_p * (len(words) + 1))(*words, None)
    assert native.options_init(len(words), argv) == 0
    native.log_set_terminal_output(True)
    native.ui_notice_present.argtypes = [C.c_int, C.c_char_p]
    native.ui_notice_log.argtypes = [C.c_int, C.c_char_p, C.c_int, C.c_char_p, C.c_char_p, C.c_char_p]
    native.ui_notice_confirm.argtypes = [C.c_int, C.c_char_p, C.c_uint]
    native.ui_notice_confirm.restype = C.c_bool
    native.ui_controller_present.argtypes = [C.c_int, C.c_int, Size, C.c_char_p, C.c_size_t]
    if kind == "layout":
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
        text = "SERVER KEY CHANGED\nSHA256:" + "0123456789abcdef" * 4 + "\n日本語 café\nEND"
        for width in (2, 7, 8, 20, 40, 80, 120):
            for unicode in (False, True):
                for severity, color in enumerate((37, 33, 31, 35)):
                    buffer = native.frame_buffer_create(0, 0)
                    try:
                        rows = native.ui_notice_render(buffer, text.encode(), severity, width, unicode, True)
                        data = C.string_at(native.frame_buffer_get_content(buffer), native.frame_buffer_get_length(buffer)).decode()
                        assert data.startswith(f"\x1b[1;{color}m")
                        plain = re.sub(r"\x1b\[[0-9;]*m", "", data)
                        assert len(plain.splitlines()) == rows
                        assert native.ui_notice_render(None, text.encode(), severity, width, unicode, False) == rows
                        lines = plain.splitlines()
                        if width >= 9:
                            assert all(native.display_width(line.encode()) <= width - 1 for line in lines)
                            content = "".join(line[2:-2].rstrip() for line in lines[1:-1])
                        else:
                            content = "".join(lines)
                        assert "0123456789abcdef" * 4 in content
                        assert "日本語" in content and "END" in content
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
        native.ui_notice_present(severity, f"COLOR {severity}\nComplete notice body".encode())
        time.sleep(.5)
    native.ui_input_shutdown()
    native.ui_controller_shutdown()
    native.keyboard_destroy()
    native.asciichat_shared_destroy()
    if dll_directory:
        dll_directory.close()
    print("NOTICE PROBE PASSED", flush=True)


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
        for kind in ("layout", "buffer", "log", "reject", "filter", "json"):
            result = subprocess.run(command(kind), input="", text=True, encoding="utf-8", capture_output=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "NOTICE PROBE PASSED" in result.stdout
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
        for kind in ("color-0", "color-1", "color-2", "color-3", "unknown-host", "acds-key", "prompt", "live", "pages"):
            term = Terminal(command(kind), rows=12 if kind == "pages" else 30, cols=80)
            try:
                if kind.startswith("color"):
                    severity = int(kind[-1])
                    term.expect(lambda text: f"COLOR {severity}" in text, "Colored notice did not render")
                    cells = [cell for row in term.screen.buffer.values() for cell in row.values() if cell.data == "C"]
                    expected = ("white", "brown", "red", "magenta")[severity]
                    assert any(cell.fg == expected for cell in cells), [(c.data, c.fg) for c in cells]
                elif kind in ("unknown-host", "acds-key"):
                    title = "REMOTE HOST IDENTIFICATION NOT KNOWN" if kind == "unknown-host" else "ACDS SERVER KEY HAS CHANGED"
                    term.expect(lambda text: title in text and "SHA256:" in text, "Security details missing")
                    if kind == "acds-key":
                        assert "Old SHA256:" in term.pump() and "New SHA256:" in term.pump()
                    term.write("no\r")
                elif kind == "prompt":
                    term.expect(lambda text: "SERVER IDENTITY CHANGED" in text, "Security prompt missing")
                    term.resize(8, 30)
                    term.expect(lambda text: "Terminal too small" in text, "Small-terminal protection missing")
                    term.write("yes\r")
                    term.pump(.3)
                    term.resize(30, 80)
                    term.expect(lambda text: "SHA256:0123456789abcdef" in text, "Fingerprint lost on resize")
                    term.write("\r")  # default No, including after covered input was discarded
                elif kind == "pages":
                    term.expect(lambda text: "LONG SECURITY NOTICE" in text, "First page missing")
                    term.expect(lambda text: "Detail 14" in text and "Page 2/2" in text, "Final page truncated", timeout=15)
                    (Path(directory) / "pages.finish").touch()
                else:
                    term.expect(lambda text: "DANGER NOTICE" in text, "Notice hidden by background")
                    term.resize(30, 44)
                    term.expect(lambda text: "SHA256:abc123" in text, "Notice lost on resize")
                    (Path(directory) / "live.advance").touch()
                    term.expect(lambda text: "QUEUED WARNING" in text, "Second notice lost")
                    (Path(directory) / "live.finish").touch()
                    term.expect(lambda text: "FATAL NOTICE" in text, "Fatal notice missing")
                term.expect(lambda text: "NOTICE PROBE PASSED" in text, "Probe did not complete")
                print("PASS", kind)
            finally:
                term.close()


if __name__ == "__main__":
    main()
