#pragma once

/* Existing buffer budgets in bytes, including NUL for text storage. */
#define ASCII_INLINE_SCRATCH_COLUMNS 512
#define ASCII_SCRATCH_PLANES 6

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

/** A renderer-owned history; one instance must not be shared by independent streams. */
typedef struct {
  uint8_t samples[16];
  uint32_t glyph;
  uint32_t color;
  uint64_t source_owner;
  bool valid;
} ascii_temporal_cell_t;

typedef struct {
  ascii_temporal_cell_t *cells;
  size_t width, height;
  uint64_t configuration;
  uint64_t elapsed_ns;
  uint64_t last_timestamp_ns;
  uint64_t fixed_step_ns;
} ascii_stabilizer_t;

/** Prepare history for a viewport and rendering contract, resetting it on change. */
asciichat_error_t ascii_stabilizer_prepare(ascii_stabilizer_t *state, size_t width, size_t height,
                                           uint64_t configuration);
void ascii_stabilizer_reset(ascii_stabilizer_t *state);
/** Select history for the current thread until the previous target is restored. */
ascii_stabilizer_t *ascii_stabilizer_set_target(ascii_stabilizer_t *state);
/** Choose a glyph using current and previous candidate errors and patch motion. */
uint32_t ascii_stabilizer_choose(ascii_temporal_cell_t *cell, const uint8_t samples[16], uint32_t candidate,
                                 uint64_t candidate_error, uint64_t previous_error, uint32_t color, uint64_t elapsed_ns,
                                 uint32_t *stabilized_color);
char *ascii_render(const image_t *image, const char *palette, ascii_color_t color, bool background, bool dither,
                   const simd_cells_t *ops);

bool ascii_glyph_palette_supported(const char *palette);
char *ascii_render_glyph_match(const image_t *image, ascii_color_t color, const simd_cells_t *ops, const char *palette);

/** Two source rows per terminal row; the supplied backend processes both rows. */
char *ascii_render_halfblocks(const uint8_t *rgb, int width, int height, int stride_bytes, ascii_color_t color,
                              const simd_cells_t *ops);

/** Prepared row storage shared by ISA-owned processing loops. No pixel work occurs here. */
typedef struct {
  outbuf_t output;
  outbuf_t *output_target;
  size_t width;
  uint32_t table[64];
  uint32_t *scratch, *packed, *index, *glyph, *fg, *bg;
  uint8_t *luma;
  ascii_stabilizer_t *stabilizer;
  size_t row;
  bool temporal_preselected;
  bool dithered;
  // A terminal row normally fits on the stack; unusually wide images retain
  // the heap fallback. Each invocation owns its scratch, including reentry.
  _Alignas(64) uint32_t inline_scratch[ASCII_INLINE_SCRATCH_COLUMNS * ASCII_SCRATCH_PLANES];
} ascii_frame_t;
asciichat_error_t ascii_frame_init(ascii_frame_t *frame, const image_t *image, const char *palette);
void ascii_frame_emit_row(ascii_frame_t *frame, ascii_color_t mode, bool background, bool more_rows,
                          const simd_cells_t *ops);
char *ascii_frame_finish(ascii_frame_t *frame);
#define DECLARE_ASCII_PROCESSOR(isa) char *render_ascii_cells_##isa(const image_t *, const char *, ascii_color_t, bool)
DECLARE_ASCII_PROCESSOR(sse2);
DECLARE_ASCII_PROCESSOR(ssse3);
DECLARE_ASCII_PROCESSOR(avx2);
DECLARE_ASCII_PROCESSOR(neon);
DECLARE_ASCII_PROCESSOR(sve);
#undef DECLARE_ASCII_PROCESSOR

asciichat_error_t ascii_half_frame_init(ascii_frame_t *frame, const uint8_t *rgb, int width, int height, int *stride,
                                        ascii_color_t mode);
void ascii_frame_emit_halfrow(ascii_frame_t *frame, ascii_color_t mode, bool more_rows, const simd_cells_t *ops);
#define DECLARE_HALF_PROCESSOR(isa) char *render_halfblocks_##isa(const uint8_t *, int, int, int, ascii_color_t)
DECLARE_HALF_PROCESSOR(sse2);
DECLARE_HALF_PROCESSOR(ssse3);
DECLARE_HALF_PROCESSOR(avx2);
DECLARE_HALF_PROCESSOR(neon);
DECLARE_HALF_PROCESSOR(sve);
#undef DECLARE_HALF_PROCESSOR

/** Internal one-shot output target, consumed by the next frame initializer.
 * Returns the prior target so capability conversion can restore its scope.
 */
outbuf_t *ascii_render_set_output_target(outbuf_t *target);
