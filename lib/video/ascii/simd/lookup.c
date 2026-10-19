// Restored vector table lookup, shared by mono and color rendering.
#include "vector.h"
void FN(simd_lookup_)(const uint8_t *luma, const uint32_t table[64], uint32_t *out, size_t count) {
#if !defined(SIMD_CELL_SVE)
#if defined(SIMD_CELL_NEON) || defined(SIMD_CELL_SSSE3)
  const size_t minimum = 16;
#elif defined(SIMD_CELL_AVX2)
  const size_t minimum = 8;
#else
  const size_t minimum = 4;
#endif
  if (count < minimum) {
    simd_lookup_scalar(luma, table, out, count);
    return;
  }
#endif
  size_t i = 0;
#if defined(SIMD_CELL_SVE)
  for (; i < count; i += svcntw()) {
    svbool_t pg = svwhilelt_b32((uint64_t)i, (uint64_t)count);
    svuint32_t index = svlsr_n_u32_x(pg, svld1ub_u32(pg, luma + i), 2);
    svst1_u32(pg, out + i, svld1_gather_u32index_u32(pg, table, index));
  }
#elif defined(SIMD_CELL_NEON)
  // Deinterleave the four UTF-8 bytes of all 64 palette entries into four tables.
  uint8x16x4_t tables[4];
  for (unsigned group = 0; group < 4; ++group) {
    uint8x16x4_t entries = vld4q_u8((const uint8_t *)(table + 16 * group));
    for (unsigned byte = 0; byte < 4; ++byte)
      tables[byte].val[group] = entries.val[byte];
  }
  for (; count - i >= 16; i += 16) {
    uint8x16_t index = vshrq_n_u8(vld1q_u8(luma + i), 2);
    uint8x16x4_t glyph;
    for (unsigned byte = 0; byte < 4; ++byte) {
#if defined(__aarch64__)
      glyph.val[byte] = vqtbl4q_u8(tables[byte], index);
#else
      uint8x8x4_t low = {{vget_low_u8(tables[byte].val[0]), vget_high_u8(tables[byte].val[0]),
                          vget_low_u8(tables[byte].val[1]), vget_high_u8(tables[byte].val[1])}};
      uint8x8x4_t high = {{vget_low_u8(tables[byte].val[2]), vget_high_u8(tables[byte].val[2]),
                           vget_low_u8(tables[byte].val[3]), vget_high_u8(tables[byte].val[3])}};
      uint8x8_t il = vget_low_u8(index), ih = vget_high_u8(index);
      glyph.val[byte] = vcombine_u8(vorr_u8(vtbl4_u8(low, il), vtbl4_u8(high, vsub_u8(il, vdup_n_u8(32)))),
                                    vorr_u8(vtbl4_u8(low, ih), vtbl4_u8(high, vsub_u8(ih, vdup_n_u8(32)))));
#endif
    }
    vst4q_u8((uint8_t *)(out + i), glyph);
  }
#elif defined(SIMD_CELL_AVX2)
  for (; count - i >= 8; i += 8) {
    __m256i index = _mm256_srli_epi32(_mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i *)(luma + i))), 2);
    _mm256_storeu_si256((__m256i *)(out + i), _mm256_i32gather_epi32((const int *)table, index, 4));
  }
#elif defined(SIMD_CELL_SSSE3)
  uint8_t bytes[4][64];
  for (unsigned entry = 0; entry < 64; ++entry) {
    const uint8_t *key = (const uint8_t *)(table + entry);
    for (unsigned byte = 0; byte < 4; ++byte)
      bytes[byte][entry] = key[byte];
  }
  for (; count - i >= 16; i += 16) {
    __m128i index = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((const __m128i *)(luma + i)), 2), _mm_set1_epi8(63));
    __m128i low = _mm_and_si128(index, _mm_set1_epi8(15));
    __m128i high = _mm_and_si128(index, _mm_set1_epi8(48));
    __m128i glyph[4];
    for (unsigned byte = 0; byte < 4; ++byte) {
      glyph[byte] = _mm_setzero_si128();
      for (unsigned group = 0; group < 4; ++group) {
        __m128i lut = _mm_loadu_si128((const __m128i *)(bytes[byte] + 16 * group));
        __m128i selected =
            _mm_and_si128(_mm_shuffle_epi8(lut, low), _mm_cmpeq_epi8(high, _mm_set1_epi8((char)(16 * group))));
        glyph[byte] = _mm_or_si128(glyph[byte], selected);
      }
    }
    __m128i a = _mm_unpacklo_epi8(glyph[0], glyph[1]), b = _mm_unpacklo_epi8(glyph[2], glyph[3]);
    __m128i c = _mm_unpackhi_epi8(glyph[0], glyph[1]), d = _mm_unpackhi_epi8(glyph[2], glyph[3]);
    _mm_storeu_si128((__m128i *)(out + i), _mm_unpacklo_epi16(a, b));
    _mm_storeu_si128((__m128i *)(out + i + 4), _mm_unpackhi_epi16(a, b));
    _mm_storeu_si128((__m128i *)(out + i + 8), _mm_unpacklo_epi16(c, d));
    _mm_storeu_si128((__m128i *)(out + i + 12), _mm_unpackhi_epi16(c, d));
  }
#else
  unsigned starts[64], ends[64], groups = 0;
  uint32_t values[64];
  for (unsigned first = 0; first < 64;) {
    unsigned end = first + 1;
    while (end < 64 && table[end] == table[first])
      ++end;
    starts[groups] = first;
    ends[groups] = end;
    values[groups] = table[first];
    ++groups;
    first = end;
  }
  for (; count - i >= 4; i += 4) {
    uint32_t input;
    memcpy(&input, luma + i, 4);
    __m128i zero = _mm_setzero_si128();
    vec_t index =
        (vec_t)_mm_srli_epi32(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)input), zero), zero), 2);
    vec_t glyph = SET(0);
    // SSE2 has no byte shuffle or gather. Compare ranges of equal palette entries
    // in parallel, rather than replacing the stage with four scalar lookups.
    for (unsigned group = 0; group < groups; ++group) {
      vec_t in_range = BOTH(LT(SET((int)starts[group] - 1), index), LT(index, SET((int)ends[group])));
      glyph = SELECT(in_range, SET((int32_t)values[group]), glyph);
    }
    STORE(out + i, glyph);
  }
#endif
  if (i < count)
    simd_lookup_scalar(luma + i, table, out + i, count - i);
}
