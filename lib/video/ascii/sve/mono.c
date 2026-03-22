/**
 * @file video/ascii/sve.c
 * @ingroup video
 * @brief 🚀 ARM SVE (Scalable Vector Extension) ASCII rendering with variable-length vectors
 */

#if SIMD_SUPPORT_SVE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ascii-chat/video/ascii/sve.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/common.h> // For LUMA_RED, LUMA_GREEN, LUMA_BLUE, LUMA_THRESHOLD
#include <ascii-chat/video/ascii/output_buffer.h>    // For outbuf_t, emit_*, ob_*

#include <arm_sve.h>

#include <ascii-chat/util/overflow.h>

//=============================================================================
// Image-based API (matches NEON architecture)
//=============================================================================

// Simple monochrome ASCII function (matches scalar image_print performance)
char *render_ascii_mono_sve(const image_t *image, const char *ascii_chars) {
  if (!image || !image->pixels) {
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
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  char *output = SAFE_MALLOC(len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure SVE processing - matches NEON approach but with scalable vectors
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process pixels with SVE (scalable vector length - typically 128, 256, or 512 bits)
    svbool_t pg = svptrue_b8(); // Predicate for all lanes
    (void)pg;                   // May be unused in some code paths

    while (x < w) {
      // Calculate how many pixels we can process in this iteration
      int remaining = w - x;
      (void)remaining;
      svbool_t pg_active = svwhilelt_b8_s32(x, w);
      int vec_len = svcntb_pat(SV_ALL) / 3; // Vector length in RGB pixels (3 bytes per pixel)
      int process_count = (remaining < vec_len) ? remaining : vec_len;

      // Manual deinterleave RGB components (SVE limitation vs NEON's vld3)
      uint8_t r_array[64], g_array[64], b_array[64]; // Max SVE vector size
      for (int j = 0; j < process_count; j++) {
        if (x + j < w) {
          r_array[j] = row[x + j].r;
          g_array[j] = row[x + j].g;
          b_array[j] = row[x + j].b;
        }
      }

      // Load into SVE vectors
      svuint8_t r_vec = svld1_u8(pg_active, r_array);
      svuint8_t g_vec = svld1_u8(pg_active, g_array);
      svuint8_t b_vec = svld1_u8(pg_active, b_array);

      // Convert to 16-bit for arithmetic
      svuint16_t r_16 = svunpklo_u16(r_vec);
      svuint16_t g_16 = svunpklo_u16(g_vec);
      svuint16_t b_16 = svunpklo_u16(b_vec);

      // Calculate luminance: (77*R + 150*G + 29*B + 128) >> 8
      svuint16_t luma = svmul_n_u16_x(svptrue_b16(), r_16, LUMA_RED);
      luma = svmla_n_u16_x(svptrue_b16(), luma, g_16, LUMA_GREEN);
      luma = svmla_n_u16_x(svptrue_b16(), luma, b_16, LUMA_BLUE);
      luma = svadd_n_u16_x(svptrue_b16(), luma, LUMA_THRESHOLD);
      luma = svlsr_n_u16_x(svptrue_b16(), luma, 8);

      // Store u16 luminance values (SVE1 compatible - no SVE2 narrowing intrinsics)
      // After right-shift by 8, values are already in 0-255 range
      uint16_t luma_temp[64];
      svst1_u16(svptrue_b16(), luma_temp, luma);

      // Convert to u8 array for ASCII lookup
      uint8_t luma_array[64];
      for (int j = 0; j < process_count; j++) {
        luma_array[j] = (uint8_t)luma_temp[j];
      }

      for (int j = 0; j < process_count; j++) {
        if (x + j < w) {
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
      x += process_count;
    }

    // Add newline (except for last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

// 256-color palette mapping (RGB to ANSI 256 color index) - copied from NEON
static inline uint8_t rgb_to_256color_sve(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// Unified SVE function for all color modes (full implementation like NEON)

#endif /* SIMD_SUPPORT_SVE */
