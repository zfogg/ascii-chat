/** @file video/ascii/render.c
 * Shared cell construction and terminal emission for scalar and SIMD kernels.
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define VIDEO_ASCII_RENDER_SEQUENCE_BUFFER_SIZE 16
#define ASCII_TEMPORAL_MOTION_LIMIT 8
#define ASCII_TEMPORAL_MAX_INTERVAL_NS 250000000ULL
#define ASCII_TEMPORAL_COLOR_TIME_NS 50000000ULL

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
static _Thread_local ascii_stabilizer_t *g_stabilizer_target;

ascii_stabilizer_t *ascii_stabilizer_set_target(ascii_stabilizer_t *state) {
  ascii_stabilizer_t *previous = g_stabilizer_target;
  g_stabilizer_target = state;
  return previous;
}

void ascii_stabilizer_reset(ascii_stabilizer_t *state) {
  if (!state)
    return;
  SAFE_FREE(state->cells);
  memset(state, 0, sizeof(*state));
}

asciichat_error_t ascii_stabilizer_prepare(ascii_stabilizer_t *state, size_t width, size_t height,
                                           uint64_t configuration) {
  if (!state || !width || !height || width > SIZE_MAX / height ||
      width * height > SIZE_MAX / sizeof(ascii_temporal_cell_t))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stabilization viewport");
  if (state->cells && state->width == width && state->height == height && state->configuration == configuration)
    return ASCIICHAT_OK;
  ascii_temporal_cell_t *cells = SAFE_CALLOC(width * height, sizeof(*cells), ascii_temporal_cell_t *);
  if (!cells)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate stabilization history");
  SAFE_FREE(state->cells);
  state->cells = cells;
  state->width = width;
  state->height = height;
  state->configuration = configuration;
  state->last_timestamp_ns = 0;
  state->elapsed_ns = UINT64_MAX;
  return ASCIICHAT_OK;
}

static uint32_t temporal_blend_color(uint32_t previous, uint32_t current, unsigned weight) {
  uint32_t result = 0;
  for (unsigned shift = 0; shift <= 16; shift += 8) {
    unsigned old_channel = (previous >> shift) & 255;
    unsigned new_channel = (current >> shift) & 255;
    result |= (uint32_t)((old_channel * (256 - weight) + new_channel * weight + 128) / 256) << shift;
  }
  return result;
}

uint32_t ascii_stabilizer_choose(ascii_temporal_cell_t *cell, const uint8_t samples[16], uint32_t candidate,
                                 uint64_t candidate_error, uint64_t previous_error, uint32_t color, uint64_t elapsed_ns,
                                 uint32_t *stabilized_color) {
  unsigned motion = 0;
  if (cell->valid) {
    for (size_t i = 0; i < 16; ++i)
      motion += (unsigned)abs((int)samples[i] - (int)cell->samples[i]);
    motion /= 16;
  }
  uint32_t result = candidate;
  uint32_t output_color = color;
  if (cell->valid && motion <= ASCII_TEMPORAL_MOTION_LIMIT && elapsed_ns <= ASCII_TEMPORAL_MAX_INTERVAL_NS) {
    uint64_t margin = candidate_error / 8 + 128;
    if (previous_error != UINT64_MAX &&
        (previous_error <= candidate_error || previous_error - candidate_error <= margin))
      result = cell->glyph;
    // Weight depends on elapsed time, avoiding a different response at each FPS.
    unsigned weight = (unsigned)(elapsed_ns * 256 / (elapsed_ns + ASCII_TEMPORAL_COLOR_TIME_NS));
    output_color = temporal_blend_color(cell->color, color, weight);
  }
  memcpy(cell->samples, samples, sizeof(cell->samples));
  cell->glyph = result;
  cell->color = output_color;
  cell->valid = true;
  if (stabilized_color)
    *stabilized_color = output_color;
  return result;
}

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
  if (g_stabilizer_target && g_stabilizer_target->cells && g_stabilizer_target->width == (size_t)image->w &&
      g_stabilizer_target->height == (size_t)image->h)
    frame->stabilizer = g_stabilizer_target;
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
  if (frame->stabilizer && !frame->temporal_preselected) {
    const uint64_t elapsed = frame->stabilizer->elapsed_ns;
    const bool recent = elapsed <= ASCII_TEMPORAL_MAX_INTERVAL_NS;
    const unsigned weight = recent ? (unsigned)(elapsed * 256 / (elapsed + ASCII_TEMPORAL_COLOR_TIME_NS)) : 256;
    for (size_t x = 0; x < frame->width; ++x) {
      ascii_temporal_cell_t *cell = &frame->stabilizer->cells[frame->row * frame->width + x];
      uint8_t luma = frame->luma[x];
      uint32_t glyph = frame->glyph[x];
      uint32_t color = frame->packed[x];
      if (cell->valid && recent && abs((int)luma - (int)cell->samples[0]) <= ASCII_TEMPORAL_MOTION_LIMIT) {
        if (cell->glyph != glyph) {
          // Only nearby brightness bins can win the hysteresis test. Searching
          // outward avoids scanning the entire 64-entry palette for every cell.
          unsigned bin = luma >> 2;
          int best_delta = (int)luma - ((int)bin * 4 + 2);
          uint64_t best_error = (uint64_t)(best_delta * best_delta);
          uint64_t margin = best_error / 8 + 128;
          for (unsigned distance = 1; distance < 64; ++distance) {
            bool found = false;
            for (int direction = -1; direction <= 1; direction += 2) {
              int index = (int)bin + direction * (int)distance;
              if (index < 0 || index >= 64 || frame->table[index] != cell->glyph)
                continue;
              int delta = (int)luma - (index * 4 + 2);
              uint64_t error = (uint64_t)(delta * delta);
              if (error <= best_error || error - best_error <= margin)
                glyph = cell->glyph;
              found = true;
            }
            // Bins farther than this cannot fall within the error margin.
            int nearest_delta = (int)distance * 4 - 3;
            if (found || (uint64_t)(nearest_delta * nearest_delta) > best_error + margin)
              break;
          }
        }
        color = temporal_blend_color(cell->color, color, weight);
      }
      cell->samples[0] = luma;
      cell->glyph = frame->glyph[x] = glyph;
      cell->color = frame->packed[x] = color;
      cell->valid = true;
      if (background)
        frame->luma[x] =
            (uint8_t)((77u * (color >> 16) + 150u * ((color >> 8) & 255) + 29u * (color & 255) + 128u) >> 8);
    }
    if (!frame->dithered && (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256))
      ops->quantize(frame->packed, frame->index, frame->width, mode == ASCII_COLOR_16 ? 16 : 256);
  }
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
  if (g_stabilizer_target && g_stabilizer_target->cells && g_stabilizer_target->width == (size_t)width &&
      g_stabilizer_target->height == (size_t)height)
    frame->stabilizer = g_stabilizer_target;
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
  if (frame->stabilizer) {
    size_t top_row = frame->row * 2;
    bool has_bottom = top_row + 1 < frame->source_height;
    for (size_t x = 0; x < frame->width; ++x) {
      uint32_t *pixels[] = {&frame->packed[x], &frame->index[x]};
      for (size_t half = 0; half < (has_bottom ? 2u : 1u); ++half) {
        uint32_t color = *pixels[half];
        uint8_t luma = (uint8_t)((76u * (color >> 16) + 150u * ((color >> 8) & 255) + 29u * (color & 255)) >> 8);
        uint8_t samples[16];
        memset(samples, luma, sizeof(samples));
        ascii_temporal_cell_t *cell = &frame->stabilizer->cells[(top_row + half) * frame->width + x];
        ascii_stabilizer_choose(cell, samples, 0, 0, UINT64_MAX, color, frame->stabilizer->elapsed_ns, pixels[half]);
      }
      if (!has_bottom)
        frame->index[x] = frame->packed[x];
    }
    if (mode == ASCII_COLOR_TRUE) {
      memcpy(frame->fg, frame->packed, frame->width * sizeof(uint32_t));
      memcpy(frame->bg, frame->index, frame->width * sizeof(uint32_t));
    } else if (mode != ASCII_COLOR_NONE) {
      ops->quantize(frame->packed, frame->fg, frame->width, mode == ASCII_COLOR_16 ? 16 : 256);
      ops->quantize(frame->index, frame->bg, frame->width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    ops->halfblock(frame->packed, frame->index, frame->glyph, frame->fg, frame->bg, frame->width, (unsigned)mode,
                   frame->table);
  }
  emit_prepared_row(&frame->output, mode, ops, frame->glyph, frame->fg, frame->bg, frame->width);
  emit_row_end(&frame->output, mode != ASCII_COLOR_NONE, more_rows);
  ++frame->row;
}

/* Compare a small spatial image patch against glyph coverage masks.
 * The source is fitted at 8x8 samples per output cell, then averaged to 4x4.
 * ASCII coverage comes from DejaVu Sans Mono; real terminal fonts may differ. */
