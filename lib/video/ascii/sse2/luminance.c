#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <string.h>
#include <immintrin.h>
void simd_luminance_sse2(const uint8_t *rgb, uint8_t *out, size_t width) {
  const rgb_pixel_t *row = (const rgb_pixel_t *)rgb;
  size_t x = 0;
  for (; width - x >= 16; x += 16) {
    // Manual deinterleave RGB components (SSE2 limitation vs NEON's vld3q_u8)
    uint8_t r_array[16], g_array[16], b_array[16];
    for (int j = 0; j < 16; j++) {
      r_array[j] = row[x + j].r;
      g_array[j] = row[x + j].g;
      b_array[j] = row[x + j].b;
    }

    // Load full 16 bytes into SSE2 registers (process in two 8-pixel batches)
    __m128i r_vec_lo = _mm_loadl_epi64((__m128i *)(r_array + 0)); // First 8 pixels
    __m128i r_vec_hi = _mm_loadl_epi64((__m128i *)(r_array + 8)); // Second 8 pixels
    __m128i g_vec_lo = _mm_loadl_epi64((__m128i *)(g_array + 0));
    __m128i g_vec_hi = _mm_loadl_epi64((__m128i *)(g_array + 8));
    __m128i b_vec_lo = _mm_loadl_epi64((__m128i *)(b_array + 0));
    __m128i b_vec_hi = _mm_loadl_epi64((__m128i *)(b_array + 8));

    // Process first 8 pixels
    __m128i r_16_lo = _mm_unpacklo_epi8(r_vec_lo, _mm_setzero_si128());
    __m128i g_16_lo = _mm_unpacklo_epi8(g_vec_lo, _mm_setzero_si128());
    __m128i b_16_lo = _mm_unpacklo_epi8(b_vec_lo, _mm_setzero_si128());

    __m128i luma_r_lo = _mm_mullo_epi16(r_16_lo, _mm_set1_epi16(LUMA_RED));
    __m128i luma_g_lo = _mm_mullo_epi16(g_16_lo, _mm_set1_epi16(150));
    __m128i luma_b_lo = _mm_mullo_epi16(b_16_lo, _mm_set1_epi16(29));

    __m128i luma_sum_lo = _mm_add_epi16(luma_r_lo, luma_g_lo);
    luma_sum_lo = _mm_add_epi16(luma_sum_lo, luma_b_lo);
    luma_sum_lo = _mm_add_epi16(luma_sum_lo, _mm_set1_epi16(128));
    luma_sum_lo = _mm_srli_epi16(luma_sum_lo, 8);

    // Process second 8 pixels
    __m128i r_16_hi = _mm_unpacklo_epi8(r_vec_hi, _mm_setzero_si128());
    __m128i g_16_hi = _mm_unpacklo_epi8(g_vec_hi, _mm_setzero_si128());
    __m128i b_16_hi = _mm_unpacklo_epi8(b_vec_hi, _mm_setzero_si128());

    __m128i luma_r_hi = _mm_mullo_epi16(r_16_hi, _mm_set1_epi16(LUMA_RED));
    __m128i luma_g_hi = _mm_mullo_epi16(g_16_hi, _mm_set1_epi16(150));
    __m128i luma_b_hi = _mm_mullo_epi16(b_16_hi, _mm_set1_epi16(29));

    __m128i luma_sum_hi = _mm_add_epi16(luma_r_hi, luma_g_hi);
    luma_sum_hi = _mm_add_epi16(luma_sum_hi, luma_b_hi);
    luma_sum_hi = _mm_add_epi16(luma_sum_hi, _mm_set1_epi16(128));
    luma_sum_hi = _mm_srli_epi16(luma_sum_hi, 8);

    // Pack both halves to 8-bit
    __m128i luminance_lo = _mm_packus_epi16(luma_sum_lo, _mm_setzero_si128());
    __m128i luminance_hi = _mm_packus_epi16(luma_sum_hi, _mm_setzero_si128());

    // Store and convert to ASCII characters
    uint8_t luma_array[16];
    _mm_storel_epi64((__m128i *)(luma_array + 0), luminance_lo);
    _mm_storel_epi64((__m128i *)(luma_array + 8), luminance_hi);

    memcpy(out + x, luma_array, 16);
  }
  simd_luminance_scalar((const uint8_t *)(row + x), out + x, width - x);
}
