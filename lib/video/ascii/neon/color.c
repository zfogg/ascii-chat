/**
 * @file video/ascii/color.c
 * @ingroup video
 * @brief ARM NEON-accelerated ASCII 16/256/truecolor foreground+background rendering
 */

#if SIMD_SUPPORT_NEON
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <ascii-chat/atomic.h>
#include <math.h>

#include <arm_neon.h>

#include <ascii-chat/common.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/video/ascii/neon.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/ansi_fast.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>

//=============================================================================
// Optimized NEON Color Converter (based on ChatGPT reference)
//=============================================================================

// Unified optimized NEON converter (foreground/background + 256-color/truecolor)
char *render_ascii_neon_unified_optimized(const image_t *image, bool use_background, bool use_256color,
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

  outbuf_t ob = {0};
  // Estimate buffer size based on mode
  size_t bytes_per_pixel = use_256color ? 6u : 8u; // 256-color shorter than truecolor

  // Calculate buffer size with overflow checking
  size_t height_times_width;
  if (checked_size_mul((size_t)height, (size_t)width, &height_times_width) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: height * width overflow");
    return NULL;
  }

  size_t pixel_data_size;
  if (checked_size_mul(height_times_width, bytes_per_pixel, &pixel_data_size) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: (height * width) * bytes_per_pixel overflow");
    return NULL;
  }

  size_t height_times_16;
  if (checked_size_mul((size_t)height, 16u, &height_times_16) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: height * 16 overflow");
    return NULL;
  }

  size_t temp;
  if (checked_size_add(pixel_data_size, height_times_16, &temp) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: pixel_data + height*16 overflow");
    return NULL;
  }

  if (checked_size_add(temp, 64u, &ob.cap) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: total capacity overflow");
    return NULL;
  }

  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  START_TIMER("neon_utf8_cache");
  // Get cached UTF-8 character mappings (like monochrome function does)
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for NEON color");
    return NULL;
  }
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 3 * NS_PER_MS_INT, "neon_utf8_cache",
                           "NEON_UTF8_CACHE: Complete (%.2f ms)");

  START_TIMER("neon_lookup_tables");
  // Build NEON lookup table inline (faster than caching - 30ns rebuild vs 50ns lookup)
  uint8x16x4_t tbl, char_lut, length_lut, char_byte0_lut, char_byte1_lut, char_byte2_lut, char_byte3_lut;
  build_neon_lookup_tables(utf8_cache, &tbl, &char_lut, &length_lut, &char_byte0_lut, &char_byte1_lut, &char_byte2_lut,
                           &char_byte3_lut);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 3 * NS_PER_MS_INT, "neon_lookup_tables",
                           "NEON_LOOKUP_TABLES: Complete (%.2f ms)");

  // Suppress unused variable warnings for color mode
  (void)char_lut;
  (void)length_lut;
  (void)char_byte0_lut;
  (void)char_byte1_lut;
  (void)char_byte2_lut;
  (void)char_byte3_lut;

  START_TIMER("neon_main_loop");
  uint64_t loop_start_ns = time_get_ns();

  // Track which code path is taken
  int chunks_256color = 0, chunks_truecolor = 0;

  // PRE-INITIALIZE: Call init once before loop
  init_neon_decimal_table();

  // Process rows
  for (int y = 0; y < height; y++) {
    // Track current color state
    int curR = -1, curG = -1, curB = -1;
    int cur_color_idx = -1;

    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 16-pixel chunks with NEON
    while (x + 16 <= width) {
      // Load 16 pixels: R,G,B interleaved
      const uint8_t *p = (const uint8_t *)(row + x);
      uint8x16x3_t pix = vld3q_u8(p); // 48 bytes

      // Vector luminance: Y ≈ (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t ylo = vmull_u8(vget_low_u8(pix.val[0]), vdup_n_u8(LUMA_RED));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[1]), vdup_n_u8(LUMA_GREEN));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[2]), vdup_n_u8(LUMA_BLUE));
      ylo = vaddq_u16(ylo, vdupq_n_u16(LUMA_THRESHOLD));
      ylo = vshrq_n_u16(ylo, 8);

      uint16x8_t yhi = vmull_u8(vget_high_u8(pix.val[0]), vdup_n_u8(LUMA_RED));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[1]), vdup_n_u8(LUMA_GREEN));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[2]), vdup_n_u8(LUMA_BLUE));
      yhi = vaddq_u16(yhi, vdupq_n_u16(LUMA_THRESHOLD));
      yhi = vshrq_n_u16(yhi, 8);

      uint8x16_t y8 = vcombine_u8(vmovn_u16(ylo), vmovn_u16(yhi));
      uint8x16_t idx = vshrq_n_u8(y8, 2); // 0..63

      // FAST: Use vqtbl4q_u8 to get character indices from the ramp
      uint8x16_t char_indices = vqtbl4q_u8(tbl, idx);

      if (use_256color) {
        chunks_256color++;
        // 256-color mode: VECTORIZED color quantization
        uint8_t char_idx_buf[16], color_indices[16];
        vst1q_u8(char_idx_buf, char_indices); // Character indices from SIMD lookup

        // VECTORIZED: Use existing optimized 256-color quantization
        uint8x16_t color_indices_vec = palette256_index_dithered_neon(pix.val[0], pix.val[1], pix.val[2], x);
        vst1q_u8(color_indices, color_indices_vec);

        // Emit with RLE on (UTF-8 character, color) runs using SIMD-derived indices
        for (int i = 0; i < 16;) {
          const uint8_t char_idx = char_idx_buf[i]; // From vqtbl4q_u8 lookup
          const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];
          const uint8_t color_idx = color_indices[i];

          // NEON-optimized RLE detection
          const uint32_t run =
              (uint32_t)find_rle_run_length_neon(char_idx_buf, color_indices, i, 16, char_idx, color_idx);

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
          i += run;
        }
      } else {
        chunks_truecolor++;
        // VECTORIZED: Truecolor mode with full SIMD pipeline (no scalar spillover)
        char temp_buffer[16 * 50]; // Temporary buffer for 16 ANSI sequences (up to 50 bytes each)
        size_t vectorized_length =
            neon_assemble_truecolor_sequences_true_simd(char_indices, pix.val[0], pix.val[1], pix.val[2], utf8_cache,
                                                        temp_buffer, sizeof(temp_buffer), use_background);

        // Write vectorized output to main buffer
        ob_write(&ob, temp_buffer, vectorized_length);
      }
      x += 16;
    }

    // Scalar tail for remaining pixels
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + LUMA_THRESHOLD) >> 8);
      uint8_t luma_idx = Y >> 2; // 0-63 index (matches SIMD: cache64 is indexed by luminance bucket)
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          uint8_t luma_idx2 = Y2 >> 2;
          uint8_t color_idx2 = rgb_to_256color((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
          if (luma_idx2 != luma_idx || color_idx2 != color_idx)
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
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x = j;
      } else {
        // Truecolor scalar tail with UTF-8 characters using cached lookups
        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          uint8_t luma_idx2 = Y2 >> 2; // Compare luminance buckets (matches SIMD)
          if (luma_idx2 != luma_idx || R2 != R || G2 != G || B2 != B)
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
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x = j;
      }
    }

    // End row: reset SGR, add newline (except for last row)
    emit_reset(&ob);
    if (y < height - 1) {
      ob_putc(&ob, '\n');
    }
  }

  uint64_t loop_end_ns = time_get_ns();
  uint64_t loop_time_ms = (loop_end_ns - loop_start_ns) / NS_PER_MS_INT;
  log_dev("NEON_MAIN_LOOP_ACTUAL: %llu ms for %d rows, %d width", loop_time_ms, height, width);

  // Log chunks per mode
  log_dev(
      "NEON_MAIN_LOOP processed %d rows x %d width = %d pixels in %llu ms (256color: %d chunks, truecolor: %d chunks)",
      height, width, height * width, loop_time_ms, chunks_256color, chunks_truecolor);

  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "neon_main_loop",
                           "NEON_MAIN_LOOP: Complete (%.2f ms)");

  ob_term(&ob);
  return ob.buf;
}

#endif
