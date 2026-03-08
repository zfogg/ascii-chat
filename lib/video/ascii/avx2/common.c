/**
 * @file video/ascii/avx2/common.c
 * @brief Shared AVX2 helper functions used by both mono and color renderers
 */

#include <stdint.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/common.h>

#if SIMD_SUPPORT_AVX2
#include <immintrin.h>

// Thread-local storage for AVX2 working buffers
// These stay in L1 cache and are reused across function calls
// Non-static for shared library compatibility (still thread-local)
THREAD_LOCAL ALIGNED_32 uint8_t avx2_r_buffer[32];
THREAD_LOCAL ALIGNED_32 uint8_t avx2_g_buffer[32];
THREAD_LOCAL ALIGNED_32 uint8_t avx2_b_buffer[32];
THREAD_LOCAL ALIGNED_32 uint8_t avx2_luminance_buffer[32];

// Helper function to emit RLE repeat count (handles any count up to 9999)
char *emit_rle_count(char *pos, uint32_t rep_count) {
  *pos++ = '\x1b';
  *pos++ = '[';

  // Handle up to 4 digits (max 9999)
  if (rep_count >= 1000) {
    *pos++ = '0' + (rep_count / 1000);
    *pos++ = '0' + ((rep_count / 100) % 10);
    *pos++ = '0' + ((rep_count / 10) % 10);
    *pos++ = '0' + (rep_count % 10);
  } else if (rep_count >= 100) {
    *pos++ = '0' + (rep_count / 100);
    *pos++ = '0' + ((rep_count / 10) % 10);
    *pos++ = '0' + (rep_count % 10);
  } else if (rep_count >= 10) {
    *pos++ = '0' + (rep_count / 10);
    *pos++ = '0' + (rep_count % 10);
  } else {
    *pos++ = '0' + rep_count;
  }
  *pos++ = 'b';

  return pos;
}

// Optimized AVX2 function to load 32 RGB pixels and separate channels
// Uses simple loop that auto-vectorizes to VMOVDQU + VPSHUFB
void avx2_load_rgb32_optimized(const rgb_pixel_t *__restrict pixels, uint8_t *__restrict r_out,
                               uint8_t *__restrict g_out, uint8_t *__restrict b_out) {
  // Simple loop that compiler auto-vectorizes into efficient SIMD
  for (int i = 0; i < 32; i++) {
    r_out[i] = pixels[i].r;
    g_out[i] = pixels[i].g;
    b_out[i] = pixels[i].b;
  }
}

// AVX2 function to compute luminance for 32 pixels
void avx2_compute_luminance_32(const uint8_t *r_vals, const uint8_t *g_vals, const uint8_t *b_vals,
                               uint8_t *luminance_out) {
  // Load all 32 RGB values into AVX2 registers
  __m256i r_all = _mm256_loadu_si256((const __m256i_u *)r_vals);
  __m256i g_all = _mm256_loadu_si256((const __m256i_u *)g_vals);
  __m256i b_all = _mm256_loadu_si256((const __m256i_u *)b_vals);

  // Process low 16 pixels with accurate coefficients (16-bit math to prevent overflow)
  __m256i r_lo = _mm256_unpacklo_epi8(r_all, _mm256_setzero_si256());
  __m256i g_lo = _mm256_unpacklo_epi8(g_all, _mm256_setzero_si256());
  __m256i b_lo = _mm256_unpacklo_epi8(b_all, _mm256_setzero_si256());

  __m256i luma_16_lo = _mm256_mullo_epi16(r_lo, _mm256_set1_epi16(77));
  luma_16_lo = _mm256_add_epi16(luma_16_lo, _mm256_mullo_epi16(g_lo, _mm256_set1_epi16(150)));
  luma_16_lo = _mm256_add_epi16(luma_16_lo, _mm256_mullo_epi16(b_lo, _mm256_set1_epi16(29)));
  luma_16_lo = _mm256_add_epi16(luma_16_lo, _mm256_set1_epi16(128));
  luma_16_lo = _mm256_srli_epi16(luma_16_lo, 8);

  // Process high 16 pixels with accurate coefficients
  __m256i r_hi = _mm256_unpackhi_epi8(r_all, _mm256_setzero_si256());
  __m256i g_hi = _mm256_unpackhi_epi8(g_all, _mm256_setzero_si256());
  __m256i b_hi = _mm256_unpackhi_epi8(b_all, _mm256_setzero_si256());

  __m256i luma_16_hi = _mm256_mullo_epi16(r_hi, _mm256_set1_epi16(77));
  luma_16_hi = _mm256_add_epi16(luma_16_hi, _mm256_mullo_epi16(g_hi, _mm256_set1_epi16(150)));
  luma_16_hi = _mm256_add_epi16(luma_16_hi, _mm256_mullo_epi16(b_hi, _mm256_set1_epi16(29)));
  luma_16_hi = _mm256_add_epi16(luma_16_hi, _mm256_set1_epi16(128));
  luma_16_hi = _mm256_srli_epi16(luma_16_hi, 8);

  // Pack back to 8-bit
  __m256i luma_packed = _mm256_packus_epi16(luma_16_lo, luma_16_hi);

  // After unpack and pack operations, bytes are already in correct order [0-31]
  // luma_16_lo contains pixels 0-7 (lower 128) and 16-23 (upper 128)
  // luma_16_hi contains pixels 8-15 (lower 128) and 24-31 (upper 128)
  // packus produces: [0-7, 8-15] in lower 128, [16-23, 24-31] in upper 128
  // No permute needed - this is already the correct sequential order
  _mm256_storeu_si256((__m256i_u *)luminance_out, luma_packed);
}

#endif // SIMD_SUPPORT_AVX2
