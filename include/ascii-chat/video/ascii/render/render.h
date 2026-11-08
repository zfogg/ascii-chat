#pragma once

#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/simd/cells.h>

/** Shared terminal-cell contract for every ISA and scalar fallback.
 * Palettes use complete UTF-8 glyphs from cache64. Runs never cross rows.
 * Background mode sets a contrasting foreground. Colored rows finish with
 * SGR reset before newline; monochrome rows carry no SGR or erase sequences.
 * The returned buffer is owned by the caller and freed with SAFE_FREE.
 * ISA entry points select their vector stages; callers must check CPU support.
 */
typedef enum { ASCII_COLOR_NONE, ASCII_COLOR_16, ASCII_COLOR_256, ASCII_COLOR_TRUE } ascii_color_t;

/** Selects the character-matching algorithm. */
typedef enum { ASCII_RENDERER_LUMINANCE, ASCII_RENDERER_GLYPH } ascii_renderer_t;

/** Rendering parameters; the caller retains ownership of palette and backend. */
typedef struct {
  ascii_renderer_t kind;
  ascii_color_t color;
  const char *palette;
  const simd_cells_t *backend; /**< Optional; NULL selects the current SIMD backend. */
  bool background;
  bool dither;
  bool enhance;
} ascii_render_options_t;

/** Render an image using the chosen renderer. The caller frees the returned text. */
char *ascii_render_image(const image_t *image, const ascii_render_options_t *options);
/** Check whether a renderer accepts a nonempty palette. */
bool ascii_render_palette_supported(ascii_renderer_t kind, const char *palette);

#include <ascii-chat/video/ascii/stabilize.h>

char *ascii_render(const image_t *image, const char *palette, ascii_color_t color, bool background, bool dither,
                   const simd_cells_t *ops);

/** Two source rows per terminal row; the supplied backend processes both rows. */
char *ascii_render_halfblocks(const uint8_t *rgb, int width, int height, int stride_bytes, ascii_color_t color,
                              const simd_cells_t *ops);

/** Internal one-shot output target, consumed by the next frame initializer.
 * Returns the prior target so capability conversion can restore its scope.
 */
outbuf_t *ascii_render_set_output_target(outbuf_t *target);
