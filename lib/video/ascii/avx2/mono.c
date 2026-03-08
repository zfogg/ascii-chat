/**
 * @file video/ascii/avx2.c
 * @ingroup video
 * @brief 🚀 AVX2-accelerated ASCII rendering with 256-bit vector operations for x86_64
 */

#if SIMD_SUPPORT_AVX2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/video/ascii/avx2.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/avx2/common.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>

#include <immintrin.h>

// Single-pass AVX2 monochrome renderer with immediate emission

char *render_ascii_mono_avx2(const image_t *image, const char *ascii_chars) {
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

  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Use malloc for output buffer (will be freed by caller)
  // Each pixel can produce: 4 bytes UTF-8 + 8 bytes RLE escape (\x1b[9999b) = 12 bytes max
  // Plus 1 newline per row
  size_t output_size = (size_t)h * ((size_t)w * 12 + 1);

  char *output = SAFE_MALLOC(output_size, char *);
  if (!output) {
    log_error("Failed to allocate output buffer for AVX2 rendering");
    return NULL;
  }

  char *pos = output;

  // Process row by row for better cache locality
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row_pixels = &pixels[y * w];
    int x = 0;

    // AVX2 fast path: process 32 pixels at a time
    while (x + 31 < w) {
      // Process 32 pixels with AVX2 using thread-local buffers
      avx2_load_rgb32_optimized(&row_pixels[x], avx2_r_buffer, avx2_g_buffer, avx2_b_buffer);
      avx2_compute_luminance_32(avx2_r_buffer, avx2_g_buffer, avx2_b_buffer, avx2_luminance_buffer);

      // Convert to character indices and emit immediately
      int i = 0;
      while (i < 32) {
        const uint8_t luma_idx = avx2_luminance_buffer[i] >> 2; // 0-63
        const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

        // Find run length within this chunk
        int run_end = i + 1;
        while (run_end < 32 && x + run_end < w) {
          const uint8_t next_luma_idx = avx2_luminance_buffer[run_end] >> 2;
          if (next_luma_idx != luma_idx)
            break;
          run_end++;
        }
        int run = run_end - i;

        // Emit UTF-8 character with RLE
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;

        if (rep_is_profitable(run)) {
          pos = emit_rle_count(pos, run - 1);
        } else {
          // Emit remaining characters
          for (int k = 1; k < run; k++) {
            memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
            pos += char_info->byte_len;
          }
        }
        i = run_end;
      }
      x += 32;
    }

    // Scalar processing for remaining pixels (< 32)
    while (x < w) {
      const rgb_pixel_t *p = &row_pixels[x];
      const int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b + 128) >> 8;
      const uint8_t luma_idx = luminance >> 2; // 0-255 -> 0-63
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      // Find run length for RLE
      int j = x + 1;
      while (j < w) {
        const rgb_pixel_t *next_p = &row_pixels[j];
        const int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b + 128) >> 8;
        const uint8_t next_luma_idx = next_luminance >> 2;
        if (next_luma_idx != luma_idx)
          break;
        j++;
      }
      int run = j - x;

      // Emit UTF-8 character with RLE
      memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
      pos += char_info->byte_len;

      if (rep_is_profitable(run)) {
        pos = emit_rle_count(pos, run - 1);
      } else {
        for (int k = 1; k < run; k++) {
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;
        }
      }
      x = j;
    }

    // Add reset sequence and newline after each row (except last)
    *pos++ = '\x1b';
    *pos++ = '[';
    *pos++ = '0';
    *pos++ = 'm';
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0'; // Null terminate

  return output;
}

// Single-pass AVX2 color renderer with immediate emission

#endif /* SIMD_SUPPORT_AVX2 */
