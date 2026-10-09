#!/usr/bin/env python3
"""Exercise native prompt presentation over an active screen (Windows DLL probe)."""
import argparse
import ctypes
import os
from pathlib import Path
import sys
import tempfile


def probe(library, log):
    dll_directory = os.add_dll_directory(str(library.parent))
    native = ctypes.CDLL(str(library))
    native.asciichat_shared_init.argtypes = [ctypes.c_char_p, ctypes.c_bool, ctypes.c_bool]
    assert native.asciichat_shared_init(str(log).encode(), True, True) == 0
    argv = (ctypes.c_char_p * 4)(b"prompt-probe", b"--no-check-update", b"mirror", None)
    native.options_init.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    assert native.options_init(3, argv) == 0

    class Size(ctypes.Structure):
        _fields_ = [("rows", ctypes.c_int), ("cols", ctypes.c_int)]

    class PromptOptions(ctypes.Structure):
        _fields_ = [("timeout_seconds", ctypes.c_uint), ("echo", ctypes.c_bool), ("same_line", ctypes.c_bool), ("mask_char", ctypes.c_char)]

    native.ui_controller_present.argtypes = [ctypes.c_int, ctypes.c_int, Size, ctypes.c_char_p, ctypes.c_size_t]
    background = b"BACKGROUND MEDIA"
    assert native.ui_controller_present(1, 1, Size(10, 20), background, len(background)) == 0
    native.platform_prompt_question.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t, PromptOptions]
    response = ctypes.create_string_buffer(128)
    assert native.platform_prompt_question(b"Account name", response, len(response), PromptOptions(0, True, True, b"\0")) == 0
    assert response.value == b"alice", response.value
    assert native.platform_prompt_question(b"Test passphrase", response, len(response), PromptOptions(0, False, True, b"*")) == 0
    assert response.value == b"s3cret", "Password edit result differs"
    native.platform_prompt_yes_no.argtypes = [ctypes.c_char_p, ctypes.c_bool]
    native.platform_prompt_yes_no.restype = ctypes.c_bool
    assert native.platform_prompt_yes_no(b"Verify host identity\nSHA256:TEST-FINGERPRINT\nAccept this host", False)
    native.ui_input_shutdown()
    native.ui_controller_shutdown()
    native.keyboard_destroy()
    print("PROMPT PROBE PASSED", flush=True)
    # The DLL's global debug registries are process-owned; shared teardown must
    # run before the Python host unloads it.
    native.asciichat_shared_destroy()
    dll_directory.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    if args.probe:
        probe(args.library.resolve(), args.log)
        return
    from terminal_ui import Terminal
    with tempfile.TemporaryDirectory(prefix="ascii-prompt-") as directory:
        log = Path(directory) / "prompt.log"
        term = Terminal([sys.executable, str(Path(__file__).resolve()), "--library", str(args.library.resolve()),
                         "--probe", "--log", str(log)], rows=30, cols=80)
        try:
            term.expect(lambda s: "Account name" in s, "Text prompt did not render")
            term.write("alicx\b" + "e\r")
            term.expect(lambda s: "Test passphrase" in s, "Password prompt did not render")
            term.write("s3cret")
            text = term.expect(lambda s: "******" in s, "Password mask did not render")
            assert "s3cret" not in text
            term.resize(8, 30)
            term.expect(lambda s: "Terminal too small" in s, "Expected prompt size warning")
            term.write("discard\r")
            term.pump(.3)
            term.resize(30, 80)
            term.expect(lambda s: "Test passphrase" in s and "******" in s, "Password not restored")
            term.write("\r")
            term.expect(lambda s: "SHA256:TEST-FINGERPRINT" in s, "Security context missing")
            term.write("yes\r")
            term.expect(lambda s: "PROMPT PROBE PASSED" in s, "Prompt probe did not finish")
            assert "s3cret" not in "".join(term.raw)
            assert "s3cret" not in log.read_text(encoding="utf-8", errors="replace")
            print("PASS text editing, masked password, covered input, resize restoration, host fingerprint, background isolation")
        finally:
            term.close()


if __name__ == "__main__":
    main()
