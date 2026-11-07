#pragma once
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <ascii-chat/video/ascii/output_buffer.h>

/** SIMD stages operate on complete rows of host-order packed RGB and UTF-8 keys.
 * Row callbacks read/write exactly count elements. Glyph correlation reads 16 samples.
 * Only short row tails use scalar code.
 */
typedef struct {
  simd_luminance_fn luminance;
  void (*pack)(const uint8_t *rgb, uint32_t *out, size_t count);
  void (*lookup)(const uint8_t *luma, const uint32_t table[64], uint32_t *out, size_t count);
  void (*quantize)(const uint32_t *rgb, uint32_t *out, size_t count, unsigned colors);
  void (*halfblock)(const uint32_t *top, const uint32_t *bottom, uint32_t *glyph, uint32_t *fg, uint32_t *bg,
                    size_t count, unsigned mode, const uint32_t keys[5]);
  size_t (*run)(const uint32_t *glyph, const uint32_t *fg, const uint32_t *bg, size_t count);
  void (*emit_mono)(outbuf_t *out, const uint32_t *glyph, size_t count);
  // The 16 inputs are centered byte values in [-255, 255].
  int (*glyph_shape_correlation)(const int16_t samples[16], const int16_t mask[16]);
} simd_cells_t;

int glyph_shape_correlation_scalar(const int16_t samples[16], const int16_t mask[16]);
int glyph_shape_correlation_sse2(const int16_t samples[16], const int16_t mask[16]);
int glyph_shape_correlation_avx2(const int16_t samples[16], const int16_t mask[16]);
int glyph_shape_correlation_neon(const int16_t samples[16], const int16_t mask[16]);
int glyph_shape_correlation_sve(const int16_t samples[16], const int16_t mask[16]);

const simd_cells_t *simd_cells_backend(void);
#define DECLARE_SIMD_CELLS(isa)                                                                                        \
  void simd_pack_##isa(const uint8_t *, uint32_t *, size_t);                                                           \
  void simd_lookup_##isa(const uint8_t *, const uint32_t[64], uint32_t *, size_t);                                     \
  void simd_quantize_##isa(const uint32_t *, uint32_t *, size_t, unsigned);                                            \
  void simd_halfblock_##isa(const uint32_t *, const uint32_t *, uint32_t *, uint32_t *, uint32_t *, size_t, unsigned,  \
                            const uint32_t[5]);                                                                        \
  size_t simd_run_##isa(const uint32_t *, const uint32_t *, const uint32_t *, size_t)
DECLARE_SIMD_CELLS(scalar);
DECLARE_SIMD_CELLS(sse2);
DECLARE_SIMD_CELLS(ssse3);
DECLARE_SIMD_CELLS(avx2);
DECLARE_SIMD_CELLS(neon);
DECLARE_SIMD_CELLS(sve);
#undef DECLARE_SIMD_CELLS

extern const simd_cells_t simd_cells_scalar;
extern const simd_cells_t simd_cells_sse2;
extern const simd_cells_t simd_cells_ssse3;
extern const simd_cells_t simd_cells_avx2;
extern const simd_cells_t simd_cells_neon;
extern const simd_cells_t simd_cells_sve;

void simd_emit_mono_neon(outbuf_t *out, const uint32_t *glyph, size_t count);
