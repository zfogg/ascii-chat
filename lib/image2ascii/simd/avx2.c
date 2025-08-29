#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avx2.h"
#include "common.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

//=============================================================================
// Image-based API (matches NEON architecture)
//=============================================================================

// Simple monochrome ASCII function (matches scalar image_print performance)
char *render_ascii_image_monochrome_avx2(const image_t *image, const char luminance_palette[256]) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Match scalar allocation exactly: h rows * (w chars + 1 newline) + null terminator
  const size_t len = (size_t)h * ((size_t)w + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure AVX2 processing - matches NEON approach
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process 32 pixels at a time with AVX2 (AVX2 has 256-bit registers - double SSE2's capacity)
    for (; x + 31 < w; x += 32) {
      // Manual deinterleave RGB components (AVX2 limitation vs NEON's vld3q_u8)
      uint8_t r_array[32], g_array[32], b_array[32];
      for (int j = 0; j < 32; j++) {
        r_array[j] = row[x + j].r;
        g_array[j] = row[x + j].g;
        b_array[j] = row[x + j].b;
      }

      // Load into AVX2 registers (256-bit)
      __m256i r_vec = _mm256_loadu_si256((__m256i *)r_array);
      __m256i g_vec = _mm256_loadu_si256((__m256i *)g_array);
      __m256i b_vec = _mm256_loadu_si256((__m256i *)b_array);

      // Convert to 16-bit for arithmetic (process both low and high halves for full 32 pixels)
      __m256i r_16_lo = _mm256_unpacklo_epi8(r_vec, _mm256_setzero_si256()); // First 16 pixels
      __m256i r_16_hi = _mm256_unpackhi_epi8(r_vec, _mm256_setzero_si256()); // Second 16 pixels
      __m256i g_16_lo = _mm256_unpacklo_epi8(g_vec, _mm256_setzero_si256());
      __m256i g_16_hi = _mm256_unpackhi_epi8(g_vec, _mm256_setzero_si256());
      __m256i b_16_lo = _mm256_unpacklo_epi8(b_vec, _mm256_setzero_si256());
      __m256i b_16_hi = _mm256_unpackhi_epi8(b_vec, _mm256_setzero_si256());

      // Calculate luminance for low half: (77*R + 150*G + 29*B + 128) >> 8
      __m256i luma_r_lo = _mm256_mullo_epi16(r_16_lo, _mm256_set1_epi16(LUMA_RED));
      __m256i luma_g_lo = _mm256_mullo_epi16(g_16_lo, _mm256_set1_epi16(LUMA_GREEN));
      __m256i luma_b_lo = _mm256_mullo_epi16(b_16_lo, _mm256_set1_epi16(LUMA_BLUE));

      __m256i luma_sum_lo = _mm256_add_epi16(luma_r_lo, luma_g_lo);
      luma_sum_lo = _mm256_add_epi16(luma_sum_lo, luma_b_lo);
      luma_sum_lo = _mm256_add_epi16(luma_sum_lo, _mm256_set1_epi16(LUMA_THRESHOLD));
      luma_sum_lo = _mm256_srli_epi16(luma_sum_lo, 8);

      // Calculate luminance for high half
      __m256i luma_r_hi = _mm256_mullo_epi16(r_16_hi, _mm256_set1_epi16(LUMA_RED));
      __m256i luma_g_hi = _mm256_mullo_epi16(g_16_hi, _mm256_set1_epi16(LUMA_GREEN));
      __m256i luma_b_hi = _mm256_mullo_epi16(b_16_hi, _mm256_set1_epi16(LUMA_BLUE));

      __m256i luma_sum_hi = _mm256_add_epi16(luma_r_hi, luma_g_hi);
      luma_sum_hi = _mm256_add_epi16(luma_sum_hi, luma_b_hi);
      luma_sum_hi = _mm256_add_epi16(luma_sum_hi, _mm256_set1_epi16(LUMA_THRESHOLD));
      luma_sum_hi = _mm256_srli_epi16(luma_sum_hi, 8);

      // Pack back to 8-bit (combines both halves into 32 8-bit values)
      __m256i luminance = _mm256_packus_epi16(luma_sum_lo, luma_sum_hi);

      // Store and convert to ASCII characters
      uint8_t luma_array[32];
      _mm256_storeu_si256((__m256i *)luma_array, luminance);

      for (int j = 0; j < 32; j++) {
        pos[j] = luminance_palette[luma_array[j]];
      }
      pos += 32;
    }

    // Handle remaining pixels with scalar code
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + LUMA_THRESHOLD) >> 8;
      *pos++ = luminance_palette[luminance];
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
static inline uint8_t rgb_to_256color_avx2(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// Unified AVX2 function for all color modes (full implementation like NEON)
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

  // Use monochrome optimization for simple case
  if (!use_background && !use_256color) {
    return render_ascii_image_monochrome_avx2(image);
  }

  outbuf_t ob = {0};
  // Estimate buffer size based on mode (copied from NEON)
  size_t bytes_per_pixel = use_256color ? 6u : 8u; // 256-color shorter than truecolor
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Build 64-entry glyph LUT (copied from NEON approach)
  uint8_t ramp64[RAMP64_SIZE];
  build_ramp64(ramp64);

  // Track current color state (copied from NEON)
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 32-pixel chunks with AVX2 (full 256-bit register width)
    while (x + 32 <= width) {
      // Manual deinterleave RGB components (AVX2 limitation vs NEON's vld3q_u8)
      uint8_t r_array[32], g_array[32], b_array[32];
      for (int j = 0; j < 32; j++) {
        r_array[j] = row[x + j].r;
        g_array[j] = row[x + j].g;
        b_array[j] = row[x + j].b;
      }

      // Load into AVX2 registers (256-bit)
      __m256i r_vec = _mm256_loadu_si256((__m256i *)r_array);
      __m256i g_vec = _mm256_loadu_si256((__m256i *)g_array);
      __m256i b_vec = _mm256_loadu_si256((__m256i *)b_array);

      // Convert to 16-bit for arithmetic (process both low and high halves for full 32 pixels)
      __m256i r_16_lo = _mm256_unpacklo_epi8(r_vec, _mm256_setzero_si256());
      __m256i r_16_hi = _mm256_unpackhi_epi8(r_vec, _mm256_setzero_si256());
      __m256i g_16_lo = _mm256_unpacklo_epi8(g_vec, _mm256_setzero_si256());
      __m256i g_16_hi = _mm256_unpackhi_epi8(g_vec, _mm256_setzero_si256());
      __m256i b_16_lo = _mm256_unpacklo_epi8(b_vec, _mm256_setzero_si256());
      __m256i b_16_hi = _mm256_unpackhi_epi8(b_vec, _mm256_setzero_si256());

      // Calculate luminance for low half: (77*R + 150*G + 29*B + 128) >> 8
      __m256i luma_r_lo = _mm256_mullo_epi16(r_16_lo, _mm256_set1_epi16(LUMA_RED));
      __m256i luma_g_lo = _mm256_mullo_epi16(g_16_lo, _mm256_set1_epi16(LUMA_GREEN));
      __m256i luma_b_lo = _mm256_mullo_epi16(b_16_lo, _mm256_set1_epi16(LUMA_BLUE));

      __m256i luma_sum_lo = _mm256_add_epi16(luma_r_lo, luma_g_lo);
      luma_sum_lo = _mm256_add_epi16(luma_sum_lo, luma_b_lo);
      luma_sum_lo = _mm256_add_epi16(luma_sum_lo, _mm256_set1_epi16(LUMA_THRESHOLD));
      luma_sum_lo = _mm256_srli_epi16(luma_sum_lo, 8);

      // Calculate luminance for high half
      __m256i luma_r_hi = _mm256_mullo_epi16(r_16_hi, _mm256_set1_epi16(LUMA_RED));
      __m256i luma_g_hi = _mm256_mullo_epi16(g_16_hi, _mm256_set1_epi16(LUMA_GREEN));
      __m256i luma_b_hi = _mm256_mullo_epi16(b_16_hi, _mm256_set1_epi16(LUMA_BLUE));

      __m256i luma_sum_hi = _mm256_add_epi16(luma_r_hi, luma_g_hi);
      luma_sum_hi = _mm256_add_epi16(luma_sum_hi, luma_b_hi);
      luma_sum_hi = _mm256_add_epi16(luma_sum_hi, _mm256_set1_epi16(LUMA_THRESHOLD));
      luma_sum_hi = _mm256_srli_epi16(luma_sum_hi, 8);

      // Pack back to 8-bit and store (combines both halves into 32 8-bit values)
      __m256i luminance = _mm256_packus_epi16(luma_sum_lo, luma_sum_hi);
      uint8_t luma_array[32];
      _mm256_storeu_si256((__m256i *)luma_array, luminance);

      // Convert to ASCII glyphs using ramp64 (like NEON)
      uint8_t gbuf[32];
      for (int i = 0; i < 32; i++) {
        gbuf[i] = ramp64[luma_array[i] >> 2]; // Use same index calculation as NEON
      }

      if (use_256color) {
        // 256-color mode processing (copied from NEON logic)
        uint8_t color_indices[16];
        for (int i = 0; i < 16; i++) {
          color_indices[i] = rgb_to_256color_avx2(r_array[i], g_array[i], b_array[i]);
        }

        // Emit with RLE on (glyph, color) runs (copied from NEON)
        for (int i = 0; i < 16;) {
          const uint8_t ch = gbuf[i];
          const uint8_t color_idx = color_indices[i];

          int j = i + 1;
          while (j < 32 && gbuf[j] == ch && color_indices[j] == color_idx) {
            j++;
          }
          const uint32_t run = (uint32_t)(j - i);

          if (color_idx != cur_color_idx) {
            if (use_background) {
              emit_set_256_color_bg(&ob, color_idx);
            } else {
              emit_set_256_color_fg(&ob, color_idx);
            }
            cur_color_idx = color_idx;
          }

          ob_putc(&ob, (char)ch);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_putc(&ob, (char)ch);
            }
          }
          i = j;
        }
      } else {
        // Truecolor mode processing (copied from NEON logic)
        for (int i = 0; i < 32;) {
          const uint8_t ch = gbuf[i];
          const uint8_t r = r_array[i];
          const uint8_t g = g_array[i];
          const uint8_t b = b_array[i];

          int j = i + 1;
          while (j < 32 && gbuf[j] == ch && r_array[j] == r && g_array[j] == g && b_array[j] == b) {
            j++;
          }
          const uint32_t run = (uint32_t)(j - i);

          if (r != curR || g != curG || b != curB) {
            if (use_background) {
              emit_set_truecolor_bg(&ob, r, g, b);
            } else {
              emit_set_truecolor_fg(&ob, r, g, b);
            }
            curR = r;
            curG = g;
            curB = b;
          }

          ob_putc(&ob, (char)ch);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_putc(&ob, (char)ch);
            }
          }
          i = j;
        }
      }
      x += 32;
    }

    // Scalar tail for remaining pixels (copied from NEON logic)
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + LUMA_THRESHOLD) >> 8);
      uint8_t ch = ramp64[Y >> 2];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color_avx2((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          uint8_t color_idx2 = rgb_to_256color_avx2((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
          if (((Y2 >> 2) != (Y >> 2)) || color_idx2 != color_idx)
            break;
          j++;
        }
        uint32_t run = (uint32_t)(j - x);

        if (color_idx != cur_color_idx) {
          if (use_background) {
            emit_set_256_color_bg(&ob, color_idx);
          } else {
            emit_set_256_color_fg(&ob, color_idx);
          }
          cur_color_idx = color_idx;
        }

        ob_putc(&ob, (char)ch);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_putc(&ob, (char)ch);
          }
        }
        x = j;
      } else {
        // Truecolor scalar tail
        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          if (((Y2 >> 2) != (Y >> 2)) || R2 != R || G2 != G || B2 != B)
            break;
          j++;
        }
        uint32_t run = (uint32_t)(j - x);

        if ((int)R != curR || (int)G != curG || (int)B != curB) {
          if (use_background) {
            emit_set_truecolor_bg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
          } else {
            emit_set_truecolor_fg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
          }
          curR = (int)R;
          curG = (int)G;
          curB = (int)B;
        }

        ob_putc(&ob, (char)ch);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_putc(&ob, (char)ch);
          }
        }
        x = j;
      }
    }

    // End row: reset SGR, add newline (except for last row) (copied from NEON)
    emit_reset(&ob);
    if (y < height - 1) {
      ob_putc(&ob, '\n');
    }
    curR = curG = curB = -1;
    cur_color_idx = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

#endif /* SIMD_SUPPORT_AVX2 */
