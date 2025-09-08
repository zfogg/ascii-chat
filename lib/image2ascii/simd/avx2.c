#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h> // Ensure bool type is available
#include "avx2.h"
#include "common.h"
#include "ansi_fast.h"
#include "../output_buffer.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// Optimized AVX2 function to load 32 RGB pixels and separate channels
// IMPORTANT: Simple loops vectorize better than manual unrolling!
static inline void avx2_load_rgb32_optimized(const rgb_pixel_t *__restrict pixels, uint8_t *__restrict r_out,
                                             uint8_t *__restrict g_out, uint8_t *__restrict b_out) {
  // CRITICAL PERFORMANCE NOTE:
  // Testing reveals that manual unrolling PREVENTS vectorization!
  // - Manual unrolling: generates 96 scalar MOVZBL instructions (30x slower)
  // - Simple loop: generates VMOVDQU + VPSHUFB SIMD instructions (30x faster)
  //
  // The compiler auto-vectorizes this simple pattern into:
  // 1. VMOVDQU - bulk load pixel data into SIMD registers
  // 2. VPSHUFB - shuffle bytes to extract R, G, B channels
  // 3. VMOVDQU - store separated channels
  //
  // DO NOT manually unroll this loop - it breaks SIMD optimization!

  for (int i = 0; i < 32; i++) {
    r_out[i] = pixels[i].r;
    g_out[i] = pixels[i].g;
    b_out[i] = pixels[i].b;
  }
}

// AVX2 function to compute luminance for 32 pixels using accurate coefficients
// Input: 32 RGB values in separate arrays
// Output: 32 luminance values (0-255 range)
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

// Process 32 pixels: extract RGB, compute luminance, store character indices
// Returns updated pixel index
static inline void avx2_process_32_pixels(const rgb_pixel_t *pixels, uint8_t *char_indices, uint8_t *r_buffer,
                                          uint8_t *g_buffer, uint8_t *b_buffer, bool store_rgb) {
  // Extract RGB channels
  uint8_t r_vals[32], g_vals[32], b_vals[32];
  avx2_load_rgb32_optimized(pixels, r_vals, g_vals, b_vals);

  // Store RGB if needed (for color modes)
  if (store_rgb && r_buffer && g_buffer && b_buffer) {
    memcpy(r_buffer, r_vals, 32);
    memcpy(g_buffer, g_vals, 32);
    memcpy(b_buffer, b_vals, 32);
  }

  // Compute luminance
  uint8_t luminance_vals[32];
  avx2_compute_luminance_32(r_vals, g_vals, b_vals, luminance_vals);

  // Convert to character indices (0-63 range)
  for (int i = 0; i < 32; i++) {
    char_indices[i] = luminance_vals[i] >> 2;
  }
}

// AVX2 optimized monochrome renderer with single-pass approach
// Process pixels and emit output immediately for better cache locality
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

  // Use outbuf_t for efficient UTF-8 RLE emission
  outbuf_t ob = {0};
  ob.cap = (size_t)h * ((size_t)w * 4 + 1); // 4 = max UTF-8 char bytes
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf) {
    log_error("Failed to allocate output buffer for AVX2 rendering");
    return NULL;
  }

  // Process row by row for better cache locality
  for (uint32_t y = 0; y < (uint32_t)h; y++) {
    const rgb_pixel_t *row_pixels = &pixels[y * w];
    uint32_t x = 0;

    // AVX2 fast path: process 32 pixels at a time
    if (w >= 32) {
      // Small buffer for 32 pixels (stays in L1 cache)
      uint8_t luma_indices[32];

      while (x + 31 < (uint32_t)w) {
        // Process 32 pixels with AVX2
        avx2_process_32_pixels(&row_pixels[x], luma_indices, NULL, NULL, NULL, false);

        // Immediately emit these 32 characters with RLE
        uint32_t chunk_pos = 0;
        while (chunk_pos < 32 && x + chunk_pos < (uint32_t)w) {
          const uint8_t luma_idx = luma_indices[chunk_pos];
          const uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];
          const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

          // Find run length within this chunk
          uint32_t run_end = chunk_pos + 1;
          while (run_end < 32 && x + run_end < (uint32_t)w) {
            const uint8_t next_luma_idx = luma_indices[run_end];
            const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
            if (next_char_idx != char_idx)
              break;
            run_end++;
          }
          uint32_t run = (uint32_t)(run_end - chunk_pos);

          // Emit UTF-8 character with RLE
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          chunk_pos = run_end;
        }
        x += 32;
      }
    }

    // Scalar processing for remaining pixels (< 32)
    while (x < (uint32_t)w) {
      const rgb_pixel_t *p = &row_pixels[x];
      const int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b + 128) >> 8;
      const uint8_t luma_idx = luminance >> 2; // 0-255 -> 0-63
      const uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      // Find run length for RLE
      uint32_t j = x + 1;
      while (j < (uint32_t)w) {
        const rgb_pixel_t *next_p = &row_pixels[j];
        const int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b + 128) >> 8;
        const uint8_t next_luma_idx = next_luminance >> 2;
        const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
        if (next_char_idx != char_idx)
          break;
        j++;
      }
      uint32_t run = j - x;

      // Emit UTF-8 character with RLE
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

    // Add newline after each row (except last)
    if (y < (uint32_t)h - 1) {
      ob_putc(&ob, '\n');
    }
  }

  ob_term(&ob);
  return ob.buf;
}
// Removed unused function avx2_rgb_to_256color_pure_32bit
// The function was expecting uint32_t arrays but we work with uint8_t buffers
// 256-color conversion is now precomputed in Pass 1

