#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "image.h"

// Luminance calculation constants (matches ascii_simd.c)
#define LUMA_RED 77    // 0.299 * 256
#define LUMA_GREEN 150 // 0.587 * 256
#define LUMA_BLUE 29   // 0.114 * 256

// Forward declaration for cache structure
typedef struct {
  uint8_t len; // 1..3
  char s[3];   // digits only, no terminator
} dec3_t;

// OPTIMIZATION 14: Cache-friendly data layout - group frequently accessed data
// Cache line is typically 64 bytes, so we organize data to minimize cache misses
struct __attribute__((aligned(64))) ascii_color_cache {
  // Hot path #1: Character lookup (accessed every pixel)
  char luminance_palette[256];

  // Hot path #2: Decimal string lookup (accessed when colors change)
  dec3_t dec3_table[256];

  // Hot path #3: Initialization flags (checked frequently)
  bool palette_initialized;
  bool dec3_initialized;

  // Cold data: Constants (accessed once during init)
  const char ascii_chars[24]; // "   ...',;:clodxkO0KXNWM"
  int palette_len;
} __attribute__((aligned(64)));

// #define luminance_palette g_ascii_cache.luminance_palette

// Pre-calculated luminance palette
static bool palette_initialized = false;

// Single cache-friendly structure instead of scattered static variables
static struct ascii_color_cache g_ascii_cache = {.ascii_chars = "   ...',;:clodxkO0KXNWM",
                                                 .palette_len = 21, // sizeof("   ...',;:clodxkO0KXNWM") - 2
                                                 .palette_initialized = false,
                                                 .dec3_initialized = false};
static char luminance_palette[256];

void init_palette(void);

// Pre-computed ANSI escape code templates
// static const char ANSI_FG_PREFIX[] = "\033[38;2;";  // Unused, replaced by inline constants
// static const char ANSI_BG_PREFIX[] = "\033[48;2;";  // Unused, replaced by inline constants
// static const char ANSI_SUFFIX[] = "m";
static const char ANSI_RESET[] = "\033[0m";

void init_dec3(void);

typedef struct {
  char *data; // pointer to allocated buffer
  size_t len; // number of bytes currently used
  size_t cap; // total bytes allocated
} Str;

// RLE state for ANSI color optimization (used by NEON renderer)
typedef struct {
  int cFR, cFG, cFB, cBR, cBG, cBB;
  int runLen;
  int seeded;
} RLEState;

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
simd_benchmark_t benchmark_simd_color_conversion(int width, int height, int iterations, bool background_mode);

// Enhanced benchmark functions with image source support
simd_benchmark_t benchmark_simd_conversion_with_source(int width, int height, int iterations,
                                                       const image_t *source_image);
simd_benchmark_t benchmark_simd_color_conversion_with_source(int width, int height, int iterations,
                                                             bool background_mode, const image_t *source_image,
                                                             bool use_fast_path);
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
bool get_256_color_fast_path(void);             // Query current quality mode setting

// FIX #5: Cache prewarming functions for benchmarks (prevents first-frame penalty)
void prewarm_sgr256_fg_cache(void);
void prewarm_sgr256_cache(void);

// Fast SGR cache wrappers for SIMD implementations
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out);
char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out);

// Enhanced REP compression writer (declared in NEON file, used by scalar fallbacks)
size_t write_row_rep_from_arrays_enhanced(const uint8_t *fg_r, const uint8_t *fg_g, const uint8_t *fg_b,
                                          const uint8_t *bg_r, const uint8_t *bg_g, const uint8_t *bg_b,
                                          const uint8_t *fg_idx, const uint8_t *bg_idx, const char *ascii_chars,
                                          int width, char *dst, size_t cap, bool utf8_block_mode, bool use_256color,
                                          bool is_truecolor);

// Scalar unified REP implementations (for NEON dispatcher fallback)
size_t render_row_256color_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
size_t render_row_truecolor_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
size_t render_row_256color_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
size_t render_row_truecolor_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);

// Unified NEON + scalar REP dispatcher
size_t render_row_color_ascii_dispatch(const rgb_pixel_t *row, int width, char *dst, size_t cap, bool background_mode,
                                       bool use_fast_path);
