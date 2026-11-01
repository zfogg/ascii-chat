"""Shared modal focus and dismissal contracts through native PTY/ConPTY input."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401

import argparse
import time
from terminal_ui import Terminal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    term = Terminal([str(args.binary.resolve()), "modal-lifecycle"], rows=37, cols=91)
    try:
        def view(label):
            try:
                term.expect(lambda text: label.casefold() in text.casefold(), f"Missing {label!r}", timeout=30)
            except AssertionError as error:
                raise AssertionError(f"{error}\nProcess alive: {term.process.isalive()}\nRaw: {''.join(term.raw)[-4000:]!r}") from error

        view("Invalid UI screen snapshot")
        term.resize(8, 24)
        view("Terminal too small")
        term.write("\r")
        term.expect(lambda _: "COVER-DRAINED" in "".join(term.raw), "Hidden input was not consumed")
        term.resize(37, 91)
        view("Invalid UI screen snapshot")
        term.write("\x1b")
        view("Answer then Enter")
        # Function keys cannot transfer ownership away from a synchronous prompt.
        term.write("\x1b[24~answer\r")
        view("Cancel with Escape")
        term.write("\x1b")
        view("Wait for timeout")
        deadline = time.monotonic() + 10
        while term.process.isalive() and time.monotonic() < deadline:
            term.pump(.1)
        assert not term.process.isalive(), "Modal cleanup did not finish"
        term.pump(.1)
        output = "".join(term.raw)
        assert "MODALS-RESTORED" in output, output[-4000:]
        assert term.process.exitstatus == 0, (term.process.exitstatus, output[-4000:])
        assert "AddressSanitizer" not in output and "runtime error:" not in output, output[-4000:]
        print("PASS modal focus, help, covered dismissal, prompt outcomes and terminal cleanup")
    finally:
        term.close()


if __name__ == "__main__":
    main()
