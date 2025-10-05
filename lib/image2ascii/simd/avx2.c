#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "avx2.h"
#include "common.h"
#include "../output_buffer.h"
#include "../../buffer_pool.h"
#include "../../ansi_fast.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// Simple emission functions for direct buffer writing
static inline char *emit_set_256_color_fg_simple(char *pos, uint8_t color_idx) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '3';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '5';
  *pos++ = ';';
  if (color_idx >= 100) {
    *pos++ = '0' + (color_idx / 100);
    *pos++ = '0' + ((color_idx / 10) % 10);
    *pos++ = '0' + (color_idx % 10);
  } else if (color_idx >= 10) {
    *pos++ = '0' + (color_idx / 10);
    *pos++ = '0' + (color_idx % 10);
  } else {
    *pos++ = '0' + color_idx;
  }
  *pos++ = 'm';
  return pos;
}

static inline char *emit_set_256_color_bg_simple(char *pos, uint8_t color_idx) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '4';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '5';
  *pos++ = ';';
  if (color_idx >= 100) {
    *pos++ = '0' + (color_idx / 100);
    *pos++ = '0' + ((color_idx / 10) % 10);
    *pos++ = '0' + (color_idx % 10);
  } else if (color_idx >= 10) {
    *pos++ = '0' + (color_idx / 10);
    *pos++ = '0' + (color_idx % 10);
  } else {
    *pos++ = '0' + color_idx;
  }
  *pos++ = 'm';
  return pos;
}

static inline char *emit_set_truecolor_fg_simple(char *pos, uint8_t r, uint8_t g, uint8_t b) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '3';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '2';
  *pos++ = ';';
  if (r >= 100) {
    *pos++ = '0' + (r / 100);
    *pos++ = '0' + ((r / 10) % 10);
    *pos++ = '0' + (r % 10);
  } else if (r >= 10) {
    *pos++ = '0' + (r / 10);
    *pos++ = '0' + (r % 10);
  } else {
    *pos++ = '0' + r;
  }
  *pos++ = ';';
  if (g >= 100) {
    *pos++ = '0' + (g / 100);
    *pos++ = '0' + ((g / 10) % 10);
    *pos++ = '0' + (g % 10);
  } else if (g >= 10) {
    *pos++ = '0' + (g / 10);
    *pos++ = '0' + (g % 10);
  } else {
    *pos++ = '0' + g;
  }
  *pos++ = ';';
  if (b >= 100) {
    *pos++ = '0' + (b / 100);
    *pos++ = '0' + ((b / 10) % 10);
    *pos++ = '0' + (b % 10);
  } else if (b >= 10) {
    *pos++ = '0' + (b / 10);
    *pos++ = '0' + (b % 10);
  } else {
    *pos++ = '0' + b;
  }
  *pos++ = 'm';
  return pos;
}

static inline char *emit_set_truecolor_bg_simple(char *pos, uint8_t r, uint8_t g, uint8_t b) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '4';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '2';
  *pos++ = ';';
  if (r >= 100) {
    *pos++ = '0' + (r / 100);
    *pos++ = '0' + ((r / 10) % 10);
    *pos++ = '0' + (r % 10);
  } else if (r >= 10) {
    *pos++ = '0' + (r / 10);
    *pos++ = '0' + (r % 10);
  } else {
    *pos++ = '0' + r;
  }
  *pos++ = ';';
  if (g >= 100) {
    *pos++ = '0' + (g / 100);
    *pos++ = '0' + ((g / 10) % 10);
    *pos++ = '0' + (g % 10);
  } else if (g >= 10) {
    *pos++ = '0' + (g / 10);
    *pos++ = '0' + (g % 10);
  } else {
    *pos++ = '0' + g;
  }
  *pos++ = ';';
  if (b >= 100) {
    *pos++ = '0' + (b / 100);
    *pos++ = '0' + ((b / 10) % 10);
    *pos++ = '0' + (b % 10);
  } else if (b >= 10) {
    *pos++ = '0' + (b / 10);
    *pos++ = '0' + (b % 10);
  } else {
    *pos++ = '0' + b;
  }
  *pos++ = 'm';
  return pos;
}

// Thread-local storage for AVX2 working buffers
// These stay in L1 cache and are reused across function calls
static THREAD_LOCAL ALIGNED_32 uint8_t avx2_r_buffer[32];
static THREAD_LOCAL ALIGNED_32 uint8_t avx2_g_buffer[32];
static THREAD_LOCAL ALIGNED_32 uint8_t avx2_b_buffer[32];
static THREAD_LOCAL ALIGNED_32 uint8_t avx2_luminance_buffer[32];

