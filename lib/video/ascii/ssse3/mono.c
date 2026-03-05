/**
 * @file video/ascii/ssse3.c
 * @ingroup video
 * @brief 🚀 SSSE3-accelerated ASCII rendering with advanced shuffle operations for x86
 */

#if SIMD_SUPPORT_SSSE3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <tmmintrin.h>

#include <ascii-chat/video/ascii/ssse3/foreground.h>
#include <ascii-chat/video/ascii/ascii_simd.h>
#include <ascii-chat/common.h>
#include <ascii-chat/output_buffer.h>
#include <ascii-chat/util/overflow.h>

//=============================================================================
// Image-based API (matches NEON architecture)
//=============================================================================

// Simple monochrome ASCII function (matches scalar image_print performance)
char *render_ascii_mono_ssse3(const image_t *image, const char *ascii_chars) {
  if (!image || !image->pixels || !ascii_chars) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Get cached UTF-8 character mappings
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache");
    return NULL;
  }

  // Buffer size for UTF-8 characters
  const size_t max_char_bytes = 4;

  // Calculate buffer size with overflow checking
  size_t w_times_bytes;
  if (checked_size_mul((size_t)w, max_char_bytes, &w_times_bytes) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: width too large for UTF-8 encoding");
    return NULL;
  }

  size_t w_times_bytes_plus_one;
  if (checked_size_add(w_times_bytes, 1, &w_times_bytes_plus_one) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: width * bytes + 1 overflow");
    return NULL;
  }

  size_t len;
  if (checked_size_mul((size_t)h, w_times_bytes_plus_one, &len) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: height * (width * bytes + 1) overflow");
    return NULL;
  }

  char *output;
  output = SAFE_MALLOC(len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure SSSE3 processing - matches NEON approach
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process 16 pixels at a time with SSSE3 (full 128-bit register capacity)
    for (; x + 15 < w; x += 16) {
      // Manual deinterleave RGB components (SSSE3 limitation vs NEON's vld3q_u8)
      uint8_t r_array[16], g_array[16], b_array[16];
      for (int j = 0; j < 16; j++) {
        r_array[j] = row[x + j].r;
        g_array[j] = row[x + j].g;
        b_array[j] = row[x + j].b;
      }

      // Process 16 pixels in two 8-pixel SSSE3 batches (same as SSE2 approach)
      __m128i r_vec_lo = _mm_loadl_epi64((__m128i *)(r_array + 0));
      __m128i r_vec_hi = _mm_loadl_epi64((__m128i *)(r_array + 8));
      __m128i g_vec_lo = _mm_loadl_epi64((__m128i *)(g_array + 0));
      __m128i g_vec_hi = _mm_loadl_epi64((__m128i *)(g_array + 8));
      __m128i b_vec_lo = _mm_loadl_epi64((__m128i *)(b_array + 0));
      __m128i b_vec_hi = _mm_loadl_epi64((__m128i *)(b_array + 8));

      // Process first 8 pixels
      __m128i r_16_lo = _mm_unpacklo_epi8(r_vec_lo, _mm_setzero_si128());
      __m128i g_16_lo = _mm_unpacklo_epi8(g_vec_lo, _mm_setzero_si128());
      __m128i b_16_lo = _mm_unpacklo_epi8(b_vec_lo, _mm_setzero_si128());

      __m128i luma_r_lo = _mm_mullo_epi16(r_16_lo, _mm_set1_epi16(LUMA_RED));
      __m128i luma_g_lo = _mm_mullo_epi16(g_16_lo, _mm_set1_epi16(LUMA_GREEN));
      __m128i luma_b_lo = _mm_mullo_epi16(b_16_lo, _mm_set1_epi16(LUMA_BLUE));

      __m128i luma_sum_lo = _mm_add_epi16(luma_r_lo, luma_g_lo);
      luma_sum_lo = _mm_add_epi16(luma_sum_lo, luma_b_lo);
      luma_sum_lo = _mm_add_epi16(luma_sum_lo, _mm_set1_epi16(LUMA_THRESHOLD));
      luma_sum_lo = _mm_srli_epi16(luma_sum_lo, 8);

      // Process second 8 pixels
      __m128i r_16_hi = _mm_unpacklo_epi8(r_vec_hi, _mm_setzero_si128());
      __m128i g_16_hi = _mm_unpacklo_epi8(g_vec_hi, _mm_setzero_si128());
      __m128i b_16_hi = _mm_unpacklo_epi8(b_vec_hi, _mm_setzero_si128());

      __m128i luma_r_hi = _mm_mullo_epi16(r_16_hi, _mm_set1_epi16(LUMA_RED));
      __m128i luma_g_hi = _mm_mullo_epi16(g_16_hi, _mm_set1_epi16(LUMA_GREEN));
      __m128i luma_b_hi = _mm_mullo_epi16(b_16_hi, _mm_set1_epi16(LUMA_BLUE));

      __m128i luma_sum_hi = _mm_add_epi16(luma_r_hi, luma_g_hi);
      luma_sum_hi = _mm_add_epi16(luma_sum_hi, luma_b_hi);
      luma_sum_hi = _mm_add_epi16(luma_sum_hi, _mm_set1_epi16(LUMA_THRESHOLD));
      luma_sum_hi = _mm_srli_epi16(luma_sum_hi, 8);

      // Pack and store
      __m128i luminance_lo = _mm_packus_epi16(luma_sum_lo, _mm_setzero_si128());
      __m128i luminance_hi = _mm_packus_epi16(luma_sum_hi, _mm_setzero_si128());

      uint8_t luma_array[16];
      _mm_storel_epi64((__m128i *)(luma_array + 0), luminance_lo);
      _mm_storel_epi64((__m128i *)(luma_array + 8), luminance_hi);

      for (int j = 0; j < 16; j++) {
        const utf8_char_t *char_info = &utf8_cache->cache[luma_array[j]];
        // Optimized: Use direct assignment for single-byte ASCII characters
        if (char_info->byte_len == 1) {
          *pos++ = char_info->utf8_bytes[0];
        } else {
          // Fallback to full memcpy for multi-byte UTF-8
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;
        }
      }
    }

    // Handle remaining pixels with optimized scalar code
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + LUMA_THRESHOLD) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];
      // Optimized: Use direct assignment for single-byte ASCII characters
      if (char_info->byte_len == 1) {
        *pos++ = char_info->utf8_bytes[0];
      } else {
        // Fallback to full memcpy for multi-byte UTF-8
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;
      }
    }

    // Add newline (except last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

// 256-color palette mapping (RGB to ANSI 256 color index) - copied from NEON
static inline uint8_t rgb_to_256color_ssse3(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// Unified SSSE3 function for all color modes (full implementation like NEON)

#endif /* SIMD_SUPPORT_SSSE3 */
