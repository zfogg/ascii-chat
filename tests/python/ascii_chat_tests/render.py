"""Shared native terminal-rendering assertions."""

import queue
import time

from ascii_chat_tests.cli import cli_command
from ascii_chat_tests.terminal import Terminal


ASCII_PALETTE_STANDARD = "   ...',;:clodxkO0KXNWM"
ASCII_PALETTE_GLYPH = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
ASCII_RENDER_WIDTH = 120
ASCII_RENDER_HEIGHT = 80


def ensure_can_render(binary, test_environment):
    """Return a renderer assertion for mirror, client, or implicit discovery."""
    def render(mode="mirror", palette="standard", delay="1", *extra_args):
        palette_chars = ASCII_PALETTE_STANDARD if palette == "standard" else ASCII_PALETTE_GLYPH
        # Leading spaces are part of the native default, but command-line
        # parsers treat them as surrounding whitespace in a value.
        palette_arg = palette_chars.lstrip()
        mode_args = [mode] if mode is not None else []
        command = cli_command(str(binary), *mode_args, "-S", "-D", delay,
                              "--test-pattern", "--palette-chars", palette_arg, *extra_args)
        term = Terminal(command, rows=ASCII_RENDER_HEIGHT, cols=ASCII_RENDER_WIDTH,
                        environment=test_environment)
        try:
            raw = ""
            deadline = time.monotonic() + max(8.0, float(delay) + 5.0)
            while time.monotonic() < deadline and term.process.isalive():
                try:
                    chunk = term.output.get(timeout=0.1)
                    raw += chunk
                    term.stream.feed(chunk)
                except queue.Empty:
                    continue
            while True:
                try:
                    chunk = term.output.get_nowait()
                    raw += chunk
                    term.stream.feed(chunk)
                except queue.Empty:
                    break
            frame_start = raw.find("\x1b[H")
            assert "\ufffd" not in raw, "terminal stream contained invalid UTF-8"
            raw.encode("utf-8", errors="strict").decode("utf-8", errors="strict")
            assert frame_start >= 0, f"{mode} emitted no terminal frame reset"
            frame = raw[frame_start:]
            visible = "\n".join(term.screen.display)
            present = set(visible) & set(palette_chars.replace(" ", ""))
            minimum = 0.20 if palette == "standard" else 0.05
            assert len(present) / max(1, len(set(palette_chars.replace(" ", "")))) >= minimum
            return frame
        finally:
            term.close()

    return render