#include "glyph_masks.h"

#define GLYPH_ASCII_COUNT 95
#define GLYPH_BLOCK_COUNT 20
#define GLYPH_COUNT (GLYPH_ASCII_COUNT + GLYPH_BLOCK_COUNT)

static int glyph_palette_index(const uint8_t *p, size_t *length) {
  if (*p >= 32 && *p <= 126) {
    *length = 1;
    return (int)*p - 32;
  }
  if (*p == 0xe2 && p[1] == 0x96 && p[2] >= 0x80 && p[2] <= 0x93) {
    *length = 3;
    return GLYPH_ASCII_COUNT + (int)(p[2] - 0x80);
  }
  return -1;
}

// Block elements have font-independent geometry. Coverage is the area of
// each quarter-cell covered by the block, including eighth-cell boundaries.
static void glyph_block_mask(int block, uint8_t mask[16]) {
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      int coverage = 0;
      for (int sy = 0; sy < 2; ++sy) {
        for (int sx = 0; sx < 2; ++sx) {
          int px = 2 * x + sx, py = 2 * y + sy;
          bool set = block == 0    ? py < 4
                     : block <= 7  ? py >= 8 - block
                     : block == 8  ? true
                     : block <= 15 ? px < 16 - block
                     : block == 16 ? px >= 4
                                   : false;
          coverage += set ? 1 : 0;
        }
      }
      mask[y * 4 + x] = block >= 17 ? (uint8_t)((block - 16) * 255 / 4) : (uint8_t)(coverage * 255 / 4);
    }
  }
}

