#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ssse3.h"
#include "common.h"

#ifdef SIMD_SUPPORT_SSSE3
#include <tmmintrin.h>

//=============================================================================
// Image-based API (matches NEON architecture)
//=============================================================================

// Simple monochrome ASCII function (matches scalar image_print performance)
char *render_ascii_image_monochrome_ssse3(const image_t *image, const char *ascii_chars) {
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
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

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
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;
      }
      pos += 16;
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
static inline uint8_t rgb_to_256color_ssse3(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// Unified SSSE3 function for all color modes (full implementation like NEON)
char *render_ascii_ssse3_unified_optimized(const image_t *image, bool use_background, bool use_256color,
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
    return render_ascii_image_monochrome_ssse3(image);
  }

  // Get cached UTF-8 character mappings for color rendering
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for SSSE3 color");
    return NULL;
  }

  outbuf_t ob = {0};
  // Estimate buffer size based on mode (copied from NEON)
  size_t bytes_per_pixel = use_256color ? 6u : 8u; // 256-color shorter than truecolor
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Build SSSE3 lookup table for _mm_shuffle_epi8 (uses character indices)
  __m128i char_lut = _mm_loadu_si128((__m128i *)utf8_cache->char_index_ramp);  // Load first 16 indices

  // Track current color state (copied from NEON)
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 16-pixel chunks with SSSE3 (full 128-bit register capacity)
    while (x + 16 <= width) {
      // Manual deinterleave RGB components (SSSE3 limitation vs NEON's vld3q_u8)
      uint8_t r_array[16], g_array[16], b_array[16];
      for (int j = 0; j < 16; j++) {
        r_array[j] = row[x + j].r;
        g_array[j] = row[x + j].g;
        b_array[j] = row[x + j].b;
      }

      // Load into SSSE3 registers
      __m128i r_vec = _mm_loadl_epi64((__m128i *)r_array);
      __m128i g_vec = _mm_loadl_epi64((__m128i *)g_array);
      __m128i b_vec = _mm_loadl_epi64((__m128i *)b_array);

      // Convert to 16-bit for arithmetic
      __m128i r_16 = _mm_unpacklo_epi8(r_vec, _mm_setzero_si128());
      __m128i g_16 = _mm_unpacklo_epi8(g_vec, _mm_setzero_si128());
      __m128i b_16 = _mm_unpacklo_epi8(b_vec, _mm_setzero_si128());

      // Calculate luminance: (77*R + 150*G + 29*B + 128) >> 8
      __m128i luma_r = _mm_mullo_epi16(r_16, _mm_set1_epi16(LUMA_RED));
      __m128i luma_g = _mm_mullo_epi16(g_16, _mm_set1_epi16(LUMA_GREEN));
      __m128i luma_b = _mm_mullo_epi16(b_16, _mm_set1_epi16(LUMA_BLUE));

      __m128i luma_sum = _mm_add_epi16(luma_r, luma_g);
      luma_sum = _mm_add_epi16(luma_sum, luma_b);
      luma_sum = _mm_add_epi16(luma_sum, _mm_set1_epi16(LUMA_THRESHOLD));
      luma_sum = _mm_srli_epi16(luma_sum, 8);

      // Pack back to 8-bit and store
      __m128i luminance = _mm_packus_epi16(luma_sum, _mm_setzero_si128());
      uint8_t luma_array[8];
      _mm_storel_epi64((__m128i *)luma_array, luminance);

      // FAST: Use _mm_shuffle_epi8 to get character indices from the ramp (SSSE3 advantage)
      __m128i luma_vec = _mm_loadl_epi64((__m128i *)luma_array);  // Load 8 luminance values
      __m128i luma_idx_vec = _mm_srli_epi16(_mm_unpacklo_epi8(luma_vec, _mm_setzero_si128()), 2); // >> 2 for 0-63
      __m128i luma_idx_8bit = _mm_packus_epi16(luma_idx_vec, _mm_setzero_si128()); // Pack back to 8-bit
      
      // Use _mm_shuffle_epi8 for fast character index lookup
      __m128i char_indices_vec = _mm_shuffle_epi8(char_lut, luma_idx_8bit);
      
      uint8_t char_indices[8];
      _mm_storel_epi64((__m128i *)char_indices, char_indices_vec);

      if (use_256color) {
        // 256-color mode processing (copied from NEON logic)
        uint8_t color_indices[8];
        for (int i = 0; i < 8; i++) {
          color_indices[i] = rgb_to_256color_ssse3(r_array[i], g_array[i], b_array[i]);
        }

        // Emit with RLE on (glyph, color) runs (copied from NEON)
        for (int i = 0; i < 8;) {
          const uint8_t char_idx = char_indices[i];
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t color_idx = color_indices[i];

          int j = i + 1;
          while (j < 8 && char_indices[j] == char_idx && color_indices[j] == color_idx) {
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

          // Emit UTF-8 character from cache
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              // Emit UTF-8 character from cache
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          i = j;
        }
      } else {
        // Truecolor mode processing (copied from NEON logic)
        for (int i = 0; i < 8;) {
          const uint8_t char_idx = char_indices[i];
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t r = r_array[i];
          const uint8_t g = g_array[i];
          const uint8_t b = b_array[i];

          int j = i + 1;
          while (j < 8 && char_indices[j] == char_idx && r_array[j] == r && g_array[j] == g && b_array[j] == b) {
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

          // Emit UTF-8 character from cache
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              // Emit UTF-8 character from cache
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          i = j;
        }
      }
      x += 16;
    }

    // Scalar tail for remaining pixels (copied from NEON logic)
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + LUMA_THRESHOLD) >> 8);
      uint8_t luma_idx = Y >> 2;
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color_ssse3((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          uint8_t color_idx2 = rgb_to_256color_ssse3((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
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

        // Emit UTF-8 character from cache
        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            // Emit UTF-8 character from cache
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
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

        // Emit UTF-8 character from cache
        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            // Emit UTF-8 character from cache
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
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

#endif /* SIMD_SUPPORT_SSSE3 */