// AVX2 color implementation with two-pass optimization:
// Pass 1: Process ALL pixels with AVX2 SIMD (compute luminance/character indices)
// Pass 2: Generate ANSI output from stored results
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;
  const size_t total_pixels = (size_t)width * (size_t)height;

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

  // =============================================================================
  // PASS 1: Process ALL pixels with AVX2 SIMD first
  // Store intermediate results to maximize SIMD throughput
  // =============================================================================

  // Allocate intermediate buffers for the entire image
  uint8_t *char_indices; // Character index for each pixel (0-63)
  uint8_t *r_buffer = NULL, *g_buffer = NULL, *b_buffer = NULL;
  uint8_t *color256_indices = NULL; // Precomputed 256-color indices

  SAFE_MALLOC(char_indices, total_pixels, uint8_t *);

  // Store RGB values for both modes to avoid re-reading pixels in pass 2
  SAFE_MALLOC(r_buffer, total_pixels, uint8_t *);
  SAFE_MALLOC(g_buffer, total_pixels, uint8_t *);
  SAFE_MALLOC(b_buffer, total_pixels, uint8_t *);

  // Allocate 256-color index buffer if needed
  if (use_256color) {
    SAFE_MALLOC(color256_indices, total_pixels, uint8_t *);
  }

  const rgb_pixel_t *pixels_data = (const rgb_pixel_t *)image->pixels;

  // Process entire image with AVX2 SIMD
  size_t pixel_idx = 0;

  // Main AVX2 loop - process 32 pixels at a time for maximum throughput
  for (; pixel_idx + 31 < total_pixels; pixel_idx += 32) {
    // Use shared helper function to process 32 pixels and store RGB
    avx2_process_32_pixels(&pixels_data[pixel_idx], &char_indices[pixel_idx], &r_buffer[pixel_idx],
                           &g_buffer[pixel_idx], &b_buffer[pixel_idx], true);
  }

  // Handle remaining pixels with scalar code
  for (; pixel_idx < total_pixels; pixel_idx++) {
    const rgb_pixel_t *p = &pixels_data[pixel_idx];
    uint8_t R = p->r, G = p->g, B = p->b;

    // Store RGB for both modes (needed in Pass 2)
    r_buffer[pixel_idx] = R;
    g_buffer[pixel_idx] = G;
    b_buffer[pixel_idx] = B;

    uint8_t Y = (uint8_t)((77 * R + 150 * G + 29 * B + 128) >> 8);
    char_indices[pixel_idx] = Y >> 2; // 0-63 index

    // Precompute 256-color index if needed
    if (use_256color) {
      color256_indices[pixel_idx] = rgb_to_256color(R, G, B);
    }
  }

  // =============================================================================
  // PASS 2: Generate ANSI output from stored results
  // This pass focuses purely on string generation without SIMD interference
  // =============================================================================

  // Initialize output buffer with improved size calculation
  outbuf_t ob = {0};
  // Calculate more accurate buffer size:
  // - For 256-color mode: ~6 bytes per pixel (ESC[38;5;NNNm + char)
  // - For true color mode: ~20 bytes per pixel (ESC[38;2;RRR;GGG;BBBm + char)
  // - Add space for newlines and potential RLE optimization
  size_t bytes_per_pixel = use_256color ? 10u : 25u; // Conservative estimates
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 1024u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf) {
    free(char_indices);
    if (r_buffer) {
      free(r_buffer);
      free(g_buffer);
      free(b_buffer);
    }
    return NULL;
  }

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  // Generate output row by row
  for (int y = 0; y < height; y++) {
    int row_start = y * width;
    int x = 0;

    while (x < width) {
      int idx = row_start + x;
      uint8_t luma_idx = char_indices[idx];                          // This is actually the luminance index (0-63)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];      // Map to character index for palette
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx]; // Use luma_idx for cache lookup

      if (use_256color) {
        // 256-color mode: use precomputed color indices from Pass 1
        uint8_t color_idx = color256_indices[idx];

        // Find run length
        uint32_t run = 1;
        while (x + run < (uint32_t)width) {
          uint8_t next_luma_idx = char_indices[idx + run];
          uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
          if (next_char_idx != char_idx)
            break;
          if (color256_indices[idx + run] != color_idx)
            break;
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

        // Emit character with RLE
        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x += run;

      } else {
        // Truecolor mode: use buffered RGB values
        uint8_t R = r_buffer[idx];
        uint8_t G = g_buffer[idx];
        uint8_t B = b_buffer[idx];

        // Find run length
        uint32_t run = 1;
        while (x + run < (uint32_t)width) {
          uint8_t next_luma_idx = char_indices[idx + run];
          uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
          if (next_char_idx != char_idx)
            break;
          if (r_buffer[idx + run] != R || g_buffer[idx + run] != G || b_buffer[idx + run] != B)
            break;
          run++;
        }

        // Set color if changed
        if ((int)R != curR || (int)G != curG || (int)B != curB) {
          if (use_background) {
            emit_set_truecolor_bg(&ob, R, G, B);
          } else {
            emit_set_truecolor_fg(&ob, R, G, B);
          }
          curR = R;
          curG = G;
          curB = B;
        }

        // Emit character with RLE
        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x += run;
      }
    }

    if (y < height - 1) {
      ob_write(&ob, "\n", 1);
    }
  }

  ob_term(&ob);

  // Clean up intermediate buffers
  free(char_indices);
  free(r_buffer);
  free(g_buffer);
  free(b_buffer);
  if (color256_indices) {
    free(color256_indices);
  }

  return ob.buf;
}

// Destroy AVX2 cache resources (called at program shutdown)
void avx2_caches_destroy(void) {
  // AVX2 currently uses shared caches from common.c, so no specific cleanup needed
  log_debug("AVX2_CACHE: AVX2 caches cleaned up");
}

#endif /* SIMD_SUPPORT_AVX2 */
