/**
 * @file video/ascii/avx2/common.h
 * @brief Shared AVX2 helper functions
 */

#ifndef AVX2_COMMON_H
#define AVX2_COMMON_H

#include <stdint.h>
#include <ascii-chat/video/ascii/common.h>

#if SIMD_SUPPORT_AVX2

// Thread-local storage for AVX2 working buffers
extern THREAD_LOCAL ALIGNED_32 uint8_t avx2_r_buffer[32];
extern THREAD_LOCAL ALIGNED_32 uint8_t avx2_g_buffer[32];
extern THREAD_LOCAL ALIGNED_32 uint8_t avx2_b_buffer[32];
extern THREAD_LOCAL ALIGNED_32 uint8_t avx2_luminance_buffer[32];

// Helper functions
char *emit_rle_count(char *pos, uint32_t rep_count);
void avx2_load_rgb32_optimized(const rgb_pixel_t *__restrict pixels, uint8_t *__restrict r_out,
                               uint8_t *__restrict g_out, uint8_t *__restrict b_out);
void avx2_compute_luminance_32(const uint8_t *r_vals, const uint8_t *g_vals, const uint8_t *b_vals,
                               uint8_t *luminance_out);

#endif // SIMD_SUPPORT_AVX2
#endif // AVX2_COMMON_H
