// RGB packing is the pixel-processing stage used by every half-block mode.
#include "vector.h"
void FN(simd_pack_)(const uint8_t *rgb, uint32_t *out, size_t count) {
  size_t i = 0;
#if defined(SIMD_CELL_NEON)
  // Retain the original NEON half-block renderer's interleaved RGB vector loads.
  for (; count - i >= 16; i += 16) {
    uint8x16x3_t pixel = vld3q_u8(rgb + 3 * i);
    uint8x16x4_t packed = {{pixel.val[2], pixel.val[1], pixel.val[0], vdupq_n_u8(0)}};
    vst4q_u8((uint8_t *)(out + i), packed);
  }
#elif defined(SIMD_CELL_SVE)
  for (; i < count; i += svcntw()) {
    size_t active = count - i < svcntw() ? count - i : svcntw();
    svbool_t bytes = svwhilelt_b8((uint64_t)0, (uint64_t)active);
    svbool_t pg = svwhilelt_b32((uint64_t)i, (uint64_t)count);
    svuint8x3_t pixel = svld3_u8(bytes, rgb + 3 * i);
    svuint32_t r = svunpklo_u32(svunpklo_u16(svget3_u8(pixel, 0)));
    svuint32_t g = svunpklo_u32(svunpklo_u16(svget3_u8(pixel, 1)));
    svuint32_t b = svunpklo_u32(svunpklo_u16(svget3_u8(pixel, 2)));
    svuint32_t packed = svorr_u32_x(pg, svlsl_n_u32_x(pg, r, 16), svorr_u32_x(pg, svlsl_n_u32_x(pg, g, 8), b));
    svst1_u32(pg, out + i, packed);
  }
#elif defined(SIMD_CELL_AVX2) || defined(SIMD_CELL_SSSE3)
#if defined(SIMD_CELL_AVX2)
  const size_t pixels = 8;
#else
  const size_t pixels = 4;
#endif
  const __m128i shuffle = _mm_setr_epi8(2, 1, 0, -128, 5, 4, 3, -128, 8, 7, 6, -128, 11, 10, 9, -128);
  for (; count - i >= pixels; i += pixels) {
    uint8_t low[16] = {0};
    memcpy(low, rgb + 3 * i, 12);
    __m128i first = _mm_loadu_si128((const __m128i *)low);
#if defined(SIMD_CELL_AVX2)
    uint8_t high[16] = {0};
    memcpy(high, rgb + 3 * i + 12, 12);
    __m256i both = _mm256_inserti128_si256(_mm256_castsi128_si256(first), _mm_loadu_si128((const __m128i *)high), 1);
    __m256i mask = _mm256_broadcastsi128_si256(shuffle);
    _mm256_storeu_si256((__m256i *)(out + i), _mm256_shuffle_epi8(both, mask));
#else
    _mm_storeu_si128((__m128i *)(out + i), _mm_shuffle_epi8(first, shuffle));
#endif
  }
#else
  for (; count - i >= 4; i += 4) {
    // SSE2 retains its deinterleave stage, then packs four RGB cells together.
    int32_t r[4], g[4], b[4];
    for (unsigned j = 0; j < 4; ++j) {
      r[j] = rgb[3 * (i + j)];
      g[j] = rgb[3 * (i + j) + 1];
      b[j] = rgb[3 * (i + j) + 2];
    }
    vec_t packed = OR(SHL(LOAD(r), 16), OR(SHL(LOAD(g), 8), LOAD(b)));
    STORE(out + i, packed);
  }
#endif
  if (i < count)
    simd_pack_scalar(rgb + 3 * i, out + i, count - i);
}

void FN(simd_halfblock_)(const uint32_t *top, const uint32_t *bottom, uint32_t *glyph, uint32_t *fg, uint32_t *bg,
                         size_t count, unsigned mode, const uint32_t keys[5]) {
  size_t i = 0;
  for (; HAS_BLOCK; i += LANES) {
    BEGIN_BLOCK;
    (void)active;
    vec_t t = LOAD(top + i), b = LOAD(bottom + i), key = SET((int32_t)keys[4]);
#if defined(SIMD_CELL_SVE)
    svbool_t transparent;
#else
    vec_t transparent;
#endif
    if (mode == 0) {
      vec_t yt = SHR(
          ADD(ADD(MUL(SHR(t, 16), SET(76)), MUL(AND(SHR(t, 8), SET(255)), SET(150))), MUL(AND(t, SET(255)), SET(29))),
          8);
      vec_t yb = SHR(
          ADD(ADD(MUL(SHR(b, 16), SET(76)), MUL(AND(SHR(b, 8), SET(255)), SET(150))), MUL(AND(b, SET(255)), SET(29))),
          8);
      transparent = BOTH(LT(yt, SET(16)), LT(yb, SET(16)));
      vec_t shade = SHR(yt, 6);
      key = SET((int32_t)keys[0]);
      for (int k = 1; k < 4; ++k)
        key = SELECT(EQ(shade, SET(k)), SET((int32_t)keys[k]), key);
      STORE(fg + i, SET(-1));
      STORE(bg + i, SET(-1));
    } else {
      transparent = BOTH(EQ(t, SET(0)), EQ(b, SET(0)));
      STORE(fg + i, SELECT(transparent, SET(-1), LOAD(fg + i)));
      STORE(bg + i, SELECT(transparent, SET(-1), LOAD(bg + i)));
    }
    STORE(glyph + i, SELECT(transparent, SET(' '), key));
  }
  if (i < count)
    simd_halfblock_scalar(top + i, bottom + i, glyph + i, fg + i, bg + i, count - i, mode, keys);
}
