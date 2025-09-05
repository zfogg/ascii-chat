#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avx2.h"
#include "common.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// AVX2 RGB deinterleaving for 32 pixels using simpler approach
// Input: 32 RGB pixels as packed bytes (RGBRGBRGB...)
// Output: 32 R values, 32 G values, 32 B values stored in provided arrays
static inline void avx2_deinterleave_rgb_32pixels(const rgb_pixel_t *pixels, uint8_t r_out[32], uint8_t g_out[32],
                                                  uint8_t b_out[32]) {
  // Process 32 pixels in two 16-pixel chunks for cleaner implementation
  // Each 16-pixel chunk = 48 bytes of RGB data

  for (int chunk = 0; chunk < 2; chunk++) {
    const rgb_pixel_t *chunk_pixels = &pixels[chunk * 16];
    uint8_t *r_chunk = &r_out[chunk * 16];
    uint8_t *g_chunk = &g_out[chunk * 16];
    uint8_t *b_chunk = &b_out[chunk * 16];

    // Load 48 bytes (16 pixels × 3 bytes) in overlapping 32-byte loads
    __m256i rgb_load1 = _mm256_loadu_si256((__m256i *)&chunk_pixels[0]);  // Bytes 0-31
    __m256i rgb_load2 = _mm256_loadu_si256((__m256i *)&chunk_pixels[10]); // Bytes 30-61 (overlaps)

    // Extract RGB using shuffle masks for 16 pixels
    // R positions: 0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45
    __m256i r_mask =
        _mm256_setr_epi8(0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, -1, -1, -1, -1, -1, // Extract R from first load
                         3, 6, 9, 12, 15, 18, 21, 24, 27, 30, -1, -1, -1, -1, -1, -1 // Extract R from second load
        );

    __m256i g_mask =
        _mm256_setr_epi8(1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, -1, -1, -1, -1, -1, // Extract G from first load
                         4, 7, 10, 13, 16, 19, 22, 25, 28, 31, -1, -1, -1, -1, -1, -1 // Extract G from second load
        );

    __m256i b_mask =
        _mm256_setr_epi8(2, 5, 8, 11, 14, 17, 20, 23, 26, 29, -1, -1, -1, -1, -1, -1, // Extract B from first load
                         5, 8, 11, 14, 17, 20, 23, 26, 29, -1, -1, -1, -1, -1, -1, -1 // Extract B from second load
        );

    // Apply shuffle to extract components
    __m256i r_extracted1 = _mm256_shuffle_epi8(rgb_load1, r_mask);
    __m256i g_extracted1 = _mm256_shuffle_epi8(rgb_load1, g_mask);
    __m256i b_extracted1 = _mm256_shuffle_epi8(rgb_load1, b_mask);

    __m256i r_extracted2 = _mm256_shuffle_epi8(rgb_load2, r_mask);
    __m256i g_extracted2 = _mm256_shuffle_epi8(rgb_load2, g_mask);
    __m256i b_extracted2 = _mm256_shuffle_epi8(rgb_load2, b_mask);

    // Combine and store extracted values
    // For simplicity, store first 11 values from each extraction
    _mm_storeu_si128((__m128i *)r_chunk, _mm256_castsi256_si128(r_extracted1));
    _mm_storeu_si128((__m128i *)g_chunk, _mm256_castsi256_si128(g_extracted1));
    _mm_storeu_si128((__m128i *)b_chunk, _mm256_castsi256_si128(b_extracted1));

    // Store remaining 5 values from second extraction
    _mm_storeu_si128((__m128i *)&r_chunk[11], _mm256_castsi256_si128(r_extracted2));
    _mm_storeu_si128((__m128i *)&g_chunk[11], _mm256_castsi256_si128(g_extracted2));
    _mm_storeu_si128((__m128i *)&b_chunk[11], _mm256_castsi256_si128(b_extracted2));
  }
}

