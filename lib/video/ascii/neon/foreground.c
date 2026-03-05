/**
 * @file video/ascii/neon.c
 * @ingroup video
 * @brief ⚡ ARM NEON-accelerated ASCII rendering with 128-bit vector operations for ARM64
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
#include <ascii-chat/video/ascii/neon/foreground.h>
#include <ascii-chat/video/ascii/ascii_simd.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/ansi_fast.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>

// NEON table cache removed - performance analysis showed rebuilding (30ns) is faster than lookup (50ns)
// Tables are now built inline when needed for optimal performance

// Build NEON lookup tables inline (faster than caching - 30ns rebuild vs 50ns lookup)
static inline void build_neon_lookup_tables(utf8_palette_cache_t *utf8_cache, uint8x16x4_t *tbl, uint8x16x4_t *char_lut,
                                            uint8x16x4_t *length_lut, uint8x16x4_t *char_byte0_lut,
                                            uint8x16x4_t *char_byte1_lut, uint8x16x4_t *char_byte2_lut,
                                            uint8x16x4_t *char_byte3_lut) {
  // Build NEON-specific lookup table with cache64 indices (direct mapping)
  uint8_t cache64_indices[64];
  for (int i = 0; i < 64; i++) {
    cache64_indices[i] = (uint8_t)i; // Direct mapping: luminance bucket -> cache64 index
  }

  tbl->val[0] = vld1q_u8(&cache64_indices[0]);
  tbl->val[1] = vld1q_u8(&cache64_indices[16]);
  tbl->val[2] = vld1q_u8(&cache64_indices[32]);
  tbl->val[3] = vld1q_u8(&cache64_indices[48]);

  // Build vectorized UTF-8 lookup tables for length-aware compaction
  uint8_t ascii_chars_lut[64]; // For ASCII fast path
  uint8_t char_lengths[64];    // Character byte lengths
  uint8_t char_byte0[64];      // First byte of each character
  uint8_t char_byte1[64];      // Second byte of each character
  uint8_t char_byte2[64];      // Third byte of each character
  uint8_t char_byte3[64];      // Fourth byte of each character

  for (int i = 0; i < 64; i++) {
    const utf8_char_t *char_info = &utf8_cache->cache64[i];

    // ASCII fast path table
    ascii_chars_lut[i] = char_info->utf8_bytes[0];

    // Length-aware compaction tables
    char_lengths[i] = char_info->byte_len;
    char_byte0[i] = char_info->utf8_bytes[0];
    char_byte1[i] = char_info->byte_len > 1 ? char_info->utf8_bytes[1] : 0;
    char_byte2[i] = char_info->byte_len > 2 ? char_info->utf8_bytes[2] : 0;
    char_byte3[i] = char_info->byte_len > 3 ? char_info->utf8_bytes[3] : 0;
  }

  // Load all lookup tables into NEON registers
  char_lut->val[0] = vld1q_u8(&ascii_chars_lut[0]);
  char_lut->val[1] = vld1q_u8(&ascii_chars_lut[16]);
  char_lut->val[2] = vld1q_u8(&ascii_chars_lut[32]);
  char_lut->val[3] = vld1q_u8(&ascii_chars_lut[48]);

  length_lut->val[0] = vld1q_u8(&char_lengths[0]);
  length_lut->val[1] = vld1q_u8(&char_lengths[16]);
  length_lut->val[2] = vld1q_u8(&char_lengths[32]);
  length_lut->val[3] = vld1q_u8(&char_lengths[48]);

  char_byte0_lut->val[0] = vld1q_u8(&char_byte0[0]);
  char_byte0_lut->val[1] = vld1q_u8(&char_byte0[16]);
  char_byte0_lut->val[2] = vld1q_u8(&char_byte0[32]);
  char_byte0_lut->val[3] = vld1q_u8(&char_byte0[48]);

  char_byte1_lut->val[0] = vld1q_u8(&char_byte1[0]);
  char_byte1_lut->val[1] = vld1q_u8(&char_byte1[16]);
  char_byte1_lut->val[2] = vld1q_u8(&char_byte1[32]);
  char_byte1_lut->val[3] = vld1q_u8(&char_byte1[48]);

  char_byte2_lut->val[0] = vld1q_u8(&char_byte2[0]);
  char_byte2_lut->val[1] = vld1q_u8(&char_byte2[16]);
  char_byte2_lut->val[2] = vld1q_u8(&char_byte2[32]);
  char_byte2_lut->val[3] = vld1q_u8(&char_byte2[48]);

  char_byte3_lut->val[0] = vld1q_u8(&char_byte3[0]);
  char_byte3_lut->val[1] = vld1q_u8(&char_byte3[16]);
  char_byte3_lut->val[2] = vld1q_u8(&char_byte3[32]);
  char_byte3_lut->val[3] = vld1q_u8(&char_byte3[48]);
}

// NEON-optimized RLE detection: find run length for char+color pairs
static inline int find_rle_run_length_neon(const uint8_t *char_buf, const uint8_t *color_buf, int start_pos,
                                           int max_len, uint8_t target_char, uint8_t target_color) {
  int run_length = 1; // At least the starting position

  // Use NEON to check multiple elements at once when possible
  int remaining = max_len - start_pos - 1;
  if (remaining <= 0)
    return 1;

  const uint8_t *char_ptr = &char_buf[start_pos + 1];
  const uint8_t *color_ptr = &color_buf[start_pos + 1];

  // Process in chunks of 16 for full NEON utilization
  while (remaining >= 16) {
    uint8x16_t chars = vld1q_u8(char_ptr);
    uint8x16_t colors = vld1q_u8(color_ptr);

    uint8x16_t char_match = vceqq_u8(chars, vdupq_n_u8(target_char));
    uint8x16_t color_match = vceqq_u8(colors, vdupq_n_u8(target_color));
    uint8x16_t both_match = vandq_u8(char_match, color_match);

    // Use NEON min/max to find first mismatch efficiently
    // If all elements match, min will be 0xFF, otherwise it will be 0x00
    uint8_t min_match = vminvq_u8(both_match);

    if (min_match == 0xFF) {
      // All 16 elements match
      run_length += 16;
      char_ptr += 16;
      color_ptr += 16;
      remaining -= 16;
    } else {
      // Find first mismatch position using bit scan
      uint64_t mask_lo = vgetq_lane_u64(vreinterpretq_u64_u8(both_match), 0);
      uint64_t mask_hi = vgetq_lane_u64(vreinterpretq_u64_u8(both_match), 1);

      int matches_found = 0;
      // Check low 8 bytes first
      for (int i = 0; i < 8; i++) {
        if ((mask_lo >> (i * 8)) & 0xFF) {
          matches_found++;
        } else {
          break;
        }
      }

      // If all low 8 matched, check high 8 bytes
      if (matches_found == 8) {
        for (int i = 0; i < 8; i++) {
          if ((mask_hi >> (i * 8)) & 0xFF) {
            matches_found++;
          } else {
            break;
          }
        }
      }

      run_length += matches_found;
      break; // Found mismatch, stop
    }
  }

  // Handle remaining elements with scalar loop
  while (remaining > 0 && *char_ptr == target_char && *color_ptr == target_color) {
    run_length++;
    char_ptr++;
    color_ptr++;
    remaining--;
  }

  return run_length;
}

// NEON helper: Check if all characters have same length
static inline bool all_same_length_neon(uint8x16_t lengths, uint8_t *out_length) {
  uint8_t first_len = vgetq_lane_u8(lengths, 0);
  uint8x16_t first_len_vec = vdupq_n_u8(first_len);
  uint8x16_t all_same = vceqq_u8(lengths, first_len_vec);

  uint64x2_t all_same_64 = vreinterpretq_u64_u8(all_same);
  uint64_t combined = vgetq_lane_u64(all_same_64, 0) & vgetq_lane_u64(all_same_64, 1);

  if (combined == 0xFFFFFFFFFFFFFFFF) {
    *out_length = first_len;
    return true;
  }
  return false;
}

// ============================================================================
// Vectorized Decimal Lookup Functions for NEON Color Performance
// ============================================================================

// NEON TBL lookup tables for decimal conversion (256 entries each)
// Format: each entry has length byte + up to 3 decimal chars (4 bytes per entry)
static uint8_t neon_decimal_table_data[256 * 4]; // 1024 bytes: [len][d1][d2][d3] per entry
// Lifecycle for thread-safe one-time initialization (replaces C11 call_once)
static lifecycle_t g_neon_table_lc = LIFECYCLE_INIT;

// Private initialization function (called exactly once via lifecycle)
static void do_init_neon_decimal_table(void) {
  // Initialize g_dec3_cache first
  if (!g_dec3_cache.dec3_initialized) {
    init_dec3();
  }

  // Convert dec3_t cache to NEON TBL format: [len][d1][d2][d3] per 4-byte entry
  for (int i = 0; i < 256; i++) {
    const dec3_t *dec = &g_dec3_cache.dec3_table[i];
    uint8_t *entry = &neon_decimal_table_data[i * 4];
    entry[0] = dec->len;                          // Length (1-3)
    entry[1] = (dec->len >= 1) ? dec->s[0] : '0'; // First digit
    entry[2] = (dec->len >= 2) ? dec->s[1] : '0'; // Second digit
    entry[3] = (dec->len >= 3) ? dec->s[2] : '0'; // Third digit
  }
}

// Initialize NEON TBL decimal lookup table (called once at startup)
// Thread-safe with lifecycle API ensuring exactly-once execution
void init_neon_decimal_table(void) {
  if (!lifecycle_init(&g_neon_table_lc, "neon_decimal")) {
    return; // Already initialized
  }
  do_init_neon_decimal_table();
}

// TODO: Implement true NEON vectorized ANSI sequence generation using TBL + compaction
// Following the monochrome pattern: pad sequences to uniform width, then compact null bytes
// For now, keep the existing scalar approach to avoid breaking the build

// True NEON vectorized ANSI truecolor sequence assembly - no scalar loops!
static inline size_t neon_assemble_truecolor_sequences_true_simd(uint8x16_t char_indices, uint8x16_t r_vals,
                                                                 uint8x16_t g_vals, uint8x16_t b_vals,
                                                                 utf8_palette_cache_t *utf8_cache, char *output_buffer,
                                                                 size_t buffer_capacity, bool use_background) {
  // STREAMLINED IMPLEMENTATION: Focus on the real bottleneck - RGB->decimal conversion
  // Key insight: ANSI sequences are too variable for effective SIMD, but TBL lookups provide major speedup

  // NOTE: Ensure NEON decimal table is initialized BEFORE calling this function (done in
  // render_ascii_neon_unified_optimized)

  char *dst = output_buffer;

  // Extract values for optimized scalar processing with SIMD-accelerated lookups
  uint8_t char_idx_buf[16], r_buf[16], g_buf[16], b_buf[16];
  vst1q_u8(char_idx_buf, char_indices);
  vst1q_u8(r_buf, r_vals);
  vst1q_u8(g_buf, g_vals);
  vst1q_u8(b_buf, b_vals);

  size_t total_written = 0;
  const char *prefix = use_background ? "\033[48;2;" : "\033[38;2;";
  const size_t prefix_len = 7;

  // Optimized scalar loop with NEON TBL acceleration for RGB->decimal conversion
  // This eliminates the expensive safe_snprintf() calls which were the real bottleneck
  for (int i = 0; i < 16; i++) {
    // Use NEON TBL lookups for RGB decimal conversion (major speedup!)
    const uint8_t *r_entry = &neon_decimal_table_data[r_buf[i] * 4];
    const uint8_t *g_entry = &neon_decimal_table_data[g_buf[i] * 4];
    const uint8_t *b_entry = &neon_decimal_table_data[b_buf[i] * 4];

    const uint8_t char_idx = char_idx_buf[i];
    const utf8_char_t *char_info = &utf8_cache->cache64[char_idx];

    // Calculate total sequence length for buffer safety
    size_t seq_len = prefix_len + r_entry[0] + 1 + g_entry[0] + 1 + b_entry[0] + 1 + char_info->byte_len;
    if (total_written >= buffer_capacity - seq_len) {
      break; // Buffer safety
    }

    // Optimized assembly using TBL results (no divisions, no snprintf!)
    memcpy(dst, prefix, prefix_len);
    dst += prefix_len;

    // RGB components using pre-computed decimal strings
    memcpy(dst, &r_entry[1], r_entry[0]);
    dst += r_entry[0];
    *dst++ = ';';

    memcpy(dst, &g_entry[1], g_entry[0]);
    dst += g_entry[0];
    *dst++ = ';';

    memcpy(dst, &b_entry[1], b_entry[0]);
    dst += b_entry[0];
    *dst++ = 'm';

    // UTF-8 character from cache
    memcpy(dst, char_info->utf8_bytes, char_info->byte_len);
    dst += char_info->byte_len;

    total_written = dst - output_buffer;
  }

  return total_written;
}

// Min-heap management removed - no longer needed without NEON table cache

// Eviction logic removed - no longer needed without NEON table cache

// Continue to actual NEON functions (helper functions already defined above)

// Definitions are in ascii_simd.h - just use them
// REMOVED: #define luminance_palette g_ascii_cache.luminance_palette (causes macro expansion issues)

// SIMD luma and helpers:

// SIMD luminance: Y = (77R + 150G + 29B) >> 8
static inline uint8x16_t simd_luma_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b) {
  uint16x8_t rl = vmovl_u8(vget_low_u8(r));
  uint16x8_t rh = vmovl_u8(vget_high_u8(r));
  uint16x8_t gl = vmovl_u8(vget_low_u8(g));
  uint16x8_t gh = vmovl_u8(vget_high_u8(g));
  uint16x8_t bl = vmovl_u8(vget_low_u8(b));
  uint16x8_t bh = vmovl_u8(vget_high_u8(b));

  uint32x4_t l0 = vmull_n_u16(vget_low_u16(rl), LUMA_RED);
  uint32x4_t l1 = vmull_n_u16(vget_high_u16(rl), LUMA_RED);
  l0 = vmlal_n_u16(l0, vget_low_u16(gl), LUMA_GREEN);
  l1 = vmlal_n_u16(l1, vget_high_u16(gl), LUMA_GREEN);
  l0 = vmlal_n_u16(l0, vget_low_u16(bl), LUMA_BLUE);
  l1 = vmlal_n_u16(l1, vget_high_u16(bl), LUMA_BLUE);

  uint32x4_t h0 = vmull_n_u16(vget_low_u16(rh), LUMA_RED);
  uint32x4_t h1 = vmull_n_u16(vget_high_u16(rh), LUMA_RED);
  h0 = vmlal_n_u16(h0, vget_low_u16(gh), LUMA_GREEN);
  h1 = vmlal_n_u16(h1, vget_high_u16(gh), LUMA_GREEN);
  h0 = vmlal_n_u16(h0, vget_low_u16(bh), LUMA_BLUE);
  h1 = vmlal_n_u16(h1, vget_high_u16(bh), LUMA_BLUE);

  uint16x8_t l = vcombine_u16(vrshrn_n_u32(l0, 8), vrshrn_n_u32(l1, 8));
  uint16x8_t h = vcombine_u16(vrshrn_n_u32(h0, 8), vrshrn_n_u32(h1, 8));
  return vcombine_u8(vqmovn_u16(l), vqmovn_u16(h));
}

// ===== SIMD helpers for 256-color quantization =====

// Approximate quantize 0..255 -> 0..5 : q ≈ round(x*5/255) = (x*5 + 128)>>8
static inline uint8x16_t q6_from_u8(uint8x16_t x) {
  uint16x8_t xl = vmovl_u8(vget_low_u8(x));
  uint16x8_t xh = vmovl_u8(vget_high_u8(x));
  xl = vmlaq_n_u16(vdupq_n_u16(0), xl, 5);
  xh = vmlaq_n_u16(vdupq_n_u16(0), xh, 5);
  xl = vaddq_u16(xl, vdupq_n_u16(128));
  xh = vaddq_u16(xh, vdupq_n_u16(128));
  xl = vshrq_n_u16(xl, 8);
  xh = vshrq_n_u16(xh, 8);
  return vcombine_u8(vqmovn_u16(xl), vqmovn_u16(xh)); // 0..5
}

// Make 256-color index (cube vs gray). threshold: max-min < thr ⇒ gray
#ifndef CUBE_GRAY_THRESHOLD
#define CUBE_GRAY_THRESHOLD 10
#endif

// Apply ordered dithering to reduce color variations (creates longer runs)
static inline uint8x16_t apply_ordered_dither(uint8x16_t color, int pixel_offset, uint8_t dither_strength) {
  // Bayer 4x4 dithering matrix (classic ordered dithering pattern)
  static const uint8_t bayer4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

  // Load dithering matrix into NEON register
  const uint8x16_t dither_matrix = vld1q_u8(bayer4x4);

  // Create pixel position indices for 16 consecutive pixels
  uint8_t pos_indices[16];
  for (int i = 0; i < 16; i++) {
    pos_indices[i] = (pixel_offset + i) & 15; // Wrap to 4x4 matrix (0-15)
  }
  const uint8x16_t position_vec = vld1q_u8(pos_indices);

  // Lookup dither values for each pixel position using table lookup
  uint8x16_t dither_values = vqtbl1q_u8(dither_matrix, position_vec);

  // Scale dither values by strength (0-255 range)
  // dither_strength controls how much dithering to apply
  uint16x8_t dither_lo = vmulq_n_u16(vmovl_u8(vget_low_u8(dither_values)), dither_strength);
  uint16x8_t dither_hi = vmulq_n_u16(vmovl_u8(vget_high_u8(dither_values)), dither_strength);
  dither_lo = vshrq_n_u16(dither_lo, 4); // Scale down (/16)
  dither_hi = vshrq_n_u16(dither_hi, 4);
  uint8x16_t scaled_dither = vcombine_u8(vqmovn_u16(dither_lo), vqmovn_u16(dither_hi));

  // Apply dithering with saturation to prevent overflow
  return vqaddq_u8(color, scaled_dither);
}

uint8x16_t palette256_index_dithered_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset) {
  // Dithering disabled in speed mode (no-op)
  r = apply_ordered_dither(r, pixel_offset, 0);
  g = apply_ordered_dither(g, pixel_offset + 1, 0);
  b = apply_ordered_dither(b, pixel_offset + 2, 0);

  // cube index
  uint8x16_t R6 = q6_from_u8(r);
  uint8x16_t G6 = q6_from_u8(g);
  uint8x16_t B6 = q6_from_u8(b);

  // idx_cube = 16 + R6*36 + G6*6 + B6  (do in 16-bit to avoid overflow)
  uint16x8_t R6l = vmovl_u8(vget_low_u8(R6));
  uint16x8_t R6h = vmovl_u8(vget_high_u8(R6));
  uint16x8_t G6l = vmovl_u8(vget_low_u8(G6));
  uint16x8_t G6h = vmovl_u8(vget_high_u8(G6));
  uint16x8_t B6l = vmovl_u8(vget_low_u8(B6));
  uint16x8_t B6h = vmovl_u8(vget_high_u8(B6));

  uint16x8_t idxl = vmlaq_n_u16(vmulq_n_u16(R6l, 36), G6l, 6);
  uint16x8_t idxh = vmlaq_n_u16(vmulq_n_u16(R6h, 36), G6h, 6);
  idxl = vaddq_u16(idxl, B6l);
  idxh = vaddq_u16(idxh, B6h);
  idxl = vaddq_u16(idxl, vdupq_n_u16(16));
  idxh = vaddq_u16(idxh, vdupq_n_u16(16));

  // gray decision: max-min < thr ?
  uint8x16_t maxrg = vmaxq_u8(r, g);
  uint8x16_t minrg = vminq_u8(r, g);
  uint8x16_t maxrgb = vmaxq_u8(maxrg, b);
  uint8x16_t minrgb = vminq_u8(minrg, b);
  uint8x16_t diff = vsubq_u8(maxrgb, minrgb);
  uint8x16_t thr = vdupq_n_u8((uint8_t)CUBE_GRAY_THRESHOLD);
  uint8x16_t is_gray = vcltq_u8(diff, thr);

  // gray idx = 232 + round(Y*23/255)
  uint8x16_t Y = simd_luma_neon(r, g, b);
  // q23 ≈ round(Y*23/255) = (Y*23 + 128)>>8
  uint16x8_t Yl = vmovl_u8(vget_low_u8(Y));
  uint16x8_t Yh = vmovl_u8(vget_high_u8(Y));
  Yl = vmlaq_n_u16(vdupq_n_u16(0), Yl, 23);
  Yh = vmlaq_n_u16(vdupq_n_u16(0), Yh, 23);
  Yl = vaddq_u16(Yl, vdupq_n_u16(128));
  Yh = vaddq_u16(Yh, vdupq_n_u16(128));
  Yl = vshrq_n_u16(Yl, 8);
  Yh = vshrq_n_u16(Yh, 8);
  uint16x8_t gidxl = vaddq_u16(Yl, vdupq_n_u16(232));
  uint16x8_t gidxh = vaddq_u16(Yh, vdupq_n_u16(232));

  // select gray or cube per lane
  uint8x16_t idx_cube = vcombine_u8(vqmovn_u16(idxl), vqmovn_u16(idxh));
  uint8x16_t idx_gray = vcombine_u8(vqmovn_u16(gidxl), vqmovn_u16(gidxh));
  return vbslq_u8(is_gray, idx_gray, idx_cube);
}

/**
 * @brief Flip image horizontally using NEON acceleration
 *
 * Reverses each row of the image using NEON v128 registers for fast
 * parallel byte swapping. Processes 16 bytes at a time.
 */
