"""Exercise native terminal APIs against a real controlling pseudo-terminal."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import codecs
import time
import pyte
import os
import sys
import termios
import pexpect
import tempfile

binary = sys.argv[1]
test_state = tempfile.TemporaryDirectory(prefix="ascii-terminal-pty-")
def start(mode, **overrides):
    env = dict(os.environ, TERM="xterm-256color")
    env.pop("TTY", None)
    env.pop("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", None)
    env.pop("COLORFGBG", None)
    env.pop("TERM_PROGRAM", None)
    env.update(overrides)
    env["ASCII_CHAT_UPDATE_CACHE_DIR"] = os.path.join(test_state.name, mode)
    return pexpect.spawn(binary, [mode], env=env, dimensions=(37, 91), timeout=5)

def finish(child):
    try:
        child.expect(pexpect.EOF)
        output = child.before
        child.close()
        assert child.exitstatus == 0, (child.exitstatus, child.signalstatus, output)
        return output
    finally:
        if child.isalive():
            child.close(force=True)

size_output = finish(start("size"))
# Querying geometry must not emit terminal control sequences. Native runtime
# diagnostics on stderr are independent of the in-process size assertions.
assert b"\x1b" not in size_output, size_output
expected = b"\x1b[2J\x1b[H\x1b[3;5H\x1b[3C\x1b[2D\x1b[?25l\x1b[?25h\x1b[H\x1b[3J\x1bc"
assert finish(start("controls")) == expected
child = start("raw")
try:
    child.expect_exact(b"READY")
    flags = termios.tcgetattr(child.child_fd)
    assert not flags[3] & (termios.ICANON | termios.ECHO | termios.ISIG)
    child.send(b"x")
    child.expect_exact(b"RESTORED")
    flags = termios.tcgetattr(child.child_fd)
    required = termios.ICANON | termios.ECHO | termios.ISIG
    assert flags[3] & required == required
    child.send(b"y\n")
    assert finish(child) == b"y\r\n"
finally:
    if child.isalive():
        child.close(force=True)
for response, mode in [(b"\x1b]11;rgb:12/34/56\x07", "background"),
                      (b"\x1b]11;rgb:1212/3434/5656\x07", "background"),
                       (b"\x1b]11;rgb:1212/3434/5656\x1b\\", "background"),
                       (b"\x1b]11;invalid\x07", "bad-background"),
                       (b"\x1b]11;rgb:zz/3434/5656\x07", "bad-background"),
                       (b"\x1b]11;rgb:1212/zz/5656\x07", "bad-background"),
                       (b"\x1b]11;rgb:1212/3434/zz\x07", "bad-background"),
                       (b"\x1b]11;rgb:12345/3434/5656\x07", "bad-background"),
                       (b"\x1b]11;rgb:1212/3434/5656junk\x07", "bad-background"),
                       (b"\x1b]11;rgb:12/3434/56\x07", "background")]:
    child = start(mode)
    try:
        child.expect_exact(b"\x1b]11;?\x1b\\")
        child.send(response)
        assert finish(child) == b""
    finally:
        if child.isalive():
            child.close(force=True)
for mode, answer in [("password", "sécret".encode() + b"\r"), ("password-box", "sécret".encode() + b"\r"), ("password-cancel", b"\x1b")]:
    child = start(mode)
    try:
        screen = pyte.Screen(91, 37)
        stream = pyte.Stream(screen)
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        before = b""
        deadline = time.monotonic() + 5
        while "test passphrase" not in "\n".join(screen.display).lower():
            assert time.monotonic() < deadline, screen.display
            try:
                chunk = child.read_nonblocking(4096, timeout=1)
            except pexpect.TIMEOUT:
                continue
            before += chunk
            stream.feed(decoder.decode(chunk))
        if mode != "password-cancel":
            child.send(answer[:-1])
            deadline = time.monotonic() + 5
            while "> ******" not in "\n".join(screen.display):
                assert time.monotonic() < deadline, screen.display
                try:
                    chunk = child.read_nonblocking(4096, timeout=1)
                except pexpect.TIMEOUT:
                    continue
                before += chunk
                stream.feed(decoder.decode(chunk))
                assert "sécret" not in "\n".join(screen.display)
            child.send(b"\r")
        else:
            child.send(answer)
        after = finish(child)
        assert "sécret".encode() not in before + after
    finally:
        if child.isalive():
            child.close(force=True)
background_cases = [
    ("dark", b"\x1b]11;rgb:0000/0000/0000\x07", {}),
    ("light", b"\x1b]11;rgb:ffff/ffff/ffff\x07", {}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "15;0"}),
    ("light", b"\x1b]11;invalid\x07", {"COLORFGBG": "0;15"}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "0;-1", "TERM_PROGRAM": "vscode"}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "0;bad", "TERM_PROGRAM": "iTerm.app"}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "0;2147483648"}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "0;1suffix"}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "0;"}),
    ("dark", b"\x1b]11;invalid\x07", {"COLORFGBG": "unknown", "TERM": "dumb"}),
    ("dark", b"\x1b]11;invalid\x07", {"TERM": "xterm-truecolor"}),
]
for mode, response, environment in background_cases:
    child = start(mode, **environment)
    try:
        child.expect_exact(b"\x1b]11;?\x1b\\")
        child.send(response)
        assert finish(child) == b""
    finally:
        if child.isalive():
            child.close(force=True)
child = start("errno-ui")
try:
    screen = pyte.Screen(91, 37)
    stream = pyte.Stream(screen)
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    captured = bytearray()
    def wait_view(predicate):
        deadline = time.monotonic() + 5
        while not predicate(screen.display):
            assert time.monotonic() < deadline, screen.display
            try:
                chunk = child.read_nonblocking(4096, timeout=1)
            except pexpect.TIMEOUT:
                continue
            except pexpect.EOF:
                assert b"DIAGNOSTIC_DISABLED" in captured, (screen.display, captured)
                child.close()
                assert child.exitstatus == 0
                return False
            captured.extend(chunk)
            stream.feed(decoder.decode(chunk))
        return True
    def selected(lines, text):
        return any(">" in line and text in line for line in lines)
    if wait_view(lambda lines: selected(lines, "ERR-CHARLIE")):
        child.send(b"j")
        wait_view(lambda lines: selected(lines, "ERR-BRAVO"))
        child.send(b"x")
        wait_view(lambda lines: selected(lines, "ERR-ALPHA") and not any("ERR-BRAVO" in line for line in lines))
        child.send(b"\t")
        wait_view(lambda lines: selected(lines, "ERR-BRAVO") and not any("ERR-ALPHA" in line for line in lines))
        child.send(b"s")
        wait_view(lambda lines: any("stack sort" in line for line in lines))
        child.send(b"\x1b")
        finish(child)
finally:
    if child.isalive():
        child.close(force=True)
for mode, key, expected in [
    ("update-y", b"Y", True),
    ("update-enter", b"\r", True),
    ("update-n", b"n", False),
    ("update-escape", b"\x1b", False),
    ("update-idle", None, False),
]:
    child = start(mode)
    try:
        screen = pyte.Screen(91, 37)
        stream = pyte.Stream(screen)
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        deadline = time.monotonic() + 5
        while "Continue" not in "\n".join(screen.display):
            assert time.monotonic() < deadline, screen.display
            stream.feed(decoder.decode(child.read_nonblocking(4096, timeout=1)))
        prompt = "\n".join(screen.display)
        assert "Update available" in prompt and "Update now" in prompt
        assert "v7.1.0" in prompt and "v8.2.0" in prompt
        if key is None:
            time.sleep(.25)
            assert child.isalive(), "The update dialog should wait for an explicit choice"
            child.send(b"\x1b")
        else:
            child.send(key)
        output = finish(child)
        if expected:
            assert b"Updated ascii-chat: v7.1.0 -> v8.2.0" in output
            assert b"https://example.invalid/releases/v8.2.0" in output
        else:
            assert b"Updated ascii-chat:" not in output
    finally:
        if child.isalive():
            child.close(force=True)
for mode, known in [("progress-known", True), ("progress-unknown", False)]:
    child = start(mode)
    try:
        screen = pyte.Screen(91, 37)
        stream = pyte.Stream(screen)
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        def progress_view(predicate):
            deadline = time.monotonic() + 5
            while not predicate("\n".join(screen.display)):
                assert time.monotonic() < deadline, screen.display
                try:
                    chunk = child.read_nonblocking(65536, timeout=.2)
                except pexpect.TIMEOUT:
                    continue
                stream.feed(decoder.decode(chunk))
            return "\n".join(screen.display)
        initial_total = "3" if known else "?"
        text = progress_view(lambda text: "Rendering file" in text and f"frame 1/{initial_total}" in text
                             and "FIRST-PREVIEW" in text and "second-line" in text)
        assert "/tmp/output?[2J?.mp4" in text, text
        assert "ZZZZ" not in text, text
        child.send(b"f")
        final_total = "3" if known else "1"
        progress_view(lambda text: "Finalizing file" in text and f"frame 1/{final_total}" in text)
        child.send(b"e")
        progress_view(lambda text: "Recording failed" in text and "FAILED-PREVIEW" in text
                      and f"frame 1/{final_total}" in text and "FIRST-PREVIEW" not in text)
        child.send(b"o")
        text = progress_view(lambda text: f"frame 2/{final_total}" in text and "FAILED-PREVIEW" not in text)
        assert "XXXXXXXX" not in text, text
        child.send(b"q")
        finish(child)
    finally:
        if child.isalive():
            child.close(force=True)
print("Terminal PTY contracts passed")
