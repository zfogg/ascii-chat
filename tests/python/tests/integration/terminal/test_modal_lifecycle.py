"""Shared modal focus and dismissal through native PTY/ConPTY input."""
import time
import pytest

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


def test_modal_lifecycle(terminal_factory, terminal_probe):
    term = terminal_factory([str(terminal_probe), "modal-lifecycle"], rows=37, cols=91)
    def view(label):
        try:
            term.expect(lambda text: label.casefold() in text.casefold(), f"Missing {label!r}", timeout=30)
        except AssertionError as error:
            raise AssertionError(f"{error}\nProcess alive: {term.process.isalive()}\nRaw: {''.join(term.raw)[-4000:]!r}") from error

    term.expect(lambda _: "MODAL-READY" in "".join(term.raw), "Fixture did not finish its focus transitions")
    view("Invalid UI screen snapshot")
    term.resize(8, 24)
    view("Terminal too small")
    term.write("\r" * 600)
    term.expect(lambda _: "COVER-DRAINED" in "".join(term.raw), "Hidden input was not consumed")
    # ConPTY may deliver a large write in chunks after the first input is read.
    term.pump(0.5)
    term.resize(37, 91)
    term.expect(lambda _: "RECOVERY-DRAINED" in "".join(term.raw), "Queued input survived recovery")
    view("Invalid UI screen snapshot")
    term.write("\x1b")
    view("Answer then Enter")
    # Function keys cannot transfer ownership away from a synchronous prompt.
    term.write("\x1b[24~answer\r")
    view("Cancel with Escape")
    term.write("\x1b")
    view("Prompt cancelled")
    term.write("\r")
    view("Wait for timeout")
    view("Prompt timed out")
    term.write("\r")
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
