#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "image.h"

// Forward declarations for architecture-specific implementations
typedef rgb_t rgb_pixel_t;

// Common SIMD function signatures
void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count);
void convert_pixels_ssse3(const rgb_pixel_t *pixels, char *ascii_chars, int count);
void convert_pixels_avx2(const rgb_pixel_t *pixels, char *ascii_chars, int count);
void convert_pixels_neon(const rgb_pixel_t *pixels, char *ascii_chars, int count);
void convert_pixels_sve(const rgb_pixel_t *pixels, char *ascii_chars, int count);

// Color conversion functions
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
size_t convert_row_with_color_ssse3(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode);
size_t convert_row_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
size_t convert_row_with_color_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                  bool background_mode);

// Optimized row processing with buffer pools
size_t convert_row_with_color_sse2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars);
size_t convert_row_with_color_ssse3_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                int width, bool background_mode, char *ascii_chars);
size_t convert_row_with_color_avx2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars);

// Luminance constants used across all implementations
#define LUMA_RED 77    // 0.299 * 256
#define LUMA_GREEN 150 // 0.587 * 256
#define LUMA_BLUE 29   // 0.114 * 256

// Architecture-specific capability detection
bool has_sse2_support(void);
bool has_ssse3_support(void);
bool has_avx2_support(void);
bool has_neon_support(void);
bool has_sve_support(void);

// ANSI color sequence generation functions (defined in ascii_simd_color.c)
char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb);
char *append_sgr_reset(char *dst);