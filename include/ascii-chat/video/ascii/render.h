#pragma once

#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/simd/cells.h>

/** Shared terminal-cell contract for every ISA and scalar fallback.
 * Palettes use complete UTF-8 glyphs from cache64. Runs never cross rows.
 * Background mode sets a contrasting foreground. Colored rows finish with
 * SGR reset before newline; monochrome rows carry no SGR or erase sequences.
 * The returned buffer is owned by the caller and freed with SAFE_FREE.
 * ISA entry points select their vector stages; callers must check CPU support.
 */
typedef enum { ASCII_COLOR_NONE, ASCII_COLOR_16, ASCII_COLOR_256, ASCII_COLOR_TRUE } ascii_color_t;
char *ascii_render(const image_t *image, const char *palette, ascii_color_t color, bool background, bool dither,
                   simd_luminance_fn luminance);

/** Two source rows per terminal row; the supplied backend processes both rows. */
char *ascii_render_halfblocks(const uint8_t *rgb, int width, int height, int stride_bytes, ascii_color_t color,
                              const simd_cells_t *ops);
