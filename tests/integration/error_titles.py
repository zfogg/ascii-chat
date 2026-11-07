#!/usr/bin/env python3
"""Exercise error titles across threads, notification dismissal, and UTF-8 settings."""
import argparse
import os
from pathlib import Path
import re
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401

from terminal_titles import TitleTerminal


def check(binary, unicode):
    with tempfile.TemporaryDirectory(prefix="error-title-") as temporary:
        control = Path(temporary) / "control.txt"
        control.write_text("0 idle 0", encoding="ascii")
        term = TitleTerminal([str(binary), str(control), "--utf8=" + str(unicode).lower(),
                              "--color=false"], rows=35, cols=110, terminal_replies=True,
                             environment={"TERM": "xterm-256color", "ASCII_CHAT_DISABLE_FIRST_USE": "1",
                                          "ASCII_CHAT_TEST_TITLES": "1"})
        sequence = 0

        def command(action, number=0):
            nonlocal sequence
            sequence += 1
            pending = control.with_suffix(".next")
            pending.write_text(f"{sequence} {action} {number}\n", encoding="ascii")
            deadline = time.monotonic() + 5
            while True:
                try:
                    pending.replace(control)
                    return
                except PermissionError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(.01)

        separator = "•" if unicode else "*"
        base = "🔠 ascii-chat • mirror" if unicode else ""
        error = f"‼️ ascii-chat {separator} "
        try:
            term.expect(lambda text: "MEDIA_BACKGROUND" in text, "Fixture did not start", timeout=30)
            term.expect_title(base, timeout=20)
            command("loading")
            if unicode:
                term.expect(lambda _: term.titles and re.fullmatch(
                    r"[⠋⠙⠴⠦]  ascii-chat • mirror", term.titles[-1][1]),
                    "Loading title did not start", timeout=20)
            command("error", 1)
            term.expect_title(error + "ERROR code=27 - Failed to open media file or stream", timeout=20)
            command("mainerror")
            term.expect_title(error + "(2) ERROR code=4 - Configuration error", timeout=20)
            command("error", 2)
            term.expect_title(error + "(3) ERROR code=43 - Network timeout", timeout=20)
            term.write("?")
            term.pump(.3)
            assert term.titles[-1][1] == error + "(3) ERROR code=43 - Network timeout"
            term.write("?")
            term.write("q")
            if unicode:
                term.expect(lambda _: term.titles and re.fullmatch(
                    r"[⠋⠙⠴⠦]  ascii-chat • mirror", term.titles[-1][1]),
                    "Loading title did not return after dismissal", timeout=20)
            else:
                term.expect_title(base, timeout=20)
            command("idle")
            term.expect_title(base, timeout=20)
            # A resolved error remains in the notification popup until dismissal.
            command("error", 13)
            term.expect_title(error + "ERROR code=23 - Audio device error", timeout=20)
            term.write("q")
            term.expect_title(base, timeout=20)
            for request, key in ((8, "\x1b"), (9, "\r")):
                command("error", request)
                term.expect_title(error + "ERROR code=23 - Audio device error", timeout=20)
                term.write(key)
                term.expect_title(base, timeout=20)
            command("stop")
            deadline = time.monotonic() + 15
            while term.process.isalive() and time.monotonic() < deadline:
                term.pump(.1)
            assert not term.process.isalive(), "Fixture did not exit"
            assert term.process.exitstatus == 0, term.process.exitstatus
        finally:
            term.close()
    print(f"PASS error titles ({'UTF-8' if unicode else 'ASCII'} separator)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    if os.name == "nt":
        try:
            import winpty  # noqa: F401
        except ImportError:
            raise SystemExit(77)
    for unicode in (True, False):
        check(args.binary.resolve(), unicode)


if __name__ == "__main__":
    main()
