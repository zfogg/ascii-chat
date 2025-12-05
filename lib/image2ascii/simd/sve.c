/**
 * @file image2ascii/simd/sve.c
 * @ingroup image2ascii
 * @brief 🚀 ARM SVE (Scalable Vector Extension) ASCII rendering with variable-length vectors
 */

#if SIMD_SUPPORT_SVE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sve.h"
#include "common.h"
#include "ascii_simd.h"    // For LUMA_RED, LUMA_GREEN, LUMA_BLUE, LUMA_THRESHOLD
#include "output_buffer.h" // For outbuf_t, emit_*, ob_*

#include <arm_sve.h>

//=============================================================================
// Image-based API (matches NEON architecture)
//=============================================================================

// Simple monochrome ASCII function (matches scalar image_print performance)
char *render_ascii_image_monochrome_sve(const image_t *image, const char *ascii_chars) {
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

      // Pack back to 8-bit (narrow from u16 to u8)
      svuint8_t luminance = svqxtnb_u16(luma);

      // Store and convert to ASCII characters
      uint8_t luma_array[64];
      svst1_u8(pg_active, luma_array, luminance);

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
char *render_ascii_sve_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                         const char *ascii_chars) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    empty = SAFE_MALLOC(1, char *);
    empty[0] = '\0';
    return empty;
  }

  // Use monochrome optimization for simple case
  if (!use_background && !use_256color) {
    return render_ascii_image_monochrome_sve(image, ascii_chars);
  }

  outbuf_t ob = {0};
  // Estimate buffer size based on mode (copied from NEON)
  size_t bytes_per_pixel = use_256color ? 6u : 8u; // 256-color shorter than truecolor
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // Get cached UTF-8 character mappings for color rendering
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for SVE color");
    return NULL;
  }

  // Track current color state (copied from NEON)
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process with SVE scalable vectors (adapts to hardware vector length)
    while (x < width) {
      svbool_t pg_active = svwhilelt_b8_s32(x, width);
      int vec_len = svcntb_pat(SV_ALL) / 3; // Vector length in RGB pixels
      int remaining = width - x;
      int process_count = (remaining < vec_len) ? remaining : vec_len;

      // Manual deinterleave RGB components (SVE limitation vs NEON's vld3)
      uint8_t r_array[64], g_array[64], b_array[64]; // Max SVE vector size
      for (int j = 0; j < process_count; j++) {
        if (x + j < width) {
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

      // Pack back to 8-bit and store (narrow from u16 to u8)
      svuint8_t luminance = svqxtnb_u16(luma);
      uint8_t luma_array[64];
      svst1_u8(pg_active, luma_array, luminance);

      // FAST: Use svtbl_u8 to get character indices from the ramp (SVE advantage)
      // Convert luminance to 0-63 indices
      svuint8_t luma_vec = svld1_u8(pg_active, luma_array);             // Load luminance values
      svuint8_t luma_idx_vec = svlsr_n_u8_x(svptrue_b8(), luma_vec, 2); // >> 2 for 0-63

      // Use svtbl_u8 for fast character index lookup (scalable!)
      svuint8_t char_lut_vec = svld1_u8(svptrue_b8(), utf8_cache->char_index_ramp);
      svuint8_t char_indices_vec = svtbl_u8(char_lut_vec, luma_idx_vec);

      uint8_t gbuf[64]; // Reuse gbuf name for compatibility
      svst1_u8(pg_active, gbuf, char_indices_vec);

      if (use_256color) {
        // 256-color mode processing (copied from NEON logic)
        uint8_t color_indices[64];
        for (int i = 0; i < process_count; i++) {
          color_indices[i] = rgb_to_256color_sve(r_array[i], g_array[i], b_array[i]);
        }

        // Emit with RLE on (glyph, color) runs (copied from NEON)
        for (int i = 0; i < process_count;) {
          const uint8_t char_idx = gbuf[i]; // This is now the character index
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t color_idx = color_indices[i];

          int j = i + 1;
          while (j < process_count && gbuf[j] == char_idx && color_indices[j] == color_idx) {
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
        for (int i = 0; i < process_count;) {
          const uint8_t char_idx = gbuf[i]; // This is now the character index
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t r = r_array[i];
          const uint8_t g = g_array[i];
          const uint8_t b = b_array[i];

          int j = i + 1;
          while (j < process_count && gbuf[j] == char_idx && r_array[j] == r && g_array[j] == g && b_array[j] == b) {
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
      x += process_count;
    }

    // Scalar tail for any remaining pixels (copied from NEON logic)
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + LUMA_THRESHOLD) >> 8);
      uint8_t luma_idx = Y >> 2;
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color_sve((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          uint8_t color_idx2 = rgb_to_256color_sve((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
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
          uint8_t Y2 = (uint8_t)((77u * R2 + 150u * G2 + 29u * B2 + 128u) >> 8);
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

// Destroy SVE cache resources (called at program shutdown)
void sve_caches_destroy(void) {
  // SVE currently uses shared caches from common.c, so no specific cleanup needed
  log_debug("SVE_CACHE: SVE caches cleaned up");
}

#endif /* SIMD_SUPPORT_SVE */
