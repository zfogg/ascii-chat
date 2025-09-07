#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avx2.h"
#include "common.h"
#include "ansi_fast.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// AVX2 optimized monochrome renderer with true 32-pixel processing per iteration
// Unlike NEON which uses vqtbl4q_u8 table lookups, AVX2 leverages:
// - Direct RGB gathering from 32 consecutive pixels (32 pixels at once)
// - 256-bit register processing with 16-bit intermediate math for precision
// - Bit-shifting luminance approximation to prevent overflow
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

    // AVX2 vectorized processing: TRUE 32 pixels per iteration - no splitting!
    for (; x + 31 < w; x += 32) {
      // Gather RGB values from 32 consecutive pixels using simple scalar loop
      // (Compiler will likely vectorize this automatically)
      uint8_t r_vals[32], g_vals[32], b_vals[32];
      for (int i = 0; i < 32; i++) {
        const rgb_pixel_t *p = &row[x + i];
        r_vals[i] = p->r;
        g_vals[i] = p->g;
        b_vals[i] = p->b;
      }

      // Calculate luminance for ALL 32 pixels using TRUE AVX2 8-bit processing
      // Y = (77*R + 150*G + 29*B + 128) >> 8 - prevent overflow using bit shifting
      uint8_t luminance_results[32];

      // Load all 32 RGB values into AVX2 registers
      __m256i r_all = _mm256_loadu_si256((__m256i *)r_vals);
      __m256i g_all = _mm256_loadu_si256((__m256i *)g_vals);
      __m256i b_all = _mm256_loadu_si256((__m256i *)b_vals);

      // AVX2 true 8-bit luminance using weighted average approach
      // Use bit shifting instead of exact coefficients to avoid overflow
      // Approximation: Y ≈ R/4 + G/2 + B/8 (close to Y = 0.299*R + 0.587*G + 0.114*B)

      // Right shift to prevent overflow: R>>2, G>>1, B>>3
      __m256i r_shifted = _mm256_srli_epi16(_mm256_unpacklo_epi8(r_all, _mm256_setzero_si256()), 2);
      __m256i g_shifted = _mm256_srli_epi16(_mm256_unpacklo_epi8(g_all, _mm256_setzero_si256()), 1);
      __m256i b_shifted = _mm256_srli_epi16(_mm256_unpacklo_epi8(b_all, _mm256_setzero_si256()), 3);

      // Add weighted components (no overflow possible now)
      __m256i luma_16_lo = _mm256_add_epi16(_mm256_add_epi16(r_shifted, g_shifted), b_shifted);

      // Process high 16 pixels (completing the full 32-pixel vector processing)
      __m256i r_shifted_hi = _mm256_srli_epi16(_mm256_unpackhi_epi8(r_all, _mm256_setzero_si256()), 2);
      __m256i g_shifted_hi = _mm256_srli_epi16(_mm256_unpackhi_epi8(g_all, _mm256_setzero_si256()), 1);
      __m256i b_shifted_hi = _mm256_srli_epi16(_mm256_unpackhi_epi8(b_all, _mm256_setzero_si256()), 3);

      __m256i luma_16_hi = _mm256_add_epi16(_mm256_add_epi16(r_shifted_hi, g_shifted_hi), b_shifted_hi);

      // Pack back to 8-bit and store all 32 luminance results
      __m256i luma_final = _mm256_packus_epi16(luma_16_lo, luma_16_hi);
      _mm256_storeu_si256((__m256i *)luminance_results, luma_final);

      // Convert luminance to characters and emit directly - no intermediate arrays!
      for (int i = 0; i < 32; i++) {
        const uint8_t luminance = luminance_results[i];
        const uint8_t luma_idx = luminance >> 2; // 0-255 -> 0-63
        const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

        // Emit UTF-8 character directly to output buffer
        switch (char_info->byte_len) {
        case 1:
          *pos++ = char_info->utf8_bytes[0];
          break;
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

    // Handle remaining pixels (scalar fallback)
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];

      switch (char_info->byte_len) {
      case 1:
        *pos++ = char_info->utf8_bytes[0];
        break;
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

    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0';
  return output;
}

// AVX2 helper function to convert 32-bit RGB arrays to 256-color indices using pure 32-bit arithmetic
static inline void avx2_rgb_to_256color_pure_32bit(const uint32_t r_vals[32], const uint32_t g_vals[32],
                                                   const uint32_t b_vals[32], uint32_t color_indices[32]) {
  // 256-color cube quantization: 16 + 36*R_q + 6*G_q + B_q where R_q,G_q,B_q ∈ [0,5]
  // Quantize to 6 levels using 32-bit arithmetic: q = (color * 5 + 127) / 255 ≈ (color * 5 + 127) >> 8

  const __m256i five = _mm256_set1_epi32(5);
  const __m256i round = _mm256_set1_epi32(127);
  const __m256i sixteen = _mm256_set1_epi32(16);
  const __m256i thirtysix = _mm256_set1_epi32(36);
  const __m256i six = _mm256_set1_epi32(6);

  // Process all 32 pixels using 4 AVX2 registers each for R, G, B (stay in 32-bit)
  for (int batch = 0; batch < 4; batch++) {
    int offset = batch * 8;

    // Load 8 RGB values as 32-bit
    __m256i r_8 = _mm256_loadu_si256((__m256i *)&r_vals[offset]);
    __m256i g_8 = _mm256_loadu_si256((__m256i *)&g_vals[offset]);
    __m256i b_8 = _mm256_loadu_si256((__m256i *)&b_vals[offset]);

    // Quantize using pure 32-bit arithmetic: q = (color * 5 + 127) >> 8
    __m256i rq = _mm256_srli_epi32(_mm256_add_epi32(_mm256_mullo_epi32(r_8, five), round), 8);
    __m256i gq = _mm256_srli_epi32(_mm256_add_epi32(_mm256_mullo_epi32(g_8, five), round), 8);
    __m256i bq = _mm256_srli_epi32(_mm256_add_epi32(_mm256_mullo_epi32(b_8, five), round), 8);

    // Calculate 256-color index using pure 32-bit: 16 + 36*R_q + 6*G_q + B_q
    __m256i idx = _mm256_add_epi32(sixteen, _mm256_add_epi32(_mm256_mullo_epi32(rq, thirtysix),
                                                             _mm256_add_epi32(_mm256_mullo_epi32(gq, six), bq)));

    // Store as 32-bit (no packing to 8-bit)
    _mm256_storeu_si256((__m256i *)&color_indices[offset], idx);
  }
}

// AVX2 helper function to build lookup tables (replaces NEON vqtbl4q_u8)
static inline void avx2_build_utf8_lookup_arrays(utf8_palette_cache_t *utf8_cache, uint8_t char_indices[64],
                                                 uint8_t char_lengths[64], uint8_t char_byte0[64],
                                                 uint8_t char_byte1[64], uint8_t char_byte2[64],
                                                 uint8_t char_byte3[64]) {
  // Build lookup arrays for AVX2 gather operations
  for (int i = 0; i < 64; i++) {
    char_indices[i] = (uint8_t)i; // Direct mapping: luminance bucket -> cache64 index
    const utf8_char_t *char_info = &utf8_cache->cache64[i];

    char_lengths[i] = char_info->byte_len;
    char_byte0[i] = char_info->utf8_bytes[0];
    char_byte1[i] = char_info->byte_len > 1 ? char_info->utf8_bytes[1] : 0;
    char_byte2[i] = char_info->byte_len > 2 ? char_info->utf8_bytes[2] : 0;
    char_byte3[i] = char_info->byte_len > 3 ? char_info->utf8_bytes[3] : 0;
  }
}

// AVX2 color implementation with full 32-pixel vectorized processing
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

  // Get cached UTF-8 character mappings
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for AVX2 color");
    return NULL;
  }

  // Build AVX2 lookup arrays (replaces NEON table lookups)
  uint8_t char_indices[64], char_lengths[64];
  uint8_t char_byte0[64], char_byte1[64], char_byte2[64], char_byte3[64];
  avx2_build_utf8_lookup_arrays(utf8_cache, char_indices, char_lengths, char_byte0, char_byte1, char_byte2, char_byte3);

  // Initialize output buffer
  outbuf_t ob = {0};
  size_t bytes_per_pixel = use_256color ? 6u : 15u; // Estimate for ANSI codes
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  const rgb_pixel_t *pixels_data = (const rgb_pixel_t *)image->pixels;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &pixels_data[y * width];
    int x = 0;

    // AVX2 vectorized processing: 32 pixels per iteration (true 32-pixel processing with 256-bit registers)
    while (x + 32 <= width) {
      // Gather RGB values from 32 consecutive pixels (same as monochrome implementation)
      uint8_t r_vals[32], g_vals[32], b_vals[32];

      // Efficiently gather RGB values (compiler will likely vectorize this loop)
      for (int i = 0; i < 32; i++) {
        const rgb_pixel_t *p = &row[x + i];
        r_vals[i] = p->r;
        g_vals[i] = p->g;
        b_vals[i] = p->b;
      }

      // Calculate luminance for ALL 32 pixels using TRUE AVX2 8-bit processing
      // Y ≈ R/4 + G/2 + B/8 - bit shifting prevents overflow, stays in 8-bit domain
      uint8_t luminance_vals[32];

      // Load all 32 RGB values into AVX2 registers
      __m256i r_all = _mm256_loadu_si256((__m256i *)r_vals);
      __m256i g_all = _mm256_loadu_si256((__m256i *)g_vals);
      __m256i b_all = _mm256_loadu_si256((__m256i *)b_vals);

      // AVX2 bit-shift luminance approximation to prevent overflow
      __m256i r_shifted = _mm256_srli_epi16(_mm256_unpacklo_epi8(r_all, _mm256_setzero_si256()), 2);
      __m256i g_shifted = _mm256_srli_epi16(_mm256_unpacklo_epi8(g_all, _mm256_setzero_si256()), 1);
      __m256i b_shifted = _mm256_srli_epi16(_mm256_unpacklo_epi8(b_all, _mm256_setzero_si256()), 3);

      __m256i luma_16_lo = _mm256_add_epi16(_mm256_add_epi16(r_shifted, g_shifted), b_shifted);

      // Process high 16 pixels (completing the full 32-pixel vector processing)
      __m256i r_shifted_hi = _mm256_srli_epi16(_mm256_unpackhi_epi8(r_all, _mm256_setzero_si256()), 2);
      __m256i g_shifted_hi = _mm256_srli_epi16(_mm256_unpackhi_epi8(g_all, _mm256_setzero_si256()), 1);
      __m256i b_shifted_hi = _mm256_srli_epi16(_mm256_unpackhi_epi8(b_all, _mm256_setzero_si256()), 3);

      __m256i luma_16_hi = _mm256_add_epi16(_mm256_add_epi16(r_shifted_hi, g_shifted_hi), b_shifted_hi);

      // Pack and store all 32 luminance results
      __m256i luma_final = _mm256_packus_epi16(luma_16_lo, luma_16_hi);
      _mm256_storeu_si256((__m256i *)luminance_vals, luma_final);

      // Convert luminance to character indices using 8-bit values
      uint8_t char_idx_results[32];
      for (int i = 0; i < 32; i++) {
        char_idx_results[i] = luminance_vals[i] >> 2; // Divide by 4 for 0-63 range
      }

      if (use_256color) {
        // 256-color mode: Convert RGB values using scalar method for now
        uint8_t color_indices[32];
        for (int i = 0; i < 32; i++) {
          color_indices[i] = rgb_to_256color(r_vals[i], g_vals[i], b_vals[i]);
        }

        // Emit with run-length encoding (similar to NEON implementation)
        for (int i = 0; i < 32;) {
          const uint8_t char_idx = char_idx_results[i];
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t color_idx = color_indices[i];

          // Find run length of same character and color
          uint32_t run = 1;
          while (i + run < 32 && char_idx_results[i + run] == char_idx && color_indices[i + run] == color_idx) {
            run++;
          }

          // Set color if changed
          if (color_idx != cur_color_idx) {
            if (use_background) {
              emit_set_256_color_bg(&ob, color_idx);
            } else {
              emit_set_256_color_fg(&ob, color_idx);
            }
            cur_color_idx = color_idx;
          }

          // Emit character with RLE optimization
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          i += run;
        }
      } else {
        // Truecolor mode: process each pixel with RLE using 8-bit RGB values
        for (int i = 0; i < 32;) {
          const uint8_t char_idx = char_idx_results[i];
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t R = r_vals[i], G = g_vals[i], B = b_vals[i];

          // Find run length of same character and color
          uint32_t run = 1;
          while (i + run < 32 && char_idx_results[i + run] == char_idx && r_vals[i + run] == R &&
                 g_vals[i + run] == G && b_vals[i + run] == B) {
            run++;
          }

          // Set color if changed
          if (R != curR || G != curG || B != curB) {
            if (use_background) {
              emit_set_truecolor_bg(&ob, R, G, B);
            } else {
              emit_set_truecolor_fg(&ob, R, G, B);
            }
            curR = R;
            curG = G;
            curB = B;
          }

          // Emit character with RLE optimization
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          i += run;
        }
      }
      x += 32;
    }

    // Scalar tail processing for remaining pixels (similar to NEON implementation)
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + 128u) >> 8);
      uint8_t luma_idx = Y >> 2; // 0-63 index
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];
      const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + 128u) >> 8);
          uint8_t char_idx2 = utf8_cache->char_index_ramp[Y2 >> 2];
          uint8_t color_idx2 = rgb_to_256color((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
          if (char_idx2 != char_idx || color_idx2 != color_idx)
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

        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
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
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + 128u) >> 8);
          uint8_t char_idx2 = utf8_cache->char_index_ramp[Y2 >> 2];
          if (char_idx2 != char_idx || R2 != R || G2 != G || B2 != B)
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

        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x = j;
      }
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