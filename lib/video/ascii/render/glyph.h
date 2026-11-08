#pragma once
#include <ascii-chat/video/ascii/render/render.h>
bool ascii_glyph_palette_supported_impl(const char *palette);
char *ascii_glyph_render(const image_t *image, ascii_color_t color, const simd_cells_t *ops, const char *palette,
                         bool background);
