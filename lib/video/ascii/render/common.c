#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render/render.h>
#include <ascii-chat/video/ascii/render/common.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>
#include <limits.h>

#define VIDEO_ASCII_RENDER_SEQUENCE_BUFFER_SIZE 16

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
    char sequence[VIDEO_ASCII_RENDER_SEQUENCE_BUFFER_SIZE];
    char *end = background ? append_16color_bg(sequence, (uint8_t)value) : append_16color_fg(sequence, (uint8_t)value);
    ob_write(out, sequence, (size_t)(end - sequence));
  }
}

static size_t glyph_bytes(uint32_t key) {
  uint8_t first;
  memcpy(&first, &key, 1);
  return first < 128 ? 1 : (first & 0xe0) == 0xc0 ? 2 : (first & 0xf0) == 0xe0 ? 3 : 4;
}

void ascii_emit_prepared_row(outbuf_t *out, ascii_color_t mode, const simd_cells_t *ops, const uint32_t *glyph,
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
  if (ascii_stabilizer_target() && ascii_stabilizer_target()->cells &&
      ascii_stabilizer_target()->width == (size_t)image->w && ascii_stabilizer_target()->height == (size_t)image->h)
    frame->stabilizer = ascii_stabilizer_target();
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
uint32_t ascii_background_contrast(ascii_color_t mode, uint32_t color, uint8_t luma) {
  if (mode == ASCII_COLOR_16) {
    uint8_t r, g, b;
    get_16color_rgb((uint8_t)color, &r, &g, &b);
    return (77 * r + 150 * g + 29 * b) / 256 < 127 ? 15u : 0;
  }
  return luma < 128 ? (mode == ASCII_COLOR_TRUE ? 0xffffffu : 15u) : 0;
}

void ascii_frame_emit_row(ascii_frame_t *frame, ascii_color_t mode, bool background, bool more_rows,
                          const simd_cells_t *ops) {
  ascii_stabilizer_frame_row(frame, mode, background, ops);
  for (size_t x = 0; x < frame->width; ++x) {
    frame->fg[x] = frame->bg[x] = UINT32_MAX;
    if (mode == ASCII_COLOR_NONE)
      continue;
    uint32_t value = mode == ASCII_COLOR_TRUE ? frame->packed[x] : frame->index[x];
    uint32_t contrast = ascii_background_contrast(mode, value, frame->luma[x]);
    frame->fg[x] = background ? contrast : value;
    frame->bg[x] = background ? value : UINT32_MAX;
  }
  ascii_emit_prepared_row(&frame->output, mode, ops, frame->glyph, frame->fg, frame->bg, frame->width);
  emit_row_end(&frame->output, mode != ASCII_COLOR_NONE, more_rows);
  ++frame->row;
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
  frame->source_height = (size_t)height;
  if (ascii_stabilizer_target() && ascii_stabilizer_target()->cells &&
      ascii_stabilizer_target()->width == (size_t)width && ascii_stabilizer_target()->height == (size_t)height)
    frame->stabilizer = ascii_stabilizer_target();
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
  ascii_stabilizer_halfrow(frame, mode, ops);
  ascii_emit_prepared_row(&frame->output, mode, ops, frame->glyph, frame->fg, frame->bg, frame->width);
  emit_row_end(&frame->output, mode != ASCII_COLOR_NONE, more_rows);
  ++frame->row;
}
