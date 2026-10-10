// This translation unit is compiled separately for each ISA. No ISA types cross its API.
#include <ascii-chat/video/ascii/simd/dispatch.h>

#if defined(SIMD_KERNEL_SVE)
#include <arm_sve.h>
void simd_luminance_sve(const uint8_t *rgb, uint8_t *out, size_t count) {
  // Predicated RGB structure loads and narrowing stores need no vector scratch buffers.
  // svcnth() is evaluated on the calling thread, whose SVE vector length may differ.
  for (size_t i = 0; i < count; i += svcnth()) {
    svbool_t pg = svwhilelt_b16((uint64_t)i, (uint64_t)count);
    // SVE1 has no u16 byte gather: deinterleave with a predicated structure load.
    svbool_t bytes = svwhilelt_b8((uint64_t)0, (uint64_t)((count - i < svcnth()) ? count - i : svcnth()));
    svuint8x3_t pixels = svld3_u8(bytes, rgb + 3 * i);
    svuint16_t r = svunpklo_u16(svget3_u8(pixels, 0));
    svuint16_t g = svunpklo_u16(svget3_u8(pixels, 1));
    svuint16_t b = svunpklo_u16(svget3_u8(pixels, 2));
    svuint16_t y = svmul_n_u16_x(pg, r, 77);
    y = svmla_n_u16_x(pg, y, g, 150);
    y = svmla_n_u16_x(pg, y, b, 29);
    y = svlsr_n_u16_x(pg, svadd_n_u16_x(pg, y, 128), 8);
    svst1b_u16(pg, out + i, y);
  }
}
#elif defined(SIMD_KERNEL_NEON)
#include <arm_neon.h>
void simd_luminance_neon(const uint8_t *rgb, uint8_t *out, size_t count) {
  size_t i = 0;
  for (; count - i >= 8; i += 8) {
    uint8x8x3_t p = vld3_u8(rgb + 3 * i);
    uint16x8_t y = vmull_u8(p.val[0], vdup_n_u8(77));
    y = vmlal_u8(y, p.val[1], vdup_n_u8(150));
    y = vmlal_u8(y, p.val[2], vdup_n_u8(29));
    vst1_u8(out + i, vshrn_n_u16(vaddq_u16(y, vdupq_n_u16(128)), 8));
  }
  simd_luminance_scalar(rgb + 3 * i, out + i, count - i);
}
#else
#include <immintrin.h>
#include <string.h>

#if defined(SIMD_KERNEL_AVX2)
#define LUMINANCE_FN simd_luminance_avx2
#elif defined(SIMD_KERNEL_SSSE3)
#define LUMINANCE_FN simd_luminance_ssse3
#else
#define LUMINANCE_FN simd_luminance_sse2
#endif

void LUMINANCE_FN(const uint8_t *rgb, uint8_t *out, size_t count) {
  size_t i = 0;
#if defined(SIMD_KERNEL_AVX2)
  for (; count - i >= 16; i += 16) {
    uint8_t r[16], g[16], b[16];
    for (size_t j = 0; j < 16; j++) {
      r[j] = rgb[3 * (i + j)];
      g[j] = rgb[3 * (i + j) + 1];
      b[j] = rgb[3 * (i + j) + 2];
    }
    __m256i y = _mm256_mullo_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)r)), _mm256_set1_epi16(77));
    y = _mm256_add_epi16(y, _mm256_mullo_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)g)), _mm256_set1_epi16(150)));
    y = _mm256_add_epi16(y, _mm256_mullo_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)b)), _mm256_set1_epi16(29)));
    y = _mm256_srli_epi16(_mm256_add_epi16(y, _mm256_set1_epi16(128)), 8);
    _mm_storeu_si128((__m128i *)(out + i), _mm_packus_epi16(_mm256_castsi256_si128(y), _mm256_extracti128_si256(y, 1)));
  }
#else
  for (; count - i >= 8; i += 8) {
    __m128i r, g, b;
#if defined(SIMD_KERNEL_SSSE3)
    // Two exact 12-byte loads cover eight RGB pixels without reading past the row.
    uint8_t low[16] = {0}, high[16] = {0};
    memcpy(low, rgb + 3 * i, 12);
    memcpy(high, rgb + 3 * i + 12, 12);
    __m128i a = _mm_loadu_si128((const __m128i *)low), c = _mm_loadu_si128((const __m128i *)high);
    __m128i mask = _mm_setr_epi8(0, 3, 6, 9, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128, -128);
    r = _mm_unpacklo_epi32(_mm_shuffle_epi8(a, mask), _mm_shuffle_epi8(c, mask));
    mask = _mm_add_epi8(mask, _mm_set1_epi8(1));
    g = _mm_unpacklo_epi32(_mm_shuffle_epi8(a, mask), _mm_shuffle_epi8(c, mask));
    mask = _mm_add_epi8(mask, _mm_set1_epi8(1));
    b = _mm_unpacklo_epi32(_mm_shuffle_epi8(a, mask), _mm_shuffle_epi8(c, mask));
#else
    uint8_t red[8], green[8], blue[8];
    for (size_t j = 0; j < 8; j++) {
      red[j] = rgb[3 * (i + j)];
      green[j] = rgb[3 * (i + j) + 1];
      blue[j] = rgb[3 * (i + j) + 2];
    }
    r = _mm_loadl_epi64((const __m128i *)red);
    g = _mm_loadl_epi64((const __m128i *)green);
    b = _mm_loadl_epi64((const __m128i *)blue);
#endif
    __m128i zero = _mm_setzero_si128();
    __m128i y = _mm_mullo_epi16(_mm_unpacklo_epi8(r, zero), _mm_set1_epi16(77));
    y = _mm_add_epi16(y, _mm_mullo_epi16(_mm_unpacklo_epi8(g, zero), _mm_set1_epi16(150)));
    y = _mm_add_epi16(y, _mm_mullo_epi16(_mm_unpacklo_epi8(b, zero), _mm_set1_epi16(29)));
    y = _mm_srli_epi16(_mm_add_epi16(y, _mm_set1_epi16(128)), 8);
    _mm_storel_epi64((__m128i *)(out + i), _mm_packus_epi16(y, zero));
  }
#endif
  simd_luminance_scalar(rgb + 3 * i, out + i, count - i);
}
#endif
