/**
 * @file video/ascii/mono.c
 * @ingroup video
 * @brief ARM NEON-accelerated ASCII monochrome ascii art terminal rendering
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
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>

//=============================================================================
// Simple Monochrome ASCII Function (matches scalar image_print performance)
//=============================================================================

char *render_ascii_mono_neon(const image_t *image, const char *ascii_chars) {
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

  // Build NEON lookup tables inline (faster than caching - 30ns rebuild vs 50ns lookup)
  uint8x16x4_t tbl, char_lut, length_lut, char_byte0_lut, char_byte1_lut, char_byte2_lut, char_byte3_lut;
  build_neon_lookup_tables(utf8_cache, &tbl, &char_lut, &length_lut, &char_byte0_lut, &char_byte1_lut, &char_byte2_lut,
                           &char_byte3_lut);

  // Estimate output buffer size for UTF-8 characters
  const size_t max_char_bytes = 4; // Max UTF-8 character size

  // Calculate buffer size with overflow checking
  size_t w_times_bytes;
  if (checked_size_mul((size_t)w, max_char_bytes, &w_times_bytes) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: width too large for UTF-8 encoding");
    return NULL;
  }

  size_t w_times_bytes_plus_one;
  if (checked_size_add(w_times_bytes, 1, &w_times_bytes_plus_one) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: width * bytes + 1 overflow");
    return NULL;
  }

  size_t len;
  if (checked_size_mul((size_t)h, w_times_bytes_plus_one, &len) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: height * (width * bytes + 1) overflow");
    return NULL;
  }

  // Use SIMD-aligned allocation for optimal vectorized write performance
  char *output = SAFE_MALLOC_SIMD(len, char *);
  if (output == NULL) {
    return NULL; // SAFE_MALLOC_SIMD already called FATAL, but satisfy analyzer
  }

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure NEON processing - no scalar fallbacks
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process 16 pixels at a time with NEON
    for (; x + 15 < w; x += 16) {
      // Load 16 RGB pixels (48 bytes)
      uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(row + x));

      // Calculate luminance for all 16 pixels: (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t luma_lo = vmull_u8(vget_low_u8(rgb.val[0]), vdup_n_u8(LUMA_RED)); // R * 77
      luma_lo = vmlal_u8(luma_lo, vget_low_u8(rgb.val[1]), vdup_n_u8(LUMA_GREEN)); // + G * 150
      luma_lo = vmlal_u8(luma_lo, vget_low_u8(rgb.val[2]), vdup_n_u8(LUMA_BLUE));  // + B * 29
      luma_lo = vaddq_u16(luma_lo, vdupq_n_u16(128));                              // + 128 (rounding)
      luma_lo = vshrq_n_u16(luma_lo, 8);                                           // >> 8

      uint16x8_t luma_hi = vmull_u8(vget_high_u8(rgb.val[0]), vdup_n_u8(LUMA_RED));
      luma_hi = vmlal_u8(luma_hi, vget_high_u8(rgb.val[1]), vdup_n_u8(LUMA_GREEN));
      luma_hi = vmlal_u8(luma_hi, vget_high_u8(rgb.val[2]), vdup_n_u8(LUMA_BLUE));
      luma_hi = vaddq_u16(luma_hi, vdupq_n_u16(128));
      luma_hi = vshrq_n_u16(luma_hi, 8);

      // Convert 16-bit luminance back to 8-bit
      uint8x16_t luminance = vcombine_u8(vmovn_u16(luma_lo), vmovn_u16(luma_hi));

      // NEON optimization: Use vqtbl4q_u8 for fast character index lookup
      // Convert luminance (0-255) to 6-bit bucket (0-63) to match scalar behavior
      uint8x16_t luma_buckets = vshrq_n_u8(luminance, 2);      // >> 2 to get 0-63 range
      uint8x16_t char_indices = vqtbl4q_u8(tbl, luma_buckets); // 16 lookups in 1 instruction!

      // VECTORIZED UTF-8 CHARACTER GENERATION: Length-aware compaction

      // Step 1: Get character lengths vectorially
      uint8x16_t char_lengths = vqtbl4q_u8(length_lut, char_indices);

      // Step 2: Check if all characters have same length (vectorized check)
      uint8_t uniform_length;
      if (all_same_length_neon(char_lengths, &uniform_length)) {

        if (uniform_length == 1) {
          // PURE ASCII PATH: 16 characters = 16 bytes (maximum vectorization)
          uint8x16_t ascii_output = vqtbl4q_u8(char_lut, char_indices);
          vst1q_u8((uint8_t *)pos, ascii_output);
          pos += 16;

        } else if (uniform_length == 4) {
          // PURE 4-BYTE UTF-8 PATH: 16 characters = 64 bytes
          // Gather all 4 byte streams in parallel
          uint8x16_t byte0_stream = vqtbl4q_u8(char_byte0_lut, char_indices);
          uint8x16_t byte1_stream = vqtbl4q_u8(char_byte1_lut, char_indices);
          uint8x16_t byte2_stream = vqtbl4q_u8(char_byte2_lut, char_indices);
          uint8x16_t byte3_stream = vqtbl4q_u8(char_byte3_lut, char_indices);

          // Interleave bytes: [char0_byte0, char0_byte1, char0_byte2, char0_byte3, char1_byte0, ...]
          uint8x16x4_t interleaved;
          interleaved.val[0] = byte0_stream;
          interleaved.val[1] = byte1_stream;
          interleaved.val[2] = byte2_stream;
          interleaved.val[3] = byte3_stream;

          // Store interleaved UTF-8 data: 64 bytes total
          vst4q_u8((uint8_t *)pos, interleaved);
          pos += 64;

        } else if (uniform_length == 2) {
          // PURE 2-BYTE UTF-8 PATH: 16 characters = 32 bytes (vectorized)
          uint8x16_t byte0_stream = vqtbl4q_u8(char_byte0_lut, char_indices);
          uint8x16_t byte1_stream = vqtbl4q_u8(char_byte1_lut, char_indices);

          // Interleave: [char0_b0, char0_b1, char1_b0, char1_b1, ...]
          uint8x16x2_t interleaved_2byte;
          interleaved_2byte.val[0] = byte0_stream;
          interleaved_2byte.val[1] = byte1_stream;

          vst2q_u8((uint8_t *)pos, interleaved_2byte);
          pos += 32;

        } else if (uniform_length == 3) {
          // PURE 3-BYTE UTF-8 PATH: 16 characters = 48 bytes (vectorized)
          uint8x16_t byte0_stream = vqtbl4q_u8(char_byte0_lut, char_indices);
          uint8x16_t byte1_stream = vqtbl4q_u8(char_byte1_lut, char_indices);
          uint8x16_t byte2_stream = vqtbl4q_u8(char_byte2_lut, char_indices);

          // Interleave: [char0_b0, char0_b1, char0_b2, char1_b0, char1_b1, char1_b2, ...]
          uint8x16x3_t interleaved_3byte;
          interleaved_3byte.val[0] = byte0_stream;
          interleaved_3byte.val[1] = byte1_stream;
          interleaved_3byte.val[2] = byte2_stream;

          vst3q_u8((uint8_t *)pos, interleaved_3byte);
          pos += 48;
        }

      } else {
        // MIXED LENGTH PATH: SIMD shuffle mask optimization
        // Use vqtbl4q_u8 to gather UTF-8 bytes in 4 passes, then compact with fast scalar

        // Gather all UTF-8 bytes using existing lookup tables with shuffle masks
        uint8x16_t byte0_vec = vqtbl4q_u8(char_byte0_lut, char_indices);
        uint8x16_t byte1_vec = vqtbl4q_u8(char_byte1_lut, char_indices);
        uint8x16_t byte2_vec = vqtbl4q_u8(char_byte2_lut, char_indices);
        uint8x16_t byte3_vec = vqtbl4q_u8(char_byte3_lut, char_indices);

        // Store gathered bytes to temporary buffers
        uint8_t byte0_buf[16], byte1_buf[16], byte2_buf[16], byte3_buf[16];
        vst1q_u8(byte0_buf, byte0_vec);
        vst1q_u8(byte1_buf, byte1_vec);
        vst1q_u8(byte2_buf, byte2_vec);
        vst1q_u8(byte3_buf, byte3_vec);

        // Fast scalar compaction: emit only valid bytes based on character lengths
        // Store char_indices to buffer for lookup
        uint8_t char_idx_buf[16];
        vst1q_u8(char_idx_buf, char_indices);

        for (int i = 0; i < 16; i++) {
          const uint8_t char_idx = char_idx_buf[i];
          const uint8_t byte_len = utf8_cache->cache64[char_idx].byte_len;

          // Emit bytes based on character length (1-4 bytes)
          *pos++ = byte0_buf[i];
          if (byte_len > 1)
            *pos++ = byte1_buf[i];
          if (byte_len > 2)
            *pos++ = byte2_buf[i];
          if (byte_len > 3)
            *pos++ = byte3_buf[i];
        }
      }
    }

    // Handle remaining pixels with optimized scalar code using 64-entry cache
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const uint8_t luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const uint8_t luma_idx = luminance >> 2;                       // Map 0..255 to 0..63 (same as NEON)
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx]; // Direct cache64 access
      // Optimized: Use direct assignment for single-byte ASCII characters
      if (char_info->byte_len == 1) {
        *pos++ = char_info->utf8_bytes[0];
      } else {
        // Fallback to full memcpy for multi-byte UTF-8
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;
      }
    }

    // Add newline (except last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

#endif
