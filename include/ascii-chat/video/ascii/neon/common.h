/**
 * @file video/ascii/neon/common.h
 * @ingroup video
 * @brief ARM NEON-accelerated ASCII rendering utilities (declarations)
 *
 * Shared helper functions and utilities for NEON rendering.
 */

#pragma once

#if SIMD_SUPPORT_NEON

#include <stdint.h>
#include <stdbool.h>
#include <arm_neon.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/rgba/image.h>

// Build NEON lookup tables inline (faster than caching - 30ns rebuild vs 50ns lookup)
static inline void build_neon_lookup_tables(utf8_palette_cache_t *utf8_cache, uint8x16x4_t *tbl, uint8x16x4_t *char_lut,
                                            uint8x16x4_t *length_lut, uint8x16x4_t *char_byte0_lut,
                                            uint8x16x4_t *char_byte1_lut, uint8x16x4_t *char_byte2_lut,
                                            uint8x16x4_t *char_byte3_lut);

// NEON-optimized RLE detection: find run length for char+color pairs
static inline int find_rle_run_length_neon(const uint8_t *char_buf, const uint8_t *color_buf, int start_pos,
                                           int max_len, uint8_t target_char, uint8_t target_color);

// NEON-optimized truecolor sequence assembly
static inline size_t neon_assemble_truecolor_sequences_true_simd(const uint8_t *char_indices, const uint8_t *r_vals,
                                                                 const uint8_t *g_vals, const uint8_t *b_vals,
                                                                 utf8_palette_cache_t *utf8_cache, outbuf_t *ob,
                                                                 int num_chars);

// Check if all character lengths in NEON register are the same
static inline bool all_same_length_neon(const uint8_t *lengths, uint8_t *uniform_length);

// Helper: NEON dithering
static inline uint8x16_t apply_ordered_dither(uint8x16_t vals, int x, int y);

// Helper: NEON luma calculation
static inline uint8x16_t simd_luma_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b);

// Helper: Convert u8 to Q6 fixed point
static inline uint8x16_t q6_from_u8(uint8x16_t u8_vals);

#endif // SIMD_SUPPORT_NEON
