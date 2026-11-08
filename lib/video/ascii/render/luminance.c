#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render/render.h>
#include <ascii-chat/video/ascii/render/common.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>
#include <limits.h>

#include "luminance.h"

char *ascii_luminance_render(const image_t *image, const char *palette, ascii_color_t mode, bool background,
                             bool dither, const simd_cells_t *ops) {
  if (!ops || mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rendering backend or color mode");
    return NULL;
  }
  if (!dither) {
#if SIMD_SUPPORT_SSE2
    if (ops == &simd_cells_sse2)
      return render_ascii_cells_sse2(image, palette, mode, background);
#endif
#if SIMD_SUPPORT_SSSE3
    if (ops == &simd_cells_ssse3)
      return render_ascii_cells_ssse3(image, palette, mode, background);
#endif
#if SIMD_SUPPORT_AVX2
    if (ops == &simd_cells_avx2)
      return render_ascii_cells_avx2(image, palette, mode, background);
#endif
#if SIMD_SUPPORT_NEON
    if (ops == &simd_cells_neon)
      return render_ascii_cells_neon(image, palette, mode, background);
#endif
#if SIMD_SUPPORT_SVE
    if (ops == &simd_cells_sve)
      return render_ascii_cells_sve(image, palette, mode, background);
#endif
  }
  ascii_frame_t frame;
  if (ascii_frame_init(&frame, image, palette) != ASCIICHAT_OK)
    return NULL;
  rgb_error_t *errors = NULL;
  if (mode == ASCII_COLOR_16 && dither)
    errors = SAFE_CALLOC((size_t)image->w * (size_t)image->h, sizeof(rgb_error_t), rgb_error_t *);
  frame.dithered = errors != NULL;
  for (int y = 0; y < image->h; ++y) {
    const uint8_t *source = (const uint8_t *)(image->pixels + (size_t)y * frame.width);
    ops->luminance(source, frame.luma, frame.width);
    ops->pack(source, frame.packed, frame.width);
    ops->lookup(frame.luma, frame.table, frame.glyph, frame.width);
    if (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256) {
      if (errors) {
        // Floyd-Steinberg error propagation is ordered; the other stages remain SIMD.
        for (int x = 0; x < image->w; ++x) {
          uint32_t pixel = frame.packed[x];
          frame.index[x] = rgb_to_16color_dithered((int)(pixel >> 16), (int)((pixel >> 8) & 255), (int)(pixel & 255), x,
                                                   y, image->w, image->h, errors);
        }
      } else
        ops->quantize(frame.packed, frame.index, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    ascii_frame_emit_row(&frame, mode, background, y + 1 < image->h, ops);
  }
  SAFE_FREE(errors);
  return ascii_frame_finish(&frame);
}

char *ascii_luminance_halfblocks(const uint8_t *rgb, int width, int height, int stride, ascii_color_t mode,
                                 const simd_cells_t *ops) {
#if SIMD_SUPPORT_SSE2
  if (ops == &simd_cells_sse2)
    return render_halfblocks_sse2(rgb, width, height, stride, mode);
#endif
#if SIMD_SUPPORT_SSSE3
  if (ops == &simd_cells_ssse3)
    return render_halfblocks_ssse3(rgb, width, height, stride, mode);
#endif
#if SIMD_SUPPORT_AVX2
  if (ops == &simd_cells_avx2)
    return render_halfblocks_avx2(rgb, width, height, stride, mode);
#endif
#if SIMD_SUPPORT_NEON
  if (ops == &simd_cells_neon)
    return render_halfblocks_neon(rgb, width, height, stride, mode);
#endif
#if SIMD_SUPPORT_SVE
  if (ops == &simd_cells_sve)
    return render_halfblocks_sve(rgb, width, height, stride, mode);
#endif

  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (!ops) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid half-block backend");
    return NULL;
  }
  ascii_frame_t frame;
  if (ascii_half_frame_init(&frame, rgb, width, height, &stride, mode) != ASCIICHAT_OK)
    return NULL;
  for (size_t y = 0; y < (size_t)height; y += 2) {
    const uint8_t *top = rgb + y * (size_t)stride;
    const uint8_t *bottom = y + 1 < (size_t)height ? top + stride : top;
    ops->pack(top, frame.packed, frame.width);
    ops->pack(bottom, frame.index, frame.width);
    if (mode == ASCII_COLOR_TRUE) {
      memcpy(frame.fg, frame.packed, frame.width * sizeof(uint32_t));
      memcpy(frame.bg, frame.index, frame.width * sizeof(uint32_t));
    } else if (mode != ASCII_COLOR_NONE) {
      ops->quantize(frame.packed, frame.fg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
      ops->quantize(frame.index, frame.bg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    ops->halfblock(frame.packed, frame.index, frame.glyph, frame.fg, frame.bg, frame.width, (unsigned)mode,
                   frame.table);
    ascii_frame_emit_halfrow(&frame, mode, y + 2 < (size_t)height, ops);
  }
  return ascii_frame_finish(&frame);
}
