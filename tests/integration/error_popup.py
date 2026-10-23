#!/usr/bin/env python3
"""Exercise error notifications through the production presenter and PTY/ConPTY input."""
from pathlib import Path
import sys
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import json
import re
import subprocess
import tempfile
import time
from terminal_ui import Terminal


def capture(term, directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (directory / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    from PIL import Image, ImageDraw, ImageFont
    fonts = [Path("C:/Windows/Fonts/consola.ttf"), Path("/System/Library/Fonts/Menlo.ttc"),
             Path("/usr/share/fonts/TTF/DejaVuSansMono.ttf"),
             Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf")]
    font_path = next((p for p in fonts if p.exists()), None)
    if font_path is None:
        raise RuntimeError("Install a monospace font to capture terminal screenshots")
    font = ImageFont.truetype(str(font_path), 16)
    cell_width = max(1, round(font.getlength("M")))
    line_height = 21
    image = Image.new("RGB", (term.screen.columns * cell_width + 24, term.screen.lines * line_height + 24), "#10151c")
    draw = ImageDraw.Draw(image)
    palette = {"black": "#10151c", "red": "#cd3131", "green": "#0dbc79",
               "yellow": "#e5e510", "blue": "#2472c8", "magenta": "#bc3fbc", "cyan": "#11a8cd", "white": "#e5e5e5"}

    def color(value, fallback):
        return palette.get(value, f"#{value}" if isinstance(value, str) and len(value) == 6 else fallback)

    for row, text in enumerate(term.screen.display):
        for col, character in enumerate(text):
            cell = term.screen.buffer[row][col]
            fg, bg = color(cell.fg, "#e1e8ed"), color(cell.bg, "#10151c")
            if cell.reverse:
                fg, bg = bg, fg
            x, y = 12 + col * cell_width, 12 + row * line_height
            draw.rectangle((x, y, x + cell_width - 1, y + line_height - 1), fill=bg)
            draw.text((x, y), character, font=font, fill=fg)
    image.save(directory / f"{name}.png")


class Fixture:
    def __init__(self, args, extra=()):
        self.args = args
        self.temp = tempfile.TemporaryDirectory(prefix="error-popup-")
        self.control = Path(self.temp.name) / "control.txt"
        self.control.write_text("0 idle 0", encoding="ascii")
        self.sequence = 0
        self.term = Terminal([str(args.binary), str(self.control), "--color-mode=truecolor", "--color=true",
                              "--utf8=true", *extra], rows=40, cols=120,
                             environment={"TERM": "xterm-256color", "ASCII_CHAT_UPDATE_CHECK": "false"})
        try:
            self.expect("MEDIA_BACKGROUND")
        except BaseException:
            self.term.close()
            self.temp.cleanup()
            raise

    def command(self, action, number=0):
        self.sequence += 1
        # Replace atomically so the native reader never sees a partially written command.
        pending = self.control.with_suffix(".next")
        pending.write_text(f"{self.sequence} {action} {number}\n", encoding="ascii")
        deadline = time.monotonic() + 5
        while True:
            try:
                pending.replace(self.control)
                break
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.01)

    def expect(self, text):
        return self.term.expect(lambda value: text in value, f"Missing {text!r}", timeout=15)

    def key(self, key, expected):
        self.term.write(key)
        return self.expect(expected)

    def absent(self, text):
        return self.term.expect(lambda value: text not in value, f"Unexpected {text!r}", timeout=10)

    def error(self, number, marker):
        self.command("error", number)
        self.expect(marker)
        self.expect("? Help")
        self.expect("Dismiss all")

    def screenshot(self, name):
        self.term.pump(.2)
        capture(self.term, self.args.artifacts, name)
        log = Path(str(self.control) + ".log")
        if log.exists():
            (self.args.artifacts / f"{name}.log").write_bytes(log.read_bytes())

    def close(self):
        try:
            self.command("stop")
            deadline = time.monotonic() + 10
            while self.term.process.isalive() and time.monotonic() < deadline:
                self.term.pump(.1)
            assert not self.term.process.isalive(), "Fixture failed to join its workers"
            self.term.pump(.1)
            assert self.term.process.exitstatus == 0, (self.term.process.exitstatus, "".join(self.term.raw)[-4000:])
            output = "".join(self.term.raw)
            assert "ERROR: AddressSanitizer" not in output and "runtime error:" not in output, output[-4000:]
        finally:
            self.term.close()
            self.temp.cleanup()


def color_of(term, text, offset=0):
    for row, line in enumerate(term.screen.display):
        if text in line:
            return term.screen.buffer[row][line.index(text) + offset].fg
    raise AssertionError(f"No cell for {text!r}")


def navigation(args):
    f = Fixture(args)
    try:
        f.error(1, "FIRST_ERROR")
        f.expect("ERROR_MEDIA_OPEN (27)")
        f.expect("Error (1)")
        f.expect("Thread:")
        if args.diagnostics:
            f.expect("popup-media-worker")
            f.expect("Source:")
            f.expect("Backtrace")
            f.absent("ascii-chat\\tests\\standalone")
            assert "error_popup_fixture.c" in "".join(f.term.raw)
        else:
            f.absent("Source:")
            f.absent("Backtrace")
        title_color = color_of(f.term, "Error (1)")
        shortcut_color = color_of(f.term, "? Help")
        assert title_color != "default", title_color
        assert shortcut_color not in ("default", color_of(f.term, "? Help", 2))
        f.absent("Left / Right")
        f.absent("Esc / q / Enter")
        f.screenshot("single-media")
        f.error(2, "SECOND_ERROR")
        f.expect("Error (2)   2 of 2")
        f.expect("ERROR_NETWORK_TIMEOUT (43)")
        f.key("\x1b[D", "FIRST_ERROR")
        f.expect("1 of 2")
        f.screenshot("multiple-first")
        f.error(3, "THIRD_ERROR")
        f.expect("3 of 3")
        f.screenshot("multiple-latest")
        f.key("\x1b[H", "FIRST_ERROR")
        f.key("\x1b[D", "1 of 3")
        f.key("\x1b[F", "THIRD_ERROR")
        f.key("\x1b[C", "3 of 3")
        f.key("\x1b[D", "SECOND_ERROR")
        f.screenshot("multiple-middle")
        # Help receives all input before the error popup, including arrivals beneath it.
        f.key("?", "Error Popup Keyboard Shortcuts")
        f.expect("Previous / next error")
        f.expect("First / last error")
        f.expect("Scroll error details")
        f.expect("Scroll a page")
        f.expect("Dismiss all errors")
        f.expect("Enter / Esc")
        f.command("error", 8)
        f.term.pump(.4)
        f.expect("Error Popup Keyboard Shortcuts")
        f.term.write("\x1b[Dq\r")
        f.term.pump(.2)
        f.expect("Error Popup Keyboard Shortcuts")
        f.screenshot("help-above-errors")
        f.key("\x1b", "NEXT_BATCH_ERROR_8")
        f.expect("4 of 4")
        # Each dismissal key acknowledges the whole batch and is consumed.
        for number, key in [(8, "q"), (9, "\r"), (10, "\x1b")]:
            if number != 8:
                f.error(number, f"NEXT_BATCH_ERROR_{number}")
                f.expect("Error (1)")
            f.term.write(key)
            f.absent("? Help")
            f.expect("MEDIA_BACKGROUND")
            f.term.pump(.25)
            assert f.term.process.isalive(), "Dismissal escaped to the underlying session"
            assert "Error (" not in "\n".join(f.term.screen.display), "Acknowledged errors reopened"
        if args.diagnostics:
            f.error(11, "DEEP_BACKTRACE_ERROR")
            f.expect("Backtrace")
            f.term.pump(.2)
            def frames():
                return [int(n) for n in re.findall(r"\[(\d+)\]", "\n".join(f.term.screen.display))]
            first = frames()
            assert first[0] == 0 and max(first) <= 11, first
            track_col = (120 - 100) // 2 + 100 - 3
            track_rows = [row for row in range(f.term.screen.lines)
                          if f.term.screen.buffer[row][track_col].data in ("█", "│", "#", "|")]
            assert len(track_rows) == 12, track_rows
            f.screenshot("backtrace-scroll-top")
            f.term.write("\x1b[6~")
            f.term.expect(lambda _: frames() and min(frames()) > 0, "Backtrace page did not scroll")
            f.expect("DEEP_BACKTRACE_ERROR")
            f.expect("Source:")
            f.screenshot("backtrace-scroll-down")
            f.key("?", "Error Popup Keyboard Shortcuts")
            f.key("?", "DEEP_BACKTRACE_ERROR")
            assert min(frames()) > 0, "Help reset the trace offset"
            for _ in range(4):
                f.term.write("\x1b[6~")
            f.term.pump(.2)
            last = frames()
            f.term.write("\x1b[6~")
            f.term.pump(.2)
            assert frames() == last, "Scrolling past the final trace line"
            for _ in range(5):
                f.term.write("\x1b[5~")
            f.term.expect(lambda _: frames() and min(frames()) == 0, "Backtrace did not return to top")
            f.key("\r", "MEDIA_BACKGROUND")
        f.error(4, "LONG_ERROR line 00")
        for _ in range(20):
            if "[truncated]" in "\n".join(f.term.screen.display):
                break
            f.term.write("\x1b[B")
            f.term.pump(.1)
        f.expect("[truncated]")
        f.term.write("\x1b[5~" * 20)
        f.expect("LONG_ERROR line 00")
        f.term.resize(20, 120)
        f.expect("LONG_ERROR line 00")
        before = f.term.screen.display
        f.term.write("\x1b[6~")
        f.term.expect(lambda _: f.term.screen.display != before, "PageDown failed to scroll")
        f.key("\x1b[5~", "LONG_ERROR line 00")
        f.term.write("\x1b[B\x1b[B")
        f.absent("Message:")
        f.key("\x1b[A\x1b[A", "Message:")
        f.term.resize(8, 35)
        f.expect("Terminal too small")
        f.screenshot("too-small")
        f.term.resize(40, 120)
        f.expect("LONG_ERROR line 00")
        f.screenshot("restored-after-resize")
        for rows, cols in [(40, 59), (13, 120)]:
            f.term.resize(rows, cols)
            f.expect("Terminal too small")
        f.term.resize(14, 60)
        f.expect("? Help")
        assert "Terminal too small" not in "\n".join(f.term.screen.display)
        f.screenshot("minimum-size")
        f.term.resize(8, 35)
        f.expect("Terminal too small")
        f.term.write("q")
        f.term.resize(40, 120)
        f.absent("? Help")
        f.error(5, "CONTROL_ERROR")
        f.expect("?[2J payload")
        f.key("\r", "MEDIA_BACKGROUND")
        f.command("error", 6)
        f.term.pump(.3)
        assert "Error (" not in "\n".join(f.term.screen.display), "UI self-error reopened popup"
        f.error(7, "BURST_ERROR_139")
        f.expect("Error (128)")
        f.expect("older errors dropped")
        f.key("\x1b[H", "BURST_ERROR_012")
        f.key("\x1b[F", "BURST_ERROR_139")
        f.screenshot("bounded-overflow")
    finally:
        f.screenshot("last-navigation-state")
        f.close()


def layers(args):
    for action, marker in [("status", "STATUS_BACKGROUND"), ("splash", "SPLASH_BACKGROUND"),
                            ("prompt", "/saved/prompt.mp4"), ("logs", "search logs"),
                            ("stats", "OVERVIEW"), ("errors", "Standard message"),
                            ("dialog", "UPDATE_DIALOG_BACKGROUND")]:
        if action == "errors" and not args.diagnostics:
            continue
        f = Fixture(args)
        try:
            f.command(action)
            f.expect(marker)
            f.error(1, "FIRST_ERROR")
            f.screenshot(f"single-{action}")
            f.key("?", "Error Popup Keyboard Shortcuts")
            f.screenshot(f"help-{action}")
            f.key("?", "FIRST_ERROR")
            if action == "stats":
                for _ in range(3):
                    f.key("?", "Error Popup Keyboard Shortcuts")
                    f.key("?", "FIRST_ERROR")
            f.term.write("q")
            f.absent("? Help")
            f.expect(marker)
            if action == "prompt":
                f.key("z", "/saved/prompt.mp4z")
            if action == "errors":
                # Dismissal preserved the producer's pending errno stack.
                f.expect("FIRST_ERROR")
        finally:
            f.screenshot(f"last-{action}-state")
            f.close()
    # An error arriving while an existing help menu is open stays beneath it.
    f = Fixture(args)
    try:
        f.command("stats")
        f.expect("OVERVIEW")
        f.command("help")
        f.expect("Statistics Keyboard Shortcuts")
        f.command("error", 2)
        f.term.pump(.4)
        f.expect("Statistics Keyboard Shortcuts")
        f.key("?", "SECOND_ERROR")
        f.key("q", "OVERVIEW")
    finally:
        f.close()
    f = Fixture(args)
    try:
        f.command("logs")
        f.expect("search logs")
        f.key("?", "Log Viewer Keyboard Controls")
        f.command("error", 2)
        f.term.pump(.5)
        f.expect("Log Viewer Keyboard Controls")
        f.expect("Error (1)")
        f.screenshot("log-help-above-errors")
        f.key("?", "SECOND_ERROR")
        f.key("q", "search logs")
    finally:
        f.close()
    if args.diagnostics:
        f = Fixture(args)
        try:
            f.key("0", "Sync")
            f.error(1, "FIRST_ERROR")
            f.key("?", "Error Popup Keyboard Shortcuts")
            f.key("?", "FIRST_ERROR")
            f.key("q", "Sync")
            f.screenshot("sync-restored")
        finally:
            f.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--diagnostics", action="store_true")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    subprocess.run([str(args.binary), "--contract"], check=True, timeout=20)
    navigation(args)
    layers(args)
    # ASCII and monochrome terminals retain the same interaction contract.
    f = Fixture(args, ["--utf8=false", "--color=false", "--strip-ansi"])
    try:
        f.error(1, "FIRST_ERROR")
        assert color_of(f.term, "Error (1)") == "default"
        assert "┌" not in "\n".join(f.term.screen.display)
        f.key("q", "MEDIA_BACKGROUND")
    finally:
        f.close()
    (args.artifacts / "result.json").write_text(json.dumps({"passed": True, "diagnostics": args.diagnostics,
        "platform": sys.platform, "binary": str(args.binary)}, indent=2), encoding="utf-8")
    print("PASS: journal contracts, navigation, arrivals, dismissals, scroll, resize, colors, overflow, and TUI layers")


if __name__ == "__main__":
    main()
