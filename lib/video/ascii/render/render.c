#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render/render.h>
#include "glyph.h"
#include "luminance.h"

char *ascii_render_image(const image_t *image, const ascii_render_options_t *options) {
  if (!options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Missing ASCII rendering options");
    return NULL;
  }
  const simd_cells_t *backend = options->backend ? options->backend : simd_cells_backend();
  switch (options->kind) {
  case ASCII_RENDERER_LUMINANCE:
    return ascii_luminance_render(image, options->palette, options->color, options->background, options->dither,
                                  backend);
  case ASCII_RENDERER_GLYPH:
    return ascii_glyph_render(image, options->color, backend, options->palette, options->background);
  }
  SET_ERRNO(ERROR_INVALID_PARAM, "Unknown ASCII renderer");
  return NULL;
}

bool ascii_render_palette_supported(ascii_renderer_t kind, const char *palette) {
  if (kind == ASCII_RENDERER_GLYPH)
    return ascii_glyph_palette_supported_impl(palette);
  return kind == ASCII_RENDERER_LUMINANCE && palette && *palette;
}

char *ascii_render(const image_t *image, const char *palette, ascii_color_t color, bool background, bool dither,
                   const simd_cells_t *ops) {
  if (!ops) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Missing ASCII rendering backend");
    return NULL;
  }
  const ascii_render_options_t options = {.kind = ASCII_RENDERER_LUMINANCE,
                                          .color = color,
                                          .palette = palette,
                                          .backend = ops,
                                          .background = background,
                                          .dither = dither};
  return ascii_render_image(image, &options);
}

char *ascii_render_halfblocks(const uint8_t *rgb, int width, int height, int stride, ascii_color_t color,
                              const simd_cells_t *ops) {
  return ascii_luminance_halfblocks(rgb, width, height, stride, color, ops);
}
