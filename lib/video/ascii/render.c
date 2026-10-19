/** @file video/ascii/render.c
 * Shared cell construction and terminal emission for scalar and SIMD kernels.
 */
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>
#include <limits.h>
#include <ascii-chat/video/ascii/simd/cells.h>

static void emit_color(outbuf_t *out, ascii_color_t mode, bool background, int value) {
  if (mode == ASCII_COLOR_TRUE) {
    if (background)
      emit_set_truecolor_bg(out, (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value);
    else
      emit_set_truecolor_fg(out, (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value);
  } else if (mode == ASCII_COLOR_256) {
    if (background)
      emit_set_256_color_bg(out, (uint8_t)value);
    else
      emit_set_256_color_fg(out, (uint8_t)value);
  } else if (mode == ASCII_COLOR_16) {
    char sequence[16];
    char *end = background ? append_16color_bg(sequence, (uint8_t)value) : append_16color_fg(sequence, (uint8_t)value);
    ob_write(out, sequence, (size_t)(end - sequence));
  }
}

static size_t glyph_bytes(uint32_t key) {
  uint8_t first;
  memcpy(&first, &key, 1);
  return first < 128 ? 1 : (first & 0xe0) == 0xc0 ? 2 : (first & 0xf0) == 0xe0 ? 3 : 4;
}

static void emit_prepared_row(outbuf_t *out, ascii_color_t mode, const simd_cells_t *ops, const uint32_t *glyph,
                              const uint32_t *fg, const uint32_t *bg, size_t width) {
  uint32_t current_fg = UINT32_MAX, current_bg = UINT32_MAX;
  for (size_t x = 0; x < width;) {
    size_t run = ops->run(glyph + x, fg + x, bg + x, width - x);
    if (fg[x] == UINT32_MAX && bg[x] == UINT32_MAX && (current_fg != UINT32_MAX || current_bg != UINT32_MAX)) {
      emit_reset(out);
      current_fg = current_bg = UINT32_MAX;
    }
    if (fg[x] != current_fg) {
      emit_color(out, mode, false, (int)fg[x]);
      current_fg = fg[x];
    }
    if (bg[x] != current_bg) {
      emit_color(out, mode, true, (int)bg[x]);
      current_bg = bg[x];
    }
    emit_glyph_run(out, &glyph[x], glyph_bytes(glyph[x]), (uint32_t)run);
    x += run;
  }
}

char *ascii_render(const image_t *image, const char *palette, ascii_color_t mode, bool background, bool dither,
                   simd_luminance_fn luminance) {
  if (!image || !image->pixels || mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE || !palette || !palette[0] ||
      !luminance || image->w <= 0 || image->h <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid image, palette, or SIMD backend");
    return NULL;
  }
  size_t pixels, capacity, scratch_bytes, rows;
  if (checked_size_mul((size_t)image->w, (size_t)image->h, &pixels) != ASCIICHAT_OK ||
      checked_size_mul(pixels,
                       mode == ASCII_COLOR_NONE ? 4u
                       : background             ? 42u
                                                : 23u,
                       &capacity) != ASCIICHAT_OK ||
      checked_size_mul((size_t)image->h, 5u, &rows) != ASCIICHAT_OK ||
      checked_size_add(capacity, rows, &capacity) != ASCIICHAT_OK ||
      checked_size_add(capacity, 1u, &capacity) != ASCIICHAT_OK ||
      checked_size_mul((size_t)image->w, 6 * sizeof(uint32_t), &scratch_bytes) != ASCIICHAT_OK)
    return NULL;
  utf8_palette_cache_t *cache = get_utf8_palette_cache(palette);
  if (!cache)
    return NULL;
  uint32_t table[64] = {0};
  for (size_t i = 0; i < 64; ++i)
    memcpy(table + i, cache->cache64[i].utf8_bytes, cache->cache64[i].byte_len);
  const simd_cells_t *ops = simd_cells_for_luminance(luminance);
  outbuf_t out = {.cap = capacity, .buf = SAFE_MALLOC(capacity, char *)};
  uint32_t *scratch = SAFE_MALLOC(scratch_bytes, uint32_t *);
  size_t width = (size_t)image->w;
  uint32_t *packed = scratch, *index = packed + width, *glyph = index + width, *fg = glyph + width, *bg = fg + width;
  uint8_t *luma = (uint8_t *)(bg + width);
  rgb_error_t *errors = NULL;
  if (mode == ASCII_COLOR_16 && dither)
    errors = SAFE_CALLOC(pixels, sizeof(rgb_error_t), rgb_error_t *);
  for (int y = 0; y < image->h; ++y) {
    const uint8_t *source = (const uint8_t *)(image->pixels + (size_t)y * width);
    luminance(source, luma, width);
    ops->pack(source, packed, width);
    ops->lookup(luma, table, glyph, width);
    if (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256) {
      if (dither && mode == ASCII_COLOR_16) {
        // Floyd-Steinberg error propagation is ordered; the other row stages remain SIMD.
        for (int x = 0; x < image->w; ++x) {
          uint32_t p = packed[x];
          index[x] = rgb_to_16color_dithered((int)(p >> 16), (int)((p >> 8) & 255), (int)(p & 255), x, y, image->w,
                                             image->h, errors);
        }
      } else
        ops->quantize(packed, index, width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    for (size_t x = 0; x < width; ++x) {
      fg[x] = bg[x] = UINT32_MAX;
      if (mode == ASCII_COLOR_NONE)
        continue;
      uint32_t value = mode == ASCII_COLOR_TRUE ? packed[x] : index[x];
      uint32_t contrast = luma[x] < 128 ? (mode == ASCII_COLOR_TRUE ? 0xffffffu : 15u) : 0;
      if (mode == ASCII_COLOR_16) {
        uint8_t r, g, b;
        get_16color_rgb((uint8_t)value, &r, &g, &b);
        contrast = (77 * r + 150 * g + 29 * b) / 256 < 127 ? 15u : 0;
      }
      fg[x] = background ? contrast : value;
      bg[x] = background ? value : UINT32_MAX;
    }
    emit_prepared_row(&out, mode, ops, glyph, fg, bg, width);
    emit_row_end(&out, mode != ASCII_COLOR_NONE, y + 1 < image->h);
  }
  SAFE_FREE(errors);
  SAFE_FREE(scratch);
  ob_term(&out);
  return out.buf;
}

char *ascii_render_halfblocks(const uint8_t *rgb, int width, int height, int stride, ascii_color_t mode,
                              const simd_cells_t *ops) {
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  size_t row_bytes, extent, capacity, scratch_bytes;
  if (!rgb || !ops || mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE ||
      checked_size_mul((size_t)width, 3, &row_bytes) != ASCIICHAT_OK || row_bytes > INT_MAX) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid half-block input");
    return NULL;
  }
  if (stride <= 0)
    stride = (int)row_bytes;
  if ((size_t)stride < row_bytes) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Half-block stride is shorter than the RGB row");
    return NULL;
  }
  size_t output_rows = ((size_t)height + 1) / 2;
  if (checked_size_mul((size_t)stride, (size_t)height, &extent) != ASCIICHAT_OK ||
      checked_size_mul((size_t)width, output_rows, &capacity) != ASCIICHAT_OK ||
      checked_size_mul(capacity, 48u, &capacity) != ASCIICHAT_OK ||
      checked_size_add(capacity, 1u, &capacity) != ASCIICHAT_OK ||
      checked_size_mul((size_t)width, 6 * sizeof(uint32_t), &scratch_bytes) != ASCIICHAT_OK)
    return NULL;
  uint32_t keys[5] = {0};
  const char blocks[5][4] = {"\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93", "\xe2\x96\x88", "\xe2\x96\x80"};
  for (size_t k = 0; k < 5; ++k)
    memcpy(keys + k, blocks[k], 3);
  outbuf_t out = {.cap = capacity, .buf = SAFE_MALLOC(capacity, char *)};
  uint32_t *scratch = SAFE_MALLOC(scratch_bytes, uint32_t *);
  uint32_t *top = scratch, *bottom = top + width, *glyph = bottom + width, *fg = glyph + width, *bg = fg + width;
  for (size_t y = 0; y < (size_t)height; y += 2) {
    const uint8_t *t = rgb + y * (size_t)stride, *b = y + 1 < (size_t)height ? t + stride : t;
    ops->pack(t, top, (size_t)width);
    ops->pack(b, bottom, (size_t)width);
    if (mode == ASCII_COLOR_TRUE) {
      memcpy(fg, top, (size_t)width * sizeof(uint32_t));
      memcpy(bg, bottom, (size_t)width * sizeof(uint32_t));
    } else if (mode != ASCII_COLOR_NONE) {
      ops->quantize(top, fg, (size_t)width, mode == ASCII_COLOR_16 ? 16 : 256);
      ops->quantize(bottom, bg, (size_t)width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    ops->halfblock(top, bottom, glyph, fg, bg, (size_t)width, (unsigned)mode, keys);
    emit_prepared_row(&out, mode, ops, glyph, fg, bg, (size_t)width);
    emit_row_end(&out, mode != ASCII_COLOR_NONE, y + 2 < (size_t)height);
  }
  SAFE_FREE(scratch);
  ob_term(&out);
  return out.buf;
}
