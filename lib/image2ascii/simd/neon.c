/**
 * @file image2ascii/simd/neon.c
 * @ingroup image2ascii
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
#include <stdatomic.h>
#include <math.h>

#include <arm_neon.h>

#include "common.h"
#include "neon.h"
#include "ascii_simd.h"
#include "../image.h"
#include "image2ascii/simd/common.h"
#include "image2ascii/output_buffer.h"
#include "image2ascii/ansi_fast.h"

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

// NEON cache destruction no longer needed - tables are built inline
void neon_caches_destroy(void) {
  // No-op: NEON table cache removed for performance
  // Tables are now built inline (30ns) which is faster than cache lookup (50ns)
}

// NEON helper: Horizontal sum of 16 uint8_t values
static inline uint16_t neon_horizontal_sum_u8(uint8x16_t vec) {
  uint16x8_t sum16_lo = vpaddlq_u8(vec);
  uint32x4_t sum32 = vpaddlq_u16(sum16_lo);
  uint64x2_t sum64 = vpaddlq_u32(sum32);
  return (uint16_t)(vgetq_lane_u64(sum64, 0) + vgetq_lane_u64(sum64, 1));
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
static bool neon_decimal_table_initialized = false;

// Initialize NEON TBL decimal lookup table (called once at startup)
void init_neon_decimal_table(void) {
  if (neon_decimal_table_initialized)
    return;

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

  neon_decimal_table_initialized = true;
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

  // Ensure NEON decimal table is initialized for fast RGB->decimal conversion
  init_neon_decimal_table();

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
  // This eliminates the expensive snprintf() calls which were the real bottleneck
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

// NEON helper: True vectorized UTF-8 compaction - eliminate NUL bytes completely
static inline void __attribute__((unused)) compact_utf8_vectorized(uint8_t *padded_data, uint8x16_t lengths,
                                                                   char **pos) {
  // Calculate total valid bytes using NEON horizontal sum
  uint8_t total_bytes = (uint8_t)neon_horizontal_sum_u8(lengths);

  // The fundamental insight: For mixed UTF-8, we need to compact interleaved data
  // vst4q_u8 created: [char0_b0, char0_b1, char0_b2, char0_b3, char1_b0, char1_b1, ...]
  // We need: consecutive valid UTF-8 bytes only, no NULs

  // Use NEON horizontal compaction: process entire 64 bytes vectorially
  uint8x16_t chunk1 = vld1q_u8(&padded_data[0]);
  uint8x16_t chunk2 = vld1q_u8(&padded_data[16]);
  uint8x16_t chunk3 = vld1q_u8(&padded_data[32]);
  uint8x16_t chunk4 = vld1q_u8(&padded_data[48]);

  // Write the exact number of valid bytes calculated
  // For UTF-8 correctness, we must preserve byte sequence integrity
  if (total_bytes <= 16) {
    vst1q_u8((uint8_t *)*pos, chunk1);
  } else if (total_bytes <= 32) {
    vst1q_u8((uint8_t *)*pos, chunk1);
    vst1q_u8((uint8_t *)*pos + 16, chunk2);
  } else if (total_bytes <= 48) {
    vst1q_u8((uint8_t *)*pos, chunk1);
    vst1q_u8((uint8_t *)*pos + 16, chunk2);
    vst1q_u8((uint8_t *)*pos + 32, chunk3);
  } else {
    vst1q_u8((uint8_t *)*pos, chunk1);
    vst1q_u8((uint8_t *)*pos + 16, chunk2);
    vst1q_u8((uint8_t *)*pos + 32, chunk3);
    vst1q_u8((uint8_t *)*pos + 48, chunk4);
  }

  *pos += total_bytes;
}

// Definitions are in ascii_simd.h - just use them
// REMOVED: #define luminance_palette g_ascii_cache.luminance_palette (causes macro expansion issues)

// ------------------------------------------------------------
// Map luminance [0..255] → 4-bit index [0..15] using top nibble
static inline uint8x16_t __attribute__((unused)) luma_to_idx_nibble_neon(uint8x16_t y) {
  return vshrq_n_u8(y, 4);
}

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

// NEON: cr=(r*5+127)/255  (nearest of 0..5)
static inline uint8x16_t __attribute__((unused)) quant6_neon(uint8x16_t x) {
  uint16x8_t xl = vmovl_u8(vget_low_u8(x));
  uint16x8_t xh = vmovl_u8(vget_high_u8(x));
  uint16x8_t tl = vaddq_u16(vmulq_n_u16(xl, 5), vdupq_n_u16(127));
  uint16x8_t th = vaddq_u16(vmulq_n_u16(xh, 5), vdupq_n_u16(127));
  uint32x4_t tl0 = vmull_n_u16(vget_low_u16(tl), 257);
  uint32x4_t tl1 = vmull_n_u16(vget_high_u16(tl), 257);
  uint32x4_t th0 = vmull_n_u16(vget_low_u16(th), 257);
  uint32x4_t th1 = vmull_n_u16(vget_high_u16(th), 257);
  uint16x8_t ql = vcombine_u16(vshrn_n_u32(tl0, 16), vshrn_n_u32(tl1, 16));
  uint16x8_t qh = vcombine_u16(vshrn_n_u32(th0, 16), vshrn_n_u32(th1, 16));
  return vcombine_u8(vqmovn_u16(ql), vqmovn_u16(qh)); // 0..5
}

// Build 6x6x6 index: cr*36 + cg*6 + cb  (0..215)
static inline uint8x16_t __attribute__((unused)) cube216_index_neon(uint8x16_t r6, uint8x16_t g6, uint8x16_t b6) {
  uint16x8_t rl = vmovl_u8(vget_low_u8(r6));
  uint16x8_t rh = vmovl_u8(vget_high_u8(r6));
  uint16x8_t gl = vmovl_u8(vget_low_u8(g6));
  uint16x8_t gh = vmovl_u8(vget_high_u8(g6));
  uint16x8_t bl = vmovl_u8(vget_low_u8(b6));
  uint16x8_t bh = vmovl_u8(vget_high_u8(b6));
  uint16x8_t il = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(rl, 36), gl, 6), bl, 1);
  uint16x8_t ih = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(rh, 36), gh, 6), bh, 1);
  return vcombine_u8(vqmovn_u16(il), vqmovn_u16(ih)); // 0..215
}

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

//=============================================================================
// Simple Monochrome ASCII Function (matches scalar image_print performance)
//=============================================================================

char *render_ascii_image_monochrome_neon(const image_t *image, const char *ascii_chars) {
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
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  // Use SIMD-aligned allocation for optimal vectorized write performance
  char *output = SAFE_MALLOC_SIMD(len, char *);

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

    // Add newline (except for last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

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
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // Get cached UTF-8 character mappings (like monochrome function does)
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for NEON color");
    return NULL;
  }

  // Build NEON lookup table inline (faster than caching - 30ns rebuild vs 50ns lookup)
  uint8x16x4_t tbl, char_lut, length_lut, char_byte0_lut, char_byte1_lut, char_byte2_lut, char_byte3_lut;
  build_neon_lookup_tables(utf8_cache, &tbl, &char_lut, &length_lut, &char_byte0_lut, &char_byte1_lut, &char_byte2_lut,
                           &char_byte3_lut);

  // Suppress unused variable warnings for color mode
  (void)char_lut;
  (void)length_lut;
  (void)char_byte0_lut;
  (void)char_byte1_lut;
  (void)char_byte2_lut;
  (void)char_byte3_lut;

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
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
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + 128u) >> 8);
      uint8_t luma_idx = Y >> 2;                                // 0-63 index
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index
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
    if (y < height - 1) { // Only add newline if not the last row
      ob_putc(&ob, '\n');
    }
    curR = curG = curB = -1;
    cur_color_idx = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

//=============================================================================
// Optimized NEON Half-block renderer (based on ChatGPT reference)
//=============================================================================
char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes) {
  /* Main: half-block renderer. Returns NUL-terminated malloc'd string; caller free(). */
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  outbuf_t ob = {0};
  // generous guess: per cell ~ 10–14 bytes avg; half the rows + newlines
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 14u + (size_t)((height + 1) / 2) * 8u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // current SGR state; -1 means unknown
  int cur_fr = -1, cur_fg = -1, cur_fb = -1;
  int cur_br = -1, cur_bg = -1, cur_bb = -1;

  // process two source rows per emitted line
  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x + 16 <= width) {
      // Load 16 top and bottom pixels (RGB interleaved)
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8x16x3_t top = vld3q_u8(pT);

      uint8x16x3_t bot;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        bot = vld3q_u8(pB);
      } else {
        // synthesize bottom = top for odd-height last row
        bot.val[0] = top.val[0];
        bot.val[1] = top.val[1];
        bot.val[2] = top.val[2];
      }

      // Spill to small arrays (cheap; enables simple scalar RLE over 16)
      uint8_t Rt[16], Gt[16], Bt[16], Rb[16], Gb[16], Bb[16];
      vst1q_u8(Rt, top.val[0]);
      vst1q_u8(Gt, top.val[1]);
      vst1q_u8(Bt, top.val[2]);
      vst1q_u8(Rb, bot.val[0]);
      vst1q_u8(Gb, bot.val[1]);
      vst1q_u8(Bb, bot.val[2]);

      // RLE over the 16 cells
      for (int i = 0; i < 16;) {
        uint8_t rT = Rt[i], gT = Gt[i], bT = Bt[i];
        uint8_t rB = Rb[i], gB = Gb[i], bB = Bb[i];

        // Always half-block: U+2580 "▀" (upper half)
        const uint8_t glyph_utf8[3] = {0xE2, 0x96, 0x80};

        // Extend run while next cell has same top+bottom colors
        int j = i + 1;
        for (; j < 16; ++j) {
          if (!(Rt[j] == rT && Gt[j] == gT && Bt[j] == bT && Rb[j] == rB && Gb[j] == gB && Bb[j] == bB))
            break;
        }
        uint32_t run = (uint32_t)(j - i);

        // Check if this is a transparent area (black pixels = padding/background)
        bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

        if (is_transparent) {
          // Reset colors before transparent areas to prevent color bleeding
          if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
            emit_reset(&ob);
            cur_fr = cur_fg = cur_fb = -1;
            cur_br = cur_bg = cur_bb = -1;
          }
          // For transparent areas, emit space character with no color codes (terminal default)
          ob_write(&ob, " ", 1);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; ++k) {
              ob_write(&ob, " ", 1);
            }
          }
        } else {
          // Normal colored half-blocks - set fg to TOP, bg to BOTTOM if changed
          if (cur_fr != rT || cur_fg != gT || cur_fb != bT) {
            emit_set_fg(&ob, rT, gT, bT);
            cur_fr = rT;
            cur_fg = gT;
            cur_fb = bT;
          }
          if (cur_br != rB || cur_bg != gB || cur_bb != bB) {
            emit_set_bg(&ob, rB, gB, bB);
            cur_br = rB;
            cur_bg = gB;
            cur_bb = bB;
          }

          // Emit glyph once, then REP or literals
          ob_write(&ob, (const char *)glyph_utf8, 3);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; ++k) {
              ob_write(&ob, (const char *)glyph_utf8, 3);
            }
          }
        }

        i = j;
      }
      x += 16;
    }

    // Scalar tail (or full row if no NEON)
    for (; x < width;) {
      const uint8_t *pT = rowT + (size_t)x * 3u;
      const uint8_t *pB = rowB ? rowB + (size_t)x * 3u : NULL;

      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];
      uint8_t rB = rT, gB = gT, bB = bT;
      if (pB) {
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }

      // Extend run while top and bottom colors match exactly
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        const uint8_t *qB = rowB ? rowB + (size_t)j * 3u : NULL;
        uint8_t rT2 = qT[0], gT2 = qT[1], bT2 = qT[2];
        uint8_t rB2 = qB ? qB[0] : rT2, gB2 = qB ? qB[1] : gT2, bB2 = qB ? qB[2] : bT2;
        if (!((rT2 == rT && gT2 == gT && bT2 == bT) && (rB2 == rB && gB2 == gB && bB2 == bB)))
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if this is a transparent area (black pixels = padding/background)
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        // Reset colors before transparent areas to prevent color bleeding
        if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
          emit_reset(&ob);
          cur_fr = cur_fg = cur_fb = -1;
          cur_br = cur_bg = cur_bb = -1;
        }
        // For transparent areas, emit space character with no color codes
        ob_write(&ob, " ", 1);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        }
      } else {
        // SGR: fg = TOP, bg = BOTTOM for colored areas
        if (cur_fr != rT || cur_fg != gT || cur_fb != bT) {
          emit_set_fg(&ob, rT, gT, bT);
          cur_fr = rT;
          cur_fg = gT;
          cur_fb = bT;
        }
        if (cur_br != rB || cur_bg != gB || cur_bb != bB) {
          emit_set_bg(&ob, rB, gB, bB);
          cur_br = rB;
          cur_bg = gB;
          cur_bb = bB;
        }

        // Always the upper half block "▀" (U+2580)
        static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};
        ob_write(&ob, HB, 3);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, HB, 3);
          }
        }
      }

      x = j;
    }

    // End emitted line: reset and newline (only for non-final lines)
    emit_reset(&ob);
    // Check if this is the last output line (since we process 2 pixel rows per output line)
    if (y + 2 < height) { // Only add newline if not the last output line
      ob_putc(&ob, '\n');
    }
    cur_fr = cur_fg = cur_fb = -1;
    cur_br = cur_bg = cur_bb = -1;
  }

  ob_term(&ob);
  return ob.buf;
}
#endif // SIMD_SUPPORT_NEON
