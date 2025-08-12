#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "image.h"

// Check for SIMD support
#ifdef __AVX2__
#define SIMD_SUPPORT_AVX2 1
#include <immintrin.h>
#endif

#ifdef __SSE2__
#define SIMD_SUPPORT_SSE2 1
#include <emmintrin.h>
#endif

#ifdef __SSSE3__
#define SIMD_SUPPORT_SSSE3 1
#include <tmmintrin.h>
#endif

#ifdef __ARM_NEON
#define SIMD_SUPPORT_NEON 1
#include <arm_neon.h>
#endif

// Use the project's existing rgb_t for consistency
#include "image.h"
typedef rgb_t rgb_pixel_t;

// SIMD-optimized functions
#ifdef SIMD_SUPPORT_AVX2
// Process 8 pixels at once with AVX2
void convert_pixels_avx2(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
#endif

#ifdef SIMD_SUPPORT_SSE2
// Process 4 pixels at once with SSE2
void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
#endif

#ifdef SIMD_SUPPORT_SSSE3
// Process 32 pixels at once with SSSE3
void convert_pixels_ssse3(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_ssse3(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode);
#endif

#ifdef SIMD_SUPPORT_NEON
// ARM NEON version for Apple Silicon
void convert_pixels_neon(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
#endif

// Fallback scalar version
void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count);

// Auto-dispatch function (chooses best available SIMD)
void convert_pixels_optimized(const rgb_pixel_t *pixels, char *ascii_chars, int count);

// Complete colored ASCII conversion with SIMD + optimized ANSI generation
size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                        bool background_mode);

size_t convert_row_with_color_scalar(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                     bool background_mode);

// Benchmark functions
typedef struct {
  double scalar_time;
  double sse2_time;
  double ssse3_time;
  double avx2_time;
  double neon_time;
  double speedup_best;
  const char *best_method;
} simd_benchmark_t;

simd_benchmark_t benchmark_simd_conversion(int width, int height, int iterations);
void print_simd_capabilities(void);

char *image_print_simd(image_t *image);
char *image_print_colored_simd(image_t *image);

// Upper half block renderer for 2x vertical density
size_t render_row_upper_half_block(const rgb_pixel_t *top_row, const rgb_pixel_t *bottom_row, int width, char *dst,
                                   size_t cap);

// Half-height image renderer using ▀ blocks
char *image_print_half_height_blocks(image_t *image);

// Quality vs speed control for 256-color mode (optimization #4)
void set_color_quality_mode(bool high_quality); // true = 24-bit truecolor, false = 256-color