static uint32_t glyph_key(int index) {
  if (index < GLYPH_ASCII_COUNT)
    return (uint32_t)(index + 32);
  uint32_t cp = 0x2580u + (uint32_t)(index - GLYPH_ASCII_COUNT);
  const uint8_t bytes[4] = {(uint8_t)(0xe0u | (cp >> 12)), (uint8_t)(0x80u | ((cp >> 6) & 63)),
                            (uint8_t)(0x80u | (cp & 63)), 0};
  uint32_t key = 0;
  memcpy(&key, bytes, sizeof(key));
  return key;
}

bool ascii_glyph_palette_supported(const char *palette) {
  if (!palette || !*palette)
    return false;
  for (const uint8_t *p = (const uint8_t *)palette; *p;) {
    size_t length;
    if (glyph_palette_index(p, &length) < 0)
      return false;
    p += length;
  }
  return true;
}

typedef struct {
  int16_t mask[16];
  int16_t centered[16];
  uint8_t fine[64];
  int sum, tone, energy;
  uint32_t key;
} glyph_candidate_t;

// Preserve hue while keeping visible ink against the dark canvas. Color is
// weighted by glyph coverage, so a small feature is not colored by its corner.
static uint32_t glyph_foreground(const glyph_candidate_t *glyph, const int16_t samples[3][16], const int sums[3]) {
  unsigned channels[3], peak = 0;
  int order[16];
  int luminance[16];
  for (int i = 0; i < 16; ++i) {
    luminance[i] = (77 * samples[0][i] + 150 * samples[1][i] + 29 * samples[2][i]) >> 8;
    int j = i;
    while (j > 0 && luminance[order[j - 1]] > luminance[i]) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = i;
  }
  int median = order[8];
  int weighted_luma = 0;
  for (int i = 0; i < 16; ++i)
    weighted_luma += luminance[i] * glyph->mask[i];
  if (glyph->sum) {
    int covered = 0;
    for (int i = 0; i < 16; ++i) {
      covered += glyph->mask[order[i]];
      if (covered * 2 >= glyph->sum) {
        median = order[i];
        break;
      }
    }
  }
  for (int c = 0; c < 3; ++c) {
    int weighted = 0;
    for (int i = 0; i < 16; ++i)
      weighted += samples[c][i] * glyph->mask[i];
    unsigned average =
        glyph->sum ? (unsigned)((weighted + glyph->sum / 2) / glyph->sum) : (unsigned)((sums[c] + 8) / 16);
    // Blend with a covered median pixel to reduce isolated outliers without
    // creating colors by independently taking a median of each channel.
    bool representative = !glyph->sum || luminance[median] * glyph->sum * 2 >= weighted_luma;
    channels[c] = representative ? (average + (unsigned)samples[c][median] + 1) / 2 : average;
    if (channels[c] > peak)
      peak = channels[c];
  }
  if (peak) {
    unsigned target = 128 + peak / 2;
    for (int c = 0; c < 3; ++c)
      channels[c] = (channels[c] * target + peak / 2) / peak;
  }
  return (channels[0] << 16) | (channels[1] << 8) | channels[2];
}

