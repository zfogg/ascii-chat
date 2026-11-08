#pragma once
#include <ascii-chat/video/ascii/render/render.h>
char *ascii_luminance_render(const image_t *image, const char *palette, ascii_color_t color, bool background,
                             bool dither, const simd_cells_t *ops);
char *ascii_luminance_halfblocks(const uint8_t *rgb, int width, int height, int stride, ascii_color_t color,
                                 const simd_cells_t *ops);