// Optimized AVX2 function to load 32 RGB pixels and separate channels
// Uses simple loop that auto-vectorizes to VMOVDQU + VPSHUFB
static inline void avx2_load_rgb32_optimized(const rgb_pixel_t *__restrict pixels, uint8_t *__restrict r_out,
                                             uint8_t *__restrict g_out, uint8_t *__restrict b_out) {
  // Simple loop that compiler auto-vectorizes into efficient SIMD
  for (int i = 0; i < 32; i++) {
    r_out[i] = pixels[i].r;
    g_out[i] = pixels[i].g;
    b_out[i] = pixels[i].b;
  }
}

// AVX2 function to compute luminance for 32 pixels
static inline void avx2_compute_luminance_32(const uint8_t *r_vals, const uint8_t *g_vals, const uint8_t *b_vals,
                                             uint8_t *luminance_out) {
  // Load all 32 RGB values into AVX2 registers
  __m256i r_all = _mm256_loadu_si256((__m256i *)r_vals);
  __m256i g_all = _mm256_loadu_si256((__m256i *)g_vals);
  __m256i b_all = _mm256_loadu_si256((__m256i *)b_vals);

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

  // Fix the 128-bit lane-local packing: [lo0..7, hi0..7, lo8..15, hi8..15] -> [lo0..15, hi0..15]
  // After packing, bytes are in [Q0,Q1,Q2,Q3] = [0-7, 16-23, 8-15, 24-31]
  // We want [0-15, 16-31] = [Q0,Q2,Q1,Q3]
  // Use permute4x64 with 0xD8 = 0b11011000 = (3,1,2,0) to swap middle quarters
  __m256i luma_final = _mm256_permute4x64_epi64(luma_packed, 0xD8);

  _mm256_storeu_si256((__m256i *)luminance_out, luma_final);
}

