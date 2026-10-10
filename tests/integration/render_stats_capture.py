"""Render tmux's captured ANSI cells as PNGs, without changing their contents."""

import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import pyte


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(args.font), 16)
    cell_width = round(font.getlength("M"))
    cell_height = 21
    colors = {
        "default": "#d1d5db",
        "black": "#11151d",
        "red": "#e06c75",
        "green": "#98c379",
        "brown": "#e5c07b",
        "blue": "#61afef",
        "magenta": "#c678dd",
        "cyan": "#56b6c2",
        "white": "#e5e7eb",
    }
    for name in [
        "mirror-overview",
        "client-overview",
        "server-overview",
        "discovery-overview",
        "acds-overview",
        "server-details",
        "acds-sessions",
    ]:
        raw = (args.captures / (name + ".ansi")).read_text(encoding="utf8")
        screen = pyte.Screen(110, len(raw.splitlines()))
        pyte.Stream(screen).feed(raw.rstrip("\n").replace("\n", "\r\n"))
        picture = Image.new(
            "RGB", (screen.columns * cell_width, screen.lines * cell_height), "#11151d"
        )
        draw = ImageDraw.Draw(picture)
        for y in range(screen.lines):
            for x in range(screen.columns):
                cell = screen.buffer[y][x]
                fg = colors.get(cell.fg, "#" + cell.fg)
                bg = (
                    "#11151d"
                    if cell.bg == "default"
                    else colors.get(cell.bg, "#" + cell.bg)
                )
                if cell.reverse:
                    fg, bg = bg, fg
                left, top = x * cell_width, y * cell_height
                draw.rectangle(
                    (left, top, left + cell_width, top + cell_height), fill=bg
                )
                draw.text((left, top), cell.data, font=font, fill=fg)
        picture.save(args.output / (name + ".png"))
        print(args.output / (name + ".png"))


if __name__ == "__main__":
    main()
