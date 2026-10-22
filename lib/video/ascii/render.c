/** @file video/ascii/render.c
 * Shared cell construction and terminal emission for scalar and SIMD kernels.
 */
#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>
#include <limits.h>
#include <ascii-chat/video/ascii/simd/cells.h>

static _Thread_local outbuf_t *g_output_target;

outbuf_t *ascii_render_set_output_target(outbuf_t *target) {
  outbuf_t *previous = g_output_target;
  g_output_target = target;
  return previous;
}

static void prepare_output(ascii_frame_t *frame, size_t capacity, outbuf_t *target) {
  frame->output_target = target;
  if (target) {
    if (target->cap < capacity) {
      target->buf = SAFE_REALLOC(target->buf, capacity, char *);
      target->cap = capacity;
    }
    frame->output = *target;
    frame->output.len = 0;
  } else {
    frame->output.cap = capacity;
    frame->output.buf = SAFE_MALLOC(capacity, char *);
  }
}

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
  if (mode == ASCII_COLOR_NONE && ops->emit_mono) {
    ops->emit_mono(out, glyph, width);
    return;
  }
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
                   const simd_cells_t *ops) {
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

char *ascii_render_halfblocks(const uint8_t *rgb, int width, int height, int stride, ascii_color_t mode,
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

asciichat_error_t ascii_frame_init(ascii_frame_t *frame, const image_t *image, const char *palette) {
  ASCII_OTEL_SCOPE(ASCII_FRAME_INIT);
  outbuf_t *target = ascii_render_set_output_target(NULL);
  memset(frame, 0, sizeof(*frame));
  if (!image || !image->pixels || image->w <= 0 || image->h <= 0 || !palette || !palette[0])
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid image or palette");
  size_t bytes, pixels, capacity, rows;
  if (checked_size_mul((size_t)image->w, (size_t)image->h, &pixels) != ASCIICHAT_OK ||
      checked_size_mul(pixels, 48u, &capacity) != ASCIICHAT_OK ||
      checked_size_mul((size_t)image->h, 5u, &rows) != ASCIICHAT_OK ||
      checked_size_add(capacity, rows, &capacity) != ASCIICHAT_OK ||
      checked_size_add(capacity, 1u, &capacity) != ASCIICHAT_OK ||
      checked_size_mul((size_t)image->w, 6 * sizeof(uint32_t), &bytes) != ASCIICHAT_OK)
    return ERROR_INVALID_PARAM;
  utf8_palette_cache_t *cache = get_utf8_palette_cache(palette);
  if (!cache)
    return ERROR_INVALID_PARAM;
  prepare_output(frame, capacity, target);
  frame->width = (size_t)image->w;
  for (size_t i = 0; i < 64; ++i)
    memcpy(frame->table + i, cache->cache64[i].utf8_bytes, cache->cache64[i].byte_len);
  frame->scratch = bytes <= sizeof(frame->inline_scratch) ? frame->inline_scratch : SAFE_MALLOC(bytes, uint32_t *);
  frame->packed = frame->scratch;
  frame->index = frame->packed + frame->width;
  frame->glyph = frame->index + frame->width;
  frame->fg = frame->glyph + frame->width;
  frame->bg = frame->fg + frame->width;
  frame->luma = (uint8_t *)(frame->bg + frame->width);
  return ASCIICHAT_OK;
}
void ascii_frame_emit_row(ascii_frame_t *frame, ascii_color_t mode, bool background, bool more_rows,
                          const simd_cells_t *ops) {
  for (size_t x = 0; x < frame->width; ++x) {
    frame->fg[x] = frame->bg[x] = UINT32_MAX;
    if (mode == ASCII_COLOR_NONE)
      continue;
    uint32_t value = mode == ASCII_COLOR_TRUE ? frame->packed[x] : frame->index[x];
    uint32_t contrast = frame->luma[x] < 128 ? (mode == ASCII_COLOR_TRUE ? 0xffffffu : 15u) : 0;
    if (mode == ASCII_COLOR_16) {
      uint8_t r, g, b;
      get_16color_rgb((uint8_t)value, &r, &g, &b);
      contrast = (77 * r + 150 * g + 29 * b) / 256 < 127 ? 15u : 0;
    }
    frame->fg[x] = background ? contrast : value;
    frame->bg[x] = background ? value : UINT32_MAX;
  }
  emit_prepared_row(&frame->output, mode, ops, frame->glyph, frame->fg, frame->bg, frame->width);
  emit_row_end(&frame->output, mode != ASCII_COLOR_NONE, more_rows);
}
char *ascii_frame_finish(ascii_frame_t *frame) {
  if (frame->scratch != frame->inline_scratch)
    SAFE_FREE(frame->scratch);
  ob_term(&frame->output);
  if (frame->output_target)
    *frame->output_target = frame->output;
  return frame->output.buf;
}

asciichat_error_t ascii_half_frame_init(ascii_frame_t *frame, const uint8_t *rgb, int width, int height, int *stride,
                                        ascii_color_t mode) {
  ASCII_OTEL_SCOPE(ASCII_HALF_FRAME_INIT);
  outbuf_t *target = ascii_render_set_output_target(NULL);
  memset(frame, 0, sizeof(*frame));
  size_t row_bytes, extent, bytes, capacity;
  if (!rgb || width <= 0 || height <= 0 || mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE ||
      checked_size_mul((size_t)width, 3, &row_bytes) != ASCIICHAT_OK || row_bytes > INT_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid half-block input");
  if (*stride <= 0)
    *stride = (int)row_bytes;
  if ((size_t)*stride < row_bytes || checked_size_mul((size_t)*stride, (size_t)height, &extent) != ASCIICHAT_OK ||
      checked_size_mul((size_t)width, 6 * sizeof(uint32_t), &bytes) != ASCIICHAT_OK ||
      checked_size_mul((size_t)width, ((size_t)height + 1) / 2, &capacity) != ASCIICHAT_OK ||
      checked_size_mul(capacity, 48u, &capacity) != ASCIICHAT_OK ||
      checked_size_add(capacity, 1u, &capacity) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid half-block stride or size");
  prepare_output(frame, capacity, target);
  frame->width = (size_t)width;
  frame->scratch = bytes <= sizeof(frame->inline_scratch) ? frame->inline_scratch : SAFE_MALLOC(bytes, uint32_t *);
  frame->packed = frame->scratch;
  frame->index = frame->packed + width;
  frame->glyph = frame->index + width;
  frame->fg = frame->glyph + width;
  frame->bg = frame->fg + width;
  const char blocks[5][4] = {"\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93", "\xe2\x96\x88", "\xe2\x96\x80"};
  for (size_t i = 0; i < 5; ++i)
    memcpy(frame->table + i, blocks[i], 3);
  return ASCIICHAT_OK;
}
void ascii_frame_emit_halfrow(ascii_frame_t *frame, ascii_color_t mode, bool more_rows, const simd_cells_t *ops) {
  emit_prepared_row(&frame->output, mode, ops, frame->glyph, frame->fg, frame->bg, frame->width);
  emit_row_end(&frame->output, mode != ASCII_COLOR_NONE, more_rows);
}
