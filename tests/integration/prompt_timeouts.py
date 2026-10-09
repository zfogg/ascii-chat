"""Native prompt deadline regressions via shared library and real PTY/ConPTY.

Run: python tests/integration/prompt_timeouts.py --library build/bin/asciichat.dll
Requires terminal_ui.py dependencies. Noninteractive tests keep stdin open.
"""
import argparse
import ctypes as C
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

class PromptOptions(C.Structure):
    _fields_ = [("timeout_seconds", C.c_uint), ("echo", C.c_bool),
                ("same_line", C.c_bool), ("mask_char", C.c_char)]

class Server(C.Structure):
    _fields_ = [("name", C.c_char * 256), ("address", C.c_char * 256),
                ("port", C.c_uint16), ("ipv4", C.c_char * 16),
                ("ipv6", C.c_char * 46), ("ttl", C.c_uint32)]

def probe(library, kind, log):
    dll_path = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
    native = C.CDLL(str(library))
    native.asciichat_shared_init.argtypes = [C.c_char_p, C.c_bool, C.c_bool]
    assert native.asciichat_shared_init(str(log).encode(), True, True) == 0
    words = [b"deadline-probe", b"--no-check-update", b"mirror"]
    argv = (C.c_char_p * (len(words) + 1))(*words, None)
    assert native.options_init(len(words), argv) == 0
    native.platform_prompt_question.argtypes = [C.c_char_p, C.c_char_p, C.c_size_t, PromptOptions]
    native.platform_prompt_yes_no.argtypes = [C.c_char_p, C.c_bool]
    native.platform_prompt_yes_no.restype = C.c_bool
    native.ui_mdns_select.argtypes = [C.POINTER(Server), C.c_int]
    response = C.create_string_buffer(128)
    budget = 30 if kind == "mdns" else 10 if kind == "update" else 1
    start = time.monotonic()
    if kind == "guarded-auto":
        # Match the interactivity guard used by openpgp_decrypt_with_gpg().
        native.platform_is_interactive.restype = C.c_bool
        assert native.platform_is_interactive(), "Automated answers must not hide a Windows TTY"
        opts = PromptOptions(1, False, True, b"*")
        assert native.platform_prompt_question(b"GPG passphrase", response, len(response), opts) == 0
        assert response.value == b"answer"
    elif kind in ("text", "password", "hidden", "success", "cancel", "auto"):
        secret = kind in ("password", "hidden", "auto")
        opts = PromptOptions(1, not secret, True, b"*" if kind != "hidden" and secret else b"\0")
        if kind == "auto":
            native.prompt_password_simple.argtypes = [C.c_char_p, C.c_char_p, C.c_size_t]
            result = native.prompt_password_simple(b"Deadline test", response, len(response))
        else:
            result = native.platform_prompt_question(b"Deadline test", response, len(response), opts)
        if kind in ("success", "auto"):
            assert result == 0 and response.value == b"answer", (result, response.value)
        else:
            assert result == -1 and not any(response.raw), (result, response.raw)
    elif kind == "yes":
        native.platform_prompt_yes_no_timeout.argtypes = [C.c_char_p, C.c_bool, C.c_uint]
        native.platform_prompt_yes_no_timeout.restype = C.c_bool
        assert not native.platform_prompt_yes_no_timeout(b"Deadline confirmation", True, 1)
    elif kind == "pipe":
        assert not native.platform_prompt_yes_no(b"Deadline confirmation", True)
    elif kind in ("mdns", "mdns-pipe"):
        server = Server(b"test-server", b"127.0.0.1", 27224, b"127.0.0.1", b"", 60)
        native.keyboard_init()
        assert native.ui_mdns_select(C.byref(server), 1) == -1
    elif kind == "update":
        native.session_display_create.restype = C.c_void_p
        display = native.session_display_create(None)
        assert display
        native.update_banner_show_prompt.argtypes = [C.c_void_p]
        native.update_banner_show_prompt.restype = C.c_bool
        assert not native.update_banner_show_prompt(display)
        native.session_display_destroy.argtypes = [C.c_void_p]
        native.session_display_destroy(display)
    elapsed = time.monotonic() - start
    if kind in ("text", "password", "hidden", "yes", "mdns", "update"):
        assert budget - .1 <= elapsed < budget + 2, elapsed
    else:
        assert elapsed < 1, elapsed
    native.ui_input_shutdown()
    native.ui_controller_shutdown()
    native.keyboard_destroy()
    native.asciichat_shared_destroy()
    print(f"PASS {kind} {elapsed:.2f}s", flush=True)
    if dll_path:
        dll_path.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--binary", type=Path, help="Also verify CLI overwrite actions")
    parser.add_argument("--case", action="append", help="Run only the named regression case (repeatable)")
    parser.add_argument("--probe")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    if args.probe:
        probe(args.library.resolve(), args.probe, args.log)
        return
    from terminal_ui import Terminal
    if args.binary:
        binary = str(args.binary.resolve())
        with tempfile.TemporaryDirectory(prefix="prompt-actions-") as directory:
            for action in ("--completions", "--config-create", "--man-page-create"):
                path = Path(directory) / "existing"
                path.write_text("KEEP")
                cmd = [binary, action]
                if action == "--completions":
                    cmd.append("bash")
                cmd.append(str(path))
                term = Terminal(cmd, rows=35, cols=110)
                try:
                    term.expect(lambda text: "Overwrite" in text, "Overwrite prompt missing", timeout=6)
                    term.expect(lambda text: "Prompt timed out" in text, "Timeout diagnostic missing", timeout=35)
                    deadline = time.monotonic() + 6
                    while term.process.isalive() and time.monotonic() < deadline:
                        term.pump(.1)
                    assert not term.process.isalive()
                    assert term.process.exitstatus != 0
                    assert path.read_text() == "KEEP"
                    print(f"PASS {action} preserves file and exits nonzero", flush=True)
                finally:
                    term.close()
    with tempfile.TemporaryDirectory(prefix="prompt-timeouts-") as directory:
        cases = ["text", "password", "hidden", "yes", "mdns", "update", "success", "cancel", "pipe", "mdns-pipe", "auto"]
        if os.name == "nt":
            cases.append("guarded-auto")
        if args.case:
            assert set(args.case) <= set(cases), args.case
            cases = args.case
        for kind in cases:
            log = Path(directory) / f"{kind}.log"
            cmd = [sys.executable, str(Path(__file__).resolve()), "--library", str(args.library.resolve()),
                   "--probe", kind, "--log", str(log)]
            if kind in ("pipe", "mdns-pipe", "auto"):
                env = dict(os.environ)
                if kind == "auto":
                    env["ASCII_CHAT_QUESTION_PROMPT_RESPONSE"] = "answer"
                child = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
                try:
                    child.wait(timeout=6)  # Deliberately leave the write end of stdin open.
                    out, err = child.communicate()
                    assert child.returncode == 0, (out, err)
                finally:
                    if child.poll() is None:
                        child.kill()
                print(out.decode().strip())
                continue
            previous_response = os.environ.get("ASCII_CHAT_QUESTION_PROMPT_RESPONSE")
            try:
                if kind == "guarded-auto":
                    os.environ["ASCII_CHAT_QUESTION_PROMPT_RESPONSE"] = "answer"
                term = Terminal(cmd, rows=40, cols=110)
            finally:
                if previous_response is None:
                    os.environ.pop("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", None)
                else:
                    os.environ["ASCII_CHAT_QUESTION_PROMPT_RESPONSE"] = previous_response
            try:
                if kind not in ("update", "guarded-auto"):
                    term.expect(lambda s: "Deadline" in s or "Select server" in s, "Prompt missing", timeout=6)
                if kind in ("password", "hidden"):
                    term.write("partial-secret")
                elif kind == "success":
                    term.write("answer\r")
                elif kind == "cancel":
                    term.write("\x1b")
                elif kind == "text":
                    # Repeated input must not extend the absolute deadline.
                    for _ in range(5):
                        term.write("x")
                        term.pump(.12)
                term.expect(lambda s: f"PASS {kind}" in s, "Probe did not finish", timeout=35)
                output = "".join(term.raw)
                if kind not in ("success", "cancel", "guarded-auto"):
                    budget = 30 if kind == "mdns" else 10 if kind == "update" else 1
                    assert f"Prompt timed out after {budget} seconds" in output, output
                assert "partial-secret" not in output
                assert "partial-secret" not in log.read_text(encoding="utf-8", errors="replace")
                print(f"PASS {kind} (native terminal)")
            finally:
                term.close()

if __name__ == "__main__":
    main()
