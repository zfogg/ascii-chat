#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <ascii-chat/video/ascii/simd/cells.h>
#include <immintrin.h>
#include <string.h>
#include <ascii-chat/video/ascii/avx2/common.h>

int glyph_shape_correlation_avx2(const int16_t samples[16], const int16_t mask[16]) {
  __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)samples);
  __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)mask);
  __m256i products = _mm256_madd_epi16(a, b);
  __m128i sums = _mm_add_epi32(_mm256_castsi256_si128(products), _mm256_extracti128_si256(products, 1));
  int32_t lanes[4];
  _mm_storeu_si128((__m128i *)(void *)lanes, sums);
  return lanes[0] + lanes[1] + lanes[2] + lanes[3];
}
void simd_luminance_avx2(const uint8_t *rgb, uint8_t *out, size_t width) {
  const rgb_pixel_t *row = (const rgb_pixel_t *)rgb;
  size_t x = 0;
  for (; width - x >= 32; x += 32) {
    avx2_load_rgb32_optimized(row + x, avx2_r_buffer, avx2_g_buffer, avx2_b_buffer);
    avx2_compute_luminance_32(avx2_r_buffer, avx2_g_buffer, avx2_b_buffer, avx2_luminance_buffer);
    memcpy(out + x, avx2_luminance_buffer, 32);
  }
  simd_luminance_scalar((const uint8_t *)(row + x), out + x, width - x);
}