// AVX2 optimized monochrome renderer with true 32-pixel processing per iteration
// Unlike NEON which uses vqtbl4q_u8 table lookups, AVX2 leverages:
// - _mm256_shuffle_epi8 for RGB deinterleaving (32 pixels at once)
// - _mm256_mullo_epi16 for vectorized luminance calculation
// - _mm256_packus_epi16 for efficient 8-bit result packing
// - Full 32-pixel processing (2x NEON's 16-pixel width)
char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars) {
  if (!image || !image->pixels || !ascii_chars) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Get cached UTF-8 character mappings (same as NEON)
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache");
    return NULL;
  }

  // Buffer allocation (same as NEON)
  const size_t max_char_bytes = 4;
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // AVX2 vectorized processing: 32 pixels per iteration (full AVX2 utilization)
    for (; x + 31 < w; x += 32) {
      // Deinterleave 32 RGB pixels using AVX2 shuffle operations
      uint8_t r_vals[32], g_vals[32], b_vals[32];
      avx2_deinterleave_rgb_32pixels(&row[x], r_vals, g_vals, b_vals);

      // Calculate luminance for all 32 pixels using full AVX2 arithmetic
      __m256i luma_red_coeff = _mm256_set1_epi16(LUMA_RED);
      __m256i luma_green_coeff = _mm256_set1_epi16(LUMA_GREEN);
      __m256i luma_blue_coeff = _mm256_set1_epi16(LUMA_BLUE);
      __m256i luma_round = _mm256_set1_epi16(128);

      // Load all 32 R, G, B values at once (full AVX2 utilization)
      __m256i r_32 = _mm256_loadu_si256((__m256i *)r_vals);
      __m256i g_32 = _mm256_loadu_si256((__m256i *)g_vals);
      __m256i b_32 = _mm256_loadu_si256((__m256i *)b_vals);

      // Split into low/high 16 values for 16-bit arithmetic (AVX2 requirement)
      __m256i r_lo_16bit = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(r_32));
      __m256i r_hi_16bit = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(r_32, 1));
      __m256i g_lo_16bit = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(g_32));
      __m256i g_hi_16bit = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(g_32, 1));
      __m256i b_lo_16bit = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(b_32));
      __m256i b_hi_16bit = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(b_32, 1));

      // Vectorized luminance calculation for low 16 pixels: (R*77 + G*150 + B*29 + 128) >> 8
      __m256i luma_r_lo = _mm256_mullo_epi16(r_lo_16bit, luma_red_coeff);
      __m256i luma_g_lo = _mm256_mullo_epi16(g_lo_16bit, luma_green_coeff);
      __m256i luma_b_lo = _mm256_mullo_epi16(b_lo_16bit, luma_blue_coeff);
      __m256i luma_sum_lo = _mm256_add_epi16(_mm256_add_epi16(luma_r_lo, luma_g_lo), luma_b_lo);
      luma_sum_lo = _mm256_add_epi16(luma_sum_lo, luma_round);
      __m256i luminance_lo = _mm256_srli_epi16(luma_sum_lo, 8);

      // Vectorized luminance calculation for high 16 pixels
      __m256i luma_r_hi = _mm256_mullo_epi16(r_hi_16bit, luma_red_coeff);
      __m256i luma_g_hi = _mm256_mullo_epi16(g_hi_16bit, luma_green_coeff);
      __m256i luma_b_hi = _mm256_mullo_epi16(b_hi_16bit, luma_blue_coeff);
      __m256i luma_sum_hi = _mm256_add_epi16(_mm256_add_epi16(luma_r_hi, luma_g_hi), luma_b_hi);
      luma_sum_hi = _mm256_add_epi16(luma_sum_hi, luma_round);
      __m256i luminance_hi = _mm256_srli_epi16(luma_sum_hi, 8);

      // Pack results back to 8-bit and store all 32 luminance values
      __m256i packed_luminance = _mm256_packus_epi16(luminance_lo, luminance_hi);
      uint8_t luminance_vals[32];
      _mm256_storeu_si256((__m256i *)luminance_vals, packed_luminance);

      // Process all 32 characters at once
      for (int i = 0; i < 32; i++) {
        const int luminance = luminance_vals[i];
        const utf8_char_t *char_info = &utf8_cache->cache[luminance];

        // Optimized character emission
        if (char_info->byte_len == 1) {
          *pos++ = char_info->utf8_bytes[0];
        } else {
          switch (char_info->byte_len) {
          case 2:
            *pos++ = char_info->utf8_bytes[0];
            *pos++ = char_info->utf8_bytes[1];
            break;
          case 3:
            *pos++ = char_info->utf8_bytes[0];
            *pos++ = char_info->utf8_bytes[1];
            *pos++ = char_info->utf8_bytes[2];
            break;
          case 4:
            *pos++ = char_info->utf8_bytes[0];
            *pos++ = char_info->utf8_bytes[1];
            *pos++ = char_info->utf8_bytes[2];
            *pos++ = char_info->utf8_bytes[3];
            break;
          }
        }
      }
    }

    // Handle remaining pixels (scalar fallback)
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];

      if (char_info->byte_len == 1) {
        *pos++ = char_info->utf8_bytes[0];
      } else {
        switch (char_info->byte_len) {
        case 2:
          *pos++ = char_info->utf8_bytes[0];
          *pos++ = char_info->utf8_bytes[1];
          break;
        case 3:
          *pos++ = char_info->utf8_bytes[0];
          *pos++ = char_info->utf8_bytes[1];
          *pos++ = char_info->utf8_bytes[2];
          break;
        case 4:
          *pos++ = char_info->utf8_bytes[0];
          *pos++ = char_info->utf8_bytes[1];
          *pos++ = char_info->utf8_bytes[2];
          *pos++ = char_info->utf8_bytes[3];
          break;
        }
      }
    }

    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0';
  return output;
}

// Color function that actually works (not NULL return)
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    SAFE_MALLOC(empty, 1, char *);
    empty[0] = '\0';
    return empty;
  }

  // Use monochrome for simple case
  if (!use_background && !use_256color) {
    return render_ascii_image_monochrome_avx2(image, ascii_chars);
  }

  // For color modes - use output buffer system like NEON color implementation
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    return NULL;
  }

  outbuf_t ob = {0};
  size_t bytes_per_pixel = use_256color ? 6u : 15u; // Estimate for ANSI codes
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  const rgb_pixel_t *pixels_data = (const rgb_pixel_t *)image->pixels;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &pixels_data[y * width];

    for (int x = 0; x < width; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];

      // Add color codes using output buffer system
      if (use_256color) {
        uint8_t color_idx = (uint8_t)(16 + 36 * (pixel.r / 51) + 6 * (pixel.g / 51) + (pixel.b / 51));
        emit_set_256_color_fg(&ob, color_idx);
      } else {
        emit_set_truecolor_fg(&ob, pixel.r, pixel.g, pixel.b);
      }

      // Add character
      ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
    }

    if (y < height - 1) {
      ob_write(&ob, "\n", 1);
    }
  }

  ob_term(&ob);
  return ob.buf;
}

// Destroy AVX2 cache resources (called at program shutdown)
void avx2_caches_destroy(void) {
  // AVX2 currently uses shared caches from common.c, so no specific cleanup needed
  log_debug("AVX2_CACHE: AVX2 caches cleaned up");
}

#endif /* SIMD_SUPPORT_AVX2 */