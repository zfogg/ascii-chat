#include <ascii-chat/video/ascii/simd/cells.h>
#include <ascii-chat/video/terminal/ansi.h>

void simd_pack_scalar(const uint8_t *rgb, uint32_t *out, size_t count) {
  for (size_t i = 0; i < count; ++i)
    out[i] = ((uint32_t)rgb[3 * i] << 16) | ((uint32_t)rgb[3 * i + 1] << 8) | rgb[3 * i + 2];
}
void simd_lookup_scalar(const uint8_t *luma, const uint32_t table[64], uint32_t *out, size_t count) {
  for (size_t i = 0; i < count; ++i)
    out[i] = table[luma[i] >> 2];
}
void simd_quantize_scalar(const uint32_t *rgb, uint32_t *out, size_t count, unsigned colors) {
  for (size_t i = 0; i < count; ++i) {
    uint8_t r = (uint8_t)(rgb[i] >> 16), g = (uint8_t)(rgb[i] >> 8), b = (uint8_t)rgb[i];
    out[i] = colors == 16 ? rgb_to_16color(r, g, b) : rgb_to_256color(r, g, b);
  }
}
size_t simd_run_scalar(const uint32_t *glyph, const uint32_t *fg, const uint32_t *bg, size_t count) {
  size_t i = 1;
  for (; i < count && glyph[i] == glyph[0] && fg[i] == fg[0] && bg[i] == bg[0]; ++i) {
  }
  return count ? i : 0;
}
void simd_halfblock_scalar(const uint32_t *top, const uint32_t *bottom, uint32_t *glyph, uint32_t *fg, uint32_t *bg,
                           size_t count, unsigned mode, const uint32_t keys[5]) {
  for (size_t i = 0; i < count; ++i) {
    uint32_t t = top[i], b = bottom[i];
    unsigned yt = (76 * (t >> 16) + 150 * ((t >> 8) & 255) + 29 * (t & 255)) >> 8;
    unsigned yb = (76 * (b >> 16) + 150 * ((b >> 8) & 255) + 29 * (b & 255)) >> 8;
    bool transparent = mode == 0 ? yt < 16 && yb < 16 : t == 0 && b == 0;
    glyph[i] = transparent ? (uint32_t)' ' : mode == 0 ? keys[yt >> 6] : keys[4];
    if (mode == 0 || transparent)
      fg[i] = bg[i] = UINT32_MAX;
  }
}
#define CELLS(isa) {simd_pack_##isa, simd_lookup_##isa, simd_quantize_##isa, simd_halfblock_##isa, simd_run_##isa}
#if SIMD_SUPPORT_SSE2
const simd_cells_t simd_cells_sse2 = CELLS(sse2);
#endif
#if SIMD_SUPPORT_SSSE3
const simd_cells_t simd_cells_ssse3 = CELLS(ssse3);
#endif
#if SIMD_SUPPORT_AVX2
const simd_cells_t simd_cells_avx2 = CELLS(avx2);
#endif
#if SIMD_SUPPORT_NEON
const simd_cells_t simd_cells_neon = CELLS(neon);
#endif
#if SIMD_SUPPORT_SVE
const simd_cells_t simd_cells_sve = CELLS(sve);
#endif
const simd_cells_t simd_cells_scalar = CELLS(scalar);
const simd_cells_t *simd_cells_for_luminance(simd_luminance_fn luminance) {
  (void)luminance;
#if SIMD_SUPPORT_SSE2
  if (luminance == simd_luminance_sse2)
    return &simd_cells_sse2;
#endif
#if SIMD_SUPPORT_SSSE3
  if (luminance == simd_luminance_ssse3)
    return &simd_cells_ssse3;
#endif
#if SIMD_SUPPORT_AVX2
  if (luminance == simd_luminance_avx2)
    return &simd_cells_avx2;
#endif
#if SIMD_SUPPORT_NEON
  if (luminance == simd_luminance_neon)
    return &simd_cells_neon;
#endif
#if SIMD_SUPPORT_SVE
  if (luminance == simd_luminance_sve)
    return &simd_cells_sve;
#endif
  return &simd_cells_scalar;
}
