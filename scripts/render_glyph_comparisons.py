#!/usr/bin/env python3
"""Rasterize captured truecolor snapshot output into side-by-side GIFs and PNGs.

Usage: python scripts/render_glyph_comparisons.py CAPTURE_DIR OUTPUT_DIR FONT.ttf
CAPTURE_DIR contains pattern0-{baseline,glyph}.log, pattern1-*.log and testsrc-*.log,
produced by the commands in docs/glyph-match-prototype.md. Requires Pillow.
"""
from pathlib import Path
import re
import sys
from PIL import Image, ImageDraw, ImageFont

font = None
escape = re.compile(rb"\x1b\[([0-9;]*)([mb])")


def frames(path):
    lines = [line for line in path.read_bytes().splitlines()
             if b"\x1b[" in line and not line.startswith(b"[")]
    if not lines or len(lines) % 32:
        raise ValueError(f"unexpected frame rows in {path}: {len(lines)}")
    for start in range(0, len(lines), 32):
        canvas = Image.new("RGB", (800, 640), "black")
        draw = ImageDraw.Draw(canvas)
        for y, line in enumerate(lines[start:start + 32]):
            x, pos, last, color = 0, 0, " ", (255, 255, 255)
            for match in escape.finditer(line):
                text = line[pos:match.start()].decode("ascii")
                for char in text:
                    draw.text((x * 10, y * 20), char, font=font, fill=color)
                    x += 1
                    last = char
                params, command = match.groups()
                values = [int(v or 0) for v in params.split(b";")]
                if command == b"m":
                    if values == [0]:
                        color = (255, 255, 255)
                    elif len(values) == 5 and values[:2] == [38, 2]:
                        color = tuple(values[2:])
                    else:
                        raise ValueError(f"unexpected SGR {values} in {path}")
                elif command == b"b":
                    for _ in range(values[0] or 1):
                        draw.text((x * 10, y * 20), last, font=font, fill=color)
                        x += 1
                pos = match.end()
            for char in line[pos:].decode("ascii"):
                draw.text((x * 10, y * 20), char, font=font, fill=color)
                x += 1
            if x != 80:
                raise ValueError(f"expected 80 cells, got {x} in {path}")
        yield canvas


if __name__ == "__main__":
    source, destination = map(Path, sys.argv[1:3])
    font = ImageFont.truetype(sys.argv[3], 16)
    label = ImageFont.truetype(sys.argv[3], 20)
    destination.mkdir(parents=True, exist_ok=True)
    for name in ("pattern0", "pattern1", "testsrc"):
        before, after = frames(source / f"{name}-baseline.log"), frames(source / f"{name}-glyph.log")
        combined = []
        for left, right in zip(before, after):
            canvas = Image.new("RGB", (1600, 680), "#111318")
            canvas.paste(left, (0, 40))
            canvas.paste(right, (800, 40))
            draw = ImageDraw.Draw(canvas)
            draw.text((16, 8), "Current luminance renderer", font=label, fill="white")
            draw.text((816, 8), "Experimental glyph matching", font=label, fill="white")
            combined.append(canvas)
        if len(combined) < 2 or len({im.tobytes() for im in combined}) < 2:
            raise ValueError(f"{name} has no animation")
        combined[min(6, len(combined) - 1)].save(destination / f"{name}.png", optimize=True)
        combined[0].save(destination / f"{name}.gif", save_all=True, append_images=combined[1:],
                         duration=125, loop=0, optimize=True, disposal=2)
        print(f"{name}: {len(combined)} frames")
