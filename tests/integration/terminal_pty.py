"""Exercise native terminal APIs against a real controlling pseudo-terminal."""
import codecs
import time
import pyte
import os
import sys
import termios
import pexpect

binary = sys.argv[1]
def start(mode):
    env = dict(os.environ, TERM="xterm-256color")
    env.pop("TTY", None)
    env.pop("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", None)
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
            chunk = child.read_nonblocking(4096, timeout=1)
            before += chunk
            stream.feed(decoder.decode(chunk))
        child.send(answer)
        after = finish(child)
        assert "sécret".encode() not in before + after
    finally:
        if child.isalive():
            child.close(force=True)
print("Terminal PTY contracts passed")
