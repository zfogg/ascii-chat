#!/usr/bin/env python3
"""Verify errno-stack long fields stay clipped at the table edge after PTY resize."""

import argparse
from pathlib import Path
import tempfile
import time

try:
    from terminal_ui import Terminal
except ImportError as error:
    print(f"SKIP: terminal prerequisites unavailable: {error}")
    raise SystemExit(77)


MARKER = "USER_MESSAGE_MARKER"


def screen_row(term):
    text = term.expect(lambda value: MARKER in value and "Standard message" in value,
                       "errno stack fixture did not render", timeout=20)
    rows = term.screen.display
    row = next((line for line in rows if MARKER in line), None)
    assert row is not None, text
    return row


def assert_clipped(row, suffix, columns):
    table_width = 114 if columns - 1 > 118 else columns - 1 - 4
    left_padding = (columns - 1 - table_width) // 2
    table_end = left_padding + table_width
    visible = row[:table_end]
    assert visible.endswith(suffix), (
        f"expected {suffix!r} at table edge {table_end}, got {visible[-12:]!r} in {visible!r}"
    )
    assert not row[table_end:].strip(), row
    return table_end


def wait_for_clipped(term, suffix, columns, description):
    deadline = time.monotonic() + 20
    last = ""
    while time.monotonic() < deadline:
        term.pump()
        row = next((line for line in term.screen.display if MARKER in line), None)
        if row is None:
            continue
        last = row
        try:
            return row, assert_clipped(row, suffix, columns)
        except AssertionError:
            pass
    raise AssertionError(f"{description}: {last!r}")


def render_capture(term, directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (directory / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return
    font_candidates = [r"C:\Windows\Fonts\CascadiaMono.ttf", r"C:\Windows\Fonts\consola.ttf",
                       "/System/Library/Fonts/Menlo.ttc", "DejaVuSansMono.ttf"]
    font = next((ImageFont.truetype(path, 16) for path in font_candidates if Path(path).exists()), None)
    if font is None:
        return
    image = Image.new("RGB", (term.screen.columns * 10 + 24, term.screen.lines * 21 + 24), "#10151c")
    draw = ImageDraw.Draw(image)
    ansi_colors = {"black": "#10151c", "blue": "#5f87ff", "cyan": "#5ccfe6", "green": "#9ece6a",
                   "red": "#f7768e", "yellow": "#e0af68", "magenta": "#bb9af7", "white": "#e1e8ed"}
    for row, text in enumerate(term.screen.display):
        header = "errno stacks" in text or ("Standard message" in text and "User message" in text)
        for column, character in enumerate(text):
            foreground = term.screen.buffer[row][column].fg
            color = ansi_colors.get(foreground, f"#{foreground}" if isinstance(foreground, str) and len(foreground) == 6
                                    else "#e1e8ed")
            if header and color == "#e1e8ed":
                color = "#65ace1"
            draw.text((12 + column * 10, 12 + row * 21), character, font=font, fill=color)
    image.save(directory / f"{name}.png")


def exercise(binary, utf8, artifacts):
    suffix = "…" if utf8 else "..."
    term = Terminal([str(binary), f"--utf8={'true' if utf8 else 'false'}"], rows=24, cols=120)
    try:
        wide = screen_row(term)
        wide_end = assert_clipped(wide, suffix, 120)

        term.resize(18, 100)
        row, narrow_end = wait_for_clipped(term, suffix, 100, "errno stack did not redraw after resize")
        assert narrow_end < wide_end, (wide_end, narrow_end, row)
        render_capture(term, artifacts, "errno-tui-utf8" if utf8 else "errno-tui-ascii")
    finally:
        term.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        raise AssertionError(f"fixture binary not found: {binary}")
    artifacts = args.artifacts or Path(tempfile.mkdtemp(prefix="ascii-errno-tui-"))
    exercise(binary, True, artifacts)
    exercise(binary, False, artifacts)
    print(f"errno TUI ellipsis PTY smoke passed; artifacts: {artifacts}")


if __name__ == "__main__":
    main()