void image_flip_horizontal_neon(image_t *image) {
  if (!image || !image->pixels || image->w < 2) {
    return;
  }

  // Process each row - swap pixels from both ends using NEON for faster loads/stores
  for (int y = 0; y < image->h; y++) {
    rgb_pixel_t *row = &image->pixels[y * image->w];
    int width = image->w;

    // NEON-accelerated swapping: process 4 pixels at a time using uint32 loads
    // Each RGB pixel is 3 bytes, so 4 pixels = 12 bytes that can be loaded as 3x u32
    int left_pix = 0;
    int right_pix = width - 1;

    // Fast path: swap 4-pixel groups using uint32 operations
    while (left_pix + 3 < right_pix - 3) {
      // Load left 4 pixels (12 bytes) as 3 uint32 values using NEON
      uint32_t *left_ptr = (uint32_t *)&row[left_pix];
      uint32_t *right_ptr = (uint32_t *)&row[right_pix - 3];

      uint32x2_t left_0 = vld1_u32(left_ptr); // first 8 bytes
      uint32_t left_1 = left_ptr[2];          // last 4 bytes

      uint32x2_t right_0 = vld1_u32(right_ptr); // first 8 bytes
      uint32_t right_1 = right_ptr[2];          // last 4 bytes

      // Store swapped using NEON
      vst1_u32(right_ptr, left_0);
      right_ptr[2] = left_1;
      vst1_u32(left_ptr, right_0);
      left_ptr[2] = right_1;

      left_pix += 4;
      right_pix -= 4;
    }

    // Scalar cleanup for remaining pixels
    while (left_pix < right_pix) {
      rgb_pixel_t temp = row[left_pix];
      row[left_pix] = row[right_pix];
      row[right_pix] = temp;
      left_pix++;
      right_pix--;
    }
  }
}

#endif // SIMD_SUPPORT_NEON