char *ascii_render_glyph_match(const image_t *image, ascii_color_t mode, const simd_cells_t *ops, const char *palette) {
  return ascii_render_glyph_match_mode(image, mode, ops, palette, false);
}

char *ascii_render_glyph_match_mode(const image_t *image, ascii_color_t mode, const simd_cells_t *ops,
                                    const char *palette, bool background) {
  return ascii_render_glyph_match_enhanced(image, mode, ops, palette, background, false);
}

static int glyph_lift_luma(int luma, bool enhance) {
  return enhance ? luma + (luma * (255 - luma) + 256) / 512 : luma;
}

static uint32_t glyph_ordered_dither(uint32_t rgb, int x, int y, ascii_color_t mode) {
  static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  int amplitude = mode == ASCII_COLOR_16 ? 32 : 16;
  int offset = ((int)bayer[y & 3][x & 3] * 2 - 15) * amplitude / 16;
  uint32_t result = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    int channel = (int)((rgb >> shift) & 255);
    if (channel > 0 && channel < 255) {
      channel += offset;
      if (channel < 0)
        channel = 0;
      if (channel > 255)
        channel = 255;
    }
    result |= (uint32_t)channel << shift;
  }
  return result;
}

char *ascii_render_glyph_match_enhanced(const image_t *image, ascii_color_t mode, const simd_cells_t *ops,
                                        const char *palette, bool background, bool enhance) {
  if (!image || !image->pixels || !ops || image->w <= 0 || image->h <= 0 || image->w % 8 || image->h % 8 ||
      mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid glyph matching input");
    return NULL;
  }
  bool allowed[GLYPH_COUNT] = {false};
  int candidates[GLYPH_COUNT];
  // Restrict the atlas to unique palette entries before scoring any cells.
  size_t candidate_count = 0;
  int (*shape_correlation)(const int16_t[16], const int16_t[16]) =
      ops->glyph_shape_correlation ? ops->glyph_shape_correlation : glyph_shape_correlation_scalar;
  if (palette) {
    if (!ascii_glyph_palette_supported(palette)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Glyph palette contains an unsupported character");
      return NULL;
    }
    for (const uint8_t *p = (const uint8_t *)palette; *p;) {
      size_t length;
      int index = glyph_palette_index(p, &length);
      if (!allowed[index]) {
        allowed[index] = true;
        candidates[candidate_count++] = index;
      }
      p += length;
    }
  } else {
    for (int i = 0; i < GLYPH_ASCII_COUNT; ++i) {
      allowed[i] = true;
      candidates[candidate_count++] = i;
    }
  }
  // Prepare coverage and its sufficient statistics once per frame.
  glyph_candidate_t glyphs[GLYPH_COUNT];
  // Precompute each mask's brightness and centered shape for correlation.
  for (size_t c = 0; c < candidate_count; ++c) {
    int g = candidates[c];
    glyph_candidate_t *glyph = &glyphs[c];
    glyph->key = glyph_key(g);
    uint8_t block_mask[16];
    const uint8_t *mask = g < GLYPH_ASCII_COUNT ? glyph_masks[g] : block_mask;
    if (g >= GLYPH_ASCII_COUNT)
      glyph_block_mask(g - GLYPH_ASCII_COUNT, block_mask);
    for (int i = 0; i < 64; ++i)
      glyph->fine[i] = g < GLYPH_ASCII_COUNT ? glyph_masks_fine[g][i] : mask[(i / 16) * 4 + (i % 8) / 2];
    glyph->sum = glyph->energy = 0;
    for (int i = 0; i < 16; ++i) {
      glyph->mask[i] = mask[i];
      glyph->sum += mask[i];
    }
    int mean = glyph->sum / 16;
    glyph->tone = glyph->sum * 255 / (g < GLYPH_ASCII_COUNT ? GLYPH_MASK_MAX_SUM : 16 * 255);
    for (int i = 0; i < 16; ++i) {
      glyph->centered[i] = (int16_t)((int)mask[i] - mean);
      glyph->energy += glyph->centered[i] * glyph->centered[i];
    }
  }
  image_t cells = *image;
  cells.w /= 8;
  cells.h /= 8;
  ascii_frame_t frame;
  if (ascii_frame_init(&frame, &cells, " .") != ASCIICHAT_OK)
    return NULL;
  frame.temporal_preselected = true;
  for (int y = 0; y < cells.h; ++y) {
    for (int x = 0; x < cells.w; ++x) {
      int16_t samples[3][16] = {{0}};
      int sums[3] = {0};
      uint8_t patch[16];
      int sample_mean = 0;
      for (int sy = 0; sy < 4; ++sy) {
        const rgb_pixel_t *top = image->pixels + (size_t)(y * 8 + sy * 2) * (size_t)image->w + (size_t)(x * 8);
        const rgb_pixel_t *bottom = top + image->w;
        for (int sx = 0; sx < 4; ++sx) {
          const rgb_pixel_t a = top[sx * 2], b = top[sx * 2 + 1];
          const rgb_pixel_t c = bottom[sx * 2], d = bottom[sx * 2 + 1];
          int i = sy * 4 + sx;
          int red = (a.r + b.r + c.r + d.r) / 4;
          int green = (a.g + b.g + c.g + d.g) / 4;
          int blue = (a.b + b.b + c.b + d.b) / 4;
          patch[i] = (uint8_t)glyph_lift_luma((77 * red + 150 * green + 29 * blue) >> 8, enhance);
          samples[0][i] = (int16_t)red;
          samples[1][i] = (int16_t)green;
          samples[2][i] = (int16_t)blue;
          sample_mean += patch[i];
          for (int channel = 0; channel < 3; ++channel)
            sums[channel] += samples[channel][i];
        }
      }
      // Compare local shape independently of the patch's average brightness.
      sample_mean /= 16;
      int16_t centered_samples[16];
      int sample_energy = 0;
      for (int i = 0; i < 16; ++i) {
        int centered = (int)patch[i] - sample_mean;
        centered_samples[i] = (int16_t)centered;
        sample_energy += centered * centered;
      }
      // Compare chromatic structure as well as luminance: red and green can
      // have the same brightness while forming a visible edge.
      int16_t chroma[2][16];
      int chroma_energy = 0;
      for (int i = 0; i < 16; ++i) {
        chroma[0][i] = (int16_t)((samples[0][i] - samples[1][i]) - (sums[0] - sums[1]) / 16);
        chroma[1][i] = (int16_t)((samples[2][i] - samples[1][i]) - (sums[2] - sums[1]) / 16);
        chroma_energy += chroma[0][i] * chroma[0][i] + chroma[1][i] * chroma[1][i];
      }
      size_t best = 0, previous_index = 0;
      uint64_t best_error = UINT64_MAX;
      uint64_t previous_error = UINT64_MAX;
      enum { GLYPH_SHORTLIST = 8 };
      size_t shortlist[GLYPH_SHORTLIST];
      uint64_t shortlist_error[GLYPH_SHORTLIST];
      size_t shortlist_count = 0;
      uint64_t prior_coarse_error = UINT64_MAX;
      ascii_temporal_cell_t *previous = frame.stabilizer ? &frame.stabilizer->cells[(size_t)y * frame.width + x] : NULL;
      for (size_t c = 0; c < candidate_count; ++c) {
        bool is_previous = previous && previous->valid && previous->glyph == glyphs[c].key;
        int tone_delta = sample_mean - glyphs[c].tone;
        int tone_error = 16 * tone_delta * tone_delta;
        // Shape and color residuals cannot lower the brightness error.
        if (!is_previous && shortlist_count == GLYPH_SHORTLIST &&
            (uint64_t)tone_error > shortlist_error[shortlist_count - 1])
          continue;
        int dot = shape_correlation(centered_samples, glyphs[c].centered);
        uint64_t error = (uint64_t)(tone_error + sample_energy + glyphs[c].energy - 2 * dot);
        if (mode != ASCII_COLOR_NONE && chroma_energy && glyphs[c].energy) {
          int64_t red_dot = shape_correlation(chroma[0], glyphs[c].centered);
          int64_t blue_dot = shape_correlation(chroma[1], glyphs[c].centered);
          int64_t explained = (red_dot * red_dot + blue_dot * blue_dot) / glyphs[c].energy;
          int64_t residual = chroma_energy - explained;
          if (residual > 0)
            error += (uint64_t)residual / 8;
        } else if (mode != ASCII_COLOR_NONE) {
          error += (uint64_t)chroma_energy / 8;
        }
        if (is_previous) {
          prior_coarse_error = error;
          previous_index = c;
        }
        size_t position = 0;
        while (position < shortlist_count && shortlist_error[position] <= error)
          ++position;
        if (position < GLYPH_SHORTLIST) {
          size_t end = shortlist_count < GLYPH_SHORTLIST ? shortlist_count++ : GLYPH_SHORTLIST - 1;
          for (size_t j = end; j > position; --j) {
            shortlist[j] = shortlist[j - 1];
            shortlist_error[j] = shortlist_error[j - 1];
          }
          shortlist[position] = c;
          shortlist_error[position] = error;
        }
      }
      // The second pass resolves near-ties with the original 8x8 samples.
      // Keep monochrome's established glyph scoring and its reference behavior.
      bool previous_scored = false;
      for (size_t n = 0; n < shortlist_count + (prior_coarse_error != UINT64_MAX ? 1u : 0u); ++n) {
        bool prior = n == shortlist_count;
        size_t index = prior ? previous_index : shortlist[n];
        if (prior && previous_scored)
          continue;
        uint64_t error = prior ? prior_coarse_error : shortlist_error[n];
        if (mode != ASCII_COLOR_NONE) {
          int fine_mean = 0;
          for (int i = 0; i < 64; ++i)
            fine_mean += glyphs[index].fine[i];
          fine_mean /= 64;
          uint64_t fine_error = 0;
          for (int sy = 0; sy < 8; ++sy) {
            const rgb_pixel_t *row = image->pixels + (size_t)(y * 8 + sy) * (size_t)image->w + (size_t)x * 8;
            for (int sx = 0; sx < 8; ++sx) {
              rgb_pixel_t p = row[sx];
              int luma = glyph_lift_luma((77 * p.r + 150 * p.g + 29 * p.b) >> 8, enhance);
              int delta = (luma - sample_mean) - ((int)glyphs[index].fine[sy * 8 + sx] - fine_mean);
              fine_error += (uint64_t)(delta * delta);
            }
          }
          error += fine_error / 16;
        }
        if (previous && previous->valid && previous->glyph == glyphs[index].key) {
          previous_error = error;
          previous_scored = true;
        }
        if (!prior && error < best_error) {
          best_error = error;
          best = index;
        }
      }
      uint32_t rgb = glyph_foreground(&glyphs[best], samples, sums);
      uint32_t bg_rgb =
          ((uint32_t)(sums[0] / 128) << 16) | ((uint32_t)(sums[1] / 128) << 8) | (uint32_t)(sums[2] / 128);
      uint32_t selected = glyphs[best].key;
      if (previous) {
        ascii_temporal_cell_t old = *previous;
        unsigned motion = 0;
        unsigned color_motion = 0;
        for (int i = 0; i < 16; ++i)
          motion += (unsigned)abs((int)patch[i] - (int)old.samples[i]);
        if (old.valid && mode != ASCII_COLOR_NONE) {
          for (int channel = 0; channel < 3; ++channel)
            for (int i = 0; i < 16; ++i)
              color_motion += (unsigned)abs(samples[channel][i] - (int)old.rgb_samples[channel][i]);
          color_motion /= 48;
        }
        uint64_t elapsed = frame.stabilizer->elapsed_ns;
        uint64_t effective_elapsed = color_motion > ASCII_TEMPORAL_MOTION_LIMIT ? UINT64_MAX : elapsed;
        selected = ascii_stabilizer_choose(previous, patch, selected, best_error, previous_error, rgb,
                                           effective_elapsed, &rgb);
        if (selected != glyphs[best].key) {
          *previous = old;
          selected = ascii_stabilizer_choose(previous, patch, glyphs[best].key, best_error, previous_error,
                                             glyph_foreground(&glyphs[previous_index], samples, sums),
                                             effective_elapsed, &rgb);
        }
        if (old.valid && motion / 16 <= ASCII_TEMPORAL_MOTION_LIMIT && color_motion <= ASCII_TEMPORAL_MOTION_LIMIT &&
            elapsed <= ASCII_TEMPORAL_MAX_INTERVAL_NS) {
          unsigned weight = (unsigned)(elapsed * 256 / (elapsed + ASCII_TEMPORAL_COLOR_TIME_NS));
          bg_rgb = temporal_blend_color(old.background, bg_rgb, weight);
        }
        for (int channel = 0; channel < 3; ++channel)
          for (int i = 0; i < 16; ++i)
            previous->rgb_samples[channel][i] = (uint8_t)samples[channel][i];
        previous->background = bg_rgb;
      }
      frame.glyph[x] = selected;
      frame.packed[x] = rgb;
      frame.index[x] = bg_rgb;
      frame.luma[x] = (uint8_t)((77 * ((rgb >> 16) & 255) + 150 * ((rgb >> 8) & 255) + 29 * (rgb & 255)) >> 8);
    }
    // Anchor the ordered pattern to terminal cells so stationary video does
    // not shimmer; truecolor and monochrome output remain unchanged.
    if (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256) {
      for (int x = 0; x < cells.w; ++x) {
        frame.packed[x] = glyph_ordered_dither(frame.packed[x], x, y, mode);
        if (background)
          frame.index[x] = glyph_ordered_dither(frame.index[x], x, y, mode);
      }
    }
    if (background && mode != ASCII_COLOR_NONE) {
      if (mode == ASCII_COLOR_TRUE) {
        memcpy(frame.fg, frame.packed, frame.width * sizeof(uint32_t));
        memcpy(frame.bg, frame.index, frame.width * sizeof(uint32_t));
      } else {
        ops->quantize(frame.packed, frame.fg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
        ops->quantize(frame.index, frame.bg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
      }
      emit_prepared_row(&frame.output, mode, ops, frame.glyph, frame.fg, frame.bg, frame.width);
      emit_row_end(&frame.output, true, y + 1 < cells.h);
      ++frame.row;
    } else {
      if (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256)
        ops->quantize(frame.packed, frame.index, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
      ascii_frame_emit_row(&frame, mode, false, y + 1 < cells.h, ops);
    }
  }
  return ascii_frame_finish(&frame);
}
