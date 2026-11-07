#!/usr/bin/env python3
"""Generate the experimental glyph matcher's coverage atlas.

Run with Pillow installed and the DejaVu Sans Mono 2.37 TTF:
  python scripts/generate_glyph_masks.py path/to/DejaVuSansMono.ttf > lib/video/ascii/glyph_masks.h
"""
import sys
from PIL import Image, ImageDraw, ImageFont

font = ImageFont.truetype(sys.argv[1], 24)
print('''/* Coverage masks generated from DejaVu Sans Mono 2.37 at 24 px.
 * See DEJAVU-LICENSE for the font license. */
#pragma once
#include <stdint.h>
// clang-format off
static const uint8_t glyph_masks[95][16] = {''')
max_sum = 0
fine_masks = []
for codepoint in range(32, 127):
    image = Image.new("L", (14, 29))
    ImageDraw.Draw(image).text((0, 0), chr(codepoint), font=font, fill=255)
    fine_masks.append(list(image.resize((8, 8), Image.Resampling.LANCZOS).getdata()))
    image = image.resize((4, 4), Image.Resampling.LANCZOS)
    values = list(image.getdata())
    max_sum = max(max_sum, sum(values))
    print("  {" + ",".join(map(str, values)) + "},")
print("};")
print("static const uint8_t glyph_masks_fine[95][64] = {")
for values in fine_masks:
    print("  {" + ",".join(map(str, values)) + "},")
print("};")
print("// clang-format on")
print(f"#define GLYPH_MASK_MAX_SUM {max_sum}")
