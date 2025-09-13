#pragma once

#ifdef _WIN32

#include <stdint.h>
#include "../image.h"

// Main optimized YUY2 to RGB conversion function
// Automatically selects best SIMD implementation based on CPU capabilities
// Falls back to scalar code on older CPUs
//
// Parameters:
//   yuy2 - Input YUY2 buffer (2 bytes per pixel, Y0 U Y1 V format)
//   rgb  - Output RGB buffer (must be pre-allocated)
//   width - Image width in pixels
//   height - Image height in pixels
//
// The function handles:
// - SSE2 (8 pixels/iteration) on most x86-64 CPUs
// - SSSE3 (16 pixels/iteration with better shuffles) on newer CPUs
// - AVX2 (16 pixels/iteration with 256-bit vectors) on modern CPUs
// - Scalar fallback for non-SIMD CPUs or remainder pixels
void convert_yuy2_to_rgb_optimized(const uint8_t *yuy2, rgb_t *rgb, int width, int height);

// Individual implementations (for testing/benchmarking)
void convert_yuy2_to_rgb_scalar(const uint8_t *yuy2, rgb_t *rgb, int width, int height);

#ifdef SIMD_SUPPORT_SSE2
void convert_yuy2_to_rgb_sse2(const uint8_t *yuy2, rgb_t *rgb, int width, int height);
#endif

#ifdef SIMD_SUPPORT_SSSE3
void convert_yuy2_to_rgb_ssse3(const uint8_t *yuy2, rgb_t *rgb, int width, int height);
#endif

#ifdef SIMD_SUPPORT_AVX2
void convert_yuy2_to_rgb_avx2(const uint8_t *yuy2, rgb_t *rgb, int width, int height);
#endif

// CPU feature detection (exposed for testing)
int yuy2_cpu_has_sse2(void);
int yuy2_cpu_has_ssse3(void);
int yuy2_cpu_has_avx2(void);

#endif // _WIN32