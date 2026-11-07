#!/usr/bin/env python3
"""Render two 80x32 captured ANSI streams as a side-by-side MP4."""
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont
import render_glyph_comparisons as render

left, right, output, font_path = map(Path, sys.argv[1:5])
render.font = ImageFont.truetype(str(font_path), 16)
label = ImageFont.truetype(str(font_path), 20)
encoder = subprocess.Popen([
    "ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1600x680", "-r", "8",
    "-i", "-", "-an", "-c:v", "libx264", "-crf", "22", "-preset", "fast", "-pix_fmt", "yuv420p",
    "-movflags", "+faststart", str(output)], stdin=subprocess.PIPE)
count = 0
try:
    for a, b in zip(render.frames(left), render.frames(right)):
        canvas = Image.new("RGB", (1600, 680), "#111318")
        canvas.paste(a, (0, 40)); canvas.paste(b, (800, 40))
        draw = ImageDraw.Draw(canvas)
        draw.text((16, 8), "Current luminance renderer", font=label, fill="white")
        draw.text((816, 8), "Experimental glyph matching", font=label, fill="white")
        if count == 48:
            canvas.save(output.with_suffix(".png"))
        encoder.stdin.write(canvas.tobytes())
        count += 1
        if count % 40 == 0:
            print(f"rendered {count} frames", flush=True)
finally:
    encoder.stdin.close()
    code = encoder.wait()
if code or count == 0:
    raise SystemExit(f"encoding failed: {code}, frames={count}")
print(f"wrote {count} frames ({count / 8:.1f} seconds)")