// Single-pass AVX2 monochrome renderer with immediate emission
char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars) {
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
  // Each pixel can produce: 4 bytes UTF-8 + 6 bytes RLE escape (\x1b[999b) = 10 bytes max
  // Plus 1 newline per row
  size_t output_size = (size_t)h * ((size_t)w * 10 + 1);

  char *output = (char *)malloc(output_size);
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
          *pos++ = '\x1b';
          *pos++ = '[';
          // Emit run count (run - 1 for REP command)
          uint32_t rep_count = run - 1;
          if (rep_count >= 100) {
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
        *pos++ = '\x1b';
        *pos++ = '[';
        // Emit run count (run - 1 for REP command)
        uint32_t rep_count = run - 1;
        if (rep_count >= 100) {
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

  // Use malloc for output buffer (will be freed by caller)
  size_t bytes_per_pixel = use_256color ? 10u : 25u; // Conservative estimates
  size_t output_size = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 1024u;

  char *output = (char *)malloc(output_size);
  if (!output) {
    log_error("Failed to allocate output buffer for AVX2 color rendering");
    return NULL;
  }

  char *pos = output;
  const rgb_pixel_t *pixels_data = (const rgb_pixel_t *)image->pixels;

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  // Generate output row by row with single-pass processing
  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row_pixels = &pixels_data[y * width];
    int x = 0;

    // AVX2 fast path: process 32 pixels at a time
    while (x + 31 < width) {
      // Process 32 pixels with AVX2 using thread-local buffers
      avx2_load_rgb32_optimized(&row_pixels[x], avx2_r_buffer, avx2_g_buffer, avx2_b_buffer);
      avx2_compute_luminance_32(avx2_r_buffer, avx2_g_buffer, avx2_b_buffer, avx2_luminance_buffer);

      // Process each pixel in the chunk
      int i = 0;
      while (i < 32) {
        const uint8_t R = avx2_r_buffer[i];
        const uint8_t G = avx2_g_buffer[i];
        const uint8_t B = avx2_b_buffer[i];
        const uint8_t luma_idx = avx2_luminance_buffer[i] >> 2;
        const uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];
        const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];

        if (use_256color) {
          uint8_t color_idx = rgb_to_256color(R, G, B);

          // Find run length
          int run = 1;
          while (i + run < 32 && x + run < width) {
            const uint8_t next_R = avx2_r_buffer[i + run];
            const uint8_t next_G = avx2_g_buffer[i + run];
            const uint8_t next_B = avx2_b_buffer[i + run];
            const uint8_t next_luma_idx = avx2_luminance_buffer[i + run] >> 2;
            const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
            if (next_char_idx != char_idx)
              break;
            if (rgb_to_256color(next_R, next_G, next_B) != color_idx)
              break;
            run++;
          }

          // Set color if changed
          if (color_idx != cur_color_idx) {
            if (use_background) {
              pos = emit_set_256_color_bg_simple(pos, color_idx);
            } else {
              pos = emit_set_256_color_fg_simple(pos, color_idx);
            }
            cur_color_idx = color_idx;
          }

          // Emit character with RLE
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;

          if (rep_is_profitable(run)) {
            *pos++ = '\x1b';
            *pos++ = '[';
            // Emit run count (run - 1 for REP command)
            uint32_t rep_count = run - 1;
            if (rep_count >= 100) {
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
          } else {
            for (int k = 1; k < run; k++) {
              memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
              pos += char_info->byte_len;
            }
          }
          i += run;
        } else {
          // Truecolor mode
          // Find run length
          int run = 1;
          while (i + run < 32 && x + run < width) {
            const uint8_t next_R = avx2_r_buffer[i + run];
            const uint8_t next_G = avx2_g_buffer[i + run];
            const uint8_t next_B = avx2_b_buffer[i + run];
            const uint8_t next_luma_idx = avx2_luminance_buffer[i + run] >> 2;
            const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
            if (next_char_idx != char_idx)
              break;
            if (next_R != R || next_G != G || next_B != B)
              break;
            run++;
          }

          // Set color if changed
          if ((int)R != curR || (int)G != curG || (int)B != curB) {
            if (use_background) {
              pos = emit_set_truecolor_bg_simple(pos, R, G, B);
            } else {
              pos = emit_set_truecolor_fg_simple(pos, R, G, B);
            }
            curR = R;
            curG = G;
            curB = B;
          }

          // Emit character with RLE
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;

          if (rep_is_profitable(run)) {
            *pos++ = '\x1b';
            *pos++ = '[';
            // Emit run count (run - 1 for REP command)
            uint32_t rep_count = run - 1;
            if (rep_count >= 100) {
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
          } else {
            for (int k = 1; k < run; k++) {
              memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
              pos += char_info->byte_len;
            }
          }
          i += run;
        }
      }
      x += 32;
    }

    // Scalar processing for remaining pixels (< 32)
    while (x < width) {
      const rgb_pixel_t *p = &row_pixels[x];
      const uint8_t R = p->r, G = p->g, B = p->b;
      const int luminance = (LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + 128) >> 8;
      const uint8_t luma_idx = luminance >> 2;
      const uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];
      const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];

      if (use_256color) {
        uint8_t color_idx = rgb_to_256color(R, G, B);

        // Find run length
        int run = 1;
        while (x + run < width) {
          const rgb_pixel_t *next_p = &row_pixels[x + run];
          const int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b + 128) >> 8;
          const uint8_t next_luma_idx = next_luminance >> 2;
          const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
          if (next_char_idx != char_idx)
            break;
          if (rgb_to_256color(next_p->r, next_p->g, next_p->b) != color_idx)
            break;
          run++;
        }

        // Set color if changed
        if (color_idx != cur_color_idx) {
          if (use_background) {
            pos = emit_set_256_color_bg_simple(pos, color_idx);
          } else {
            pos = emit_set_256_color_fg_simple(pos, color_idx);
          }
          cur_color_idx = color_idx;
        }

        // Emit character with RLE
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;

        if (rep_is_profitable(run)) {
          *pos++ = '\x1b';
          *pos++ = '[';
          // Emit run count (run - 1 for REP command)
          uint32_t rep_count = run - 1;
          if (rep_count >= 100) {
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
        } else {
          for (int k = 1; k < run; k++) {
            memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
            pos += char_info->byte_len;
          }
        }
        x += run;
      } else {
        // Truecolor mode
        // Find run length
        int run = 1;
        while (x + run < width) {
          const rgb_pixel_t *next_p = &row_pixels[x + run];
          const int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b + 128) >> 8;
          const uint8_t next_luma_idx = next_luminance >> 2;
          const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
          if (next_char_idx != char_idx)
            break;
          if (next_p->r != R || next_p->g != G || next_p->b != B)
            break;
          run++;
        }

        // Set color if changed
        if ((int)R != curR || (int)G != curG || (int)B != curB) {
          if (use_background) {
            pos = emit_set_truecolor_bg_simple(pos, R, G, B);
          } else {
            pos = emit_set_truecolor_fg_simple(pos, R, G, B);
          }
          curR = R;
          curG = G;
          curB = B;
        }

        // Emit character with RLE
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;

        if (rep_is_profitable(run)) {
          *pos++ = '\x1b';
          *pos++ = '[';
          // Emit run count (run - 1 for REP command)
          uint32_t rep_count = run - 1;
          if (rep_count >= 100) {
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
        } else {
          for (int k = 1; k < run; k++) {
            memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
            pos += char_info->byte_len;
          }
        }
        x += run;
      }
    }

    // Add reset sequence and newline after each row (except last)
    *pos++ = '\x1b';
    *pos++ = '[';
    *pos++ = '0';
    *pos++ = 'm';
    if (y < height - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0'; // Null terminate

  return output;
}

// Destroy AVX2 cache resources (called at program shutdown)
void avx2_caches_destroy(void) {
  // AVX2 currently uses shared caches from common.c, so no specific cleanup needed
  log_debug("AVX2_CACHE: AVX2 optimized caches cleaned up");
}

#endif /* SIMD_SUPPORT_AVX2 */
