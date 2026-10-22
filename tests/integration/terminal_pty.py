"""Exercise native terminal APIs against a real controlling pseudo-terminal."""
import codecs
import time
import pyte
import os
import sys
import termios
import pexpect

binary = sys.argv[1]
def start(mode, **overrides):
    env = dict(os.environ, TERM="xterm-256color")
    env.pop("TTY", None)
    env.pop("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", None)
    env.pop("COLORFGBG", None)
    env.pop("TERM_PROGRAM", None)
    env.update(overrides)
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

assert finish(start("size")) == b""
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
print("Terminal PTY contracts passed")
