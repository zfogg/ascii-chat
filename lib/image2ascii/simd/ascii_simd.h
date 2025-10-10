#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "../image.h"
#include "common.h"

// Check for SIMD support and include architecture-specific headers
// Disable SIMD on Windows with Clang due to intrinsics compatibility issues
#if defined(_WIN32) && defined(__clang__) && __clang_major__ >= 20
// Windows with Clang 20+ has issues with MMX intrinsics
// Keep SIMD disabled until fixed
#else
#ifdef __ARM_FEATURE_SVE
#define SIMD_SUPPORT_SVE 1
#endif

#ifdef __AVX2__
#define SIMD_SUPPORT_AVX2 1
#endif

#ifdef __SSE2__
#define SIMD_SUPPORT_SSE2 1
#endif

#ifdef __SSSE3__
#define SIMD_SUPPORT_SSSE3 1
#endif

#ifdef __ARM_NEON
#define SIMD_SUPPORT_NEON 1
#endif
#endif

// Luminance calculation constants (shared across all files)
// formula: Y = (RED*R + GREEN*G + BLUE*B + THRESHOLD) >> 8
#define LUMA_RED 77    // 0.299 * 256
#define LUMA_GREEN 150 // 0.587 * 256
#define LUMA_BLUE 29   // 0.114 * 256
#define LUMA_THRESHOLD 128

// Forward declaration for cache structure
typedef struct {
  uint8_t len; // 1..3
  char s[3];   // digits only, no terminator
} dec3_t;

// Global dec3 cache for digit conversion (shared across all clients)
typedef struct {
  dec3_t dec3_table[256];
  bool dec3_initialized;
} global_dec3_cache_t;

extern global_dec3_cache_t g_dec3_cache;

// Pre-computed ANSI escape code templates
// static const char ANSI_FG_PREFIX[] = "\033[38;2;";  // Unused, replaced by inline constants
// static const char ANSI_BG_PREFIX[] = "\033[48;2;";  // Unused, replaced by inline constants
// static const char ANSI_SUFFIX[] = "m";
static const char ANSI_RESET[] = "\033[0m";

void init_dec3(void);
void ascii_simd_init(void);

// Default palette access for legacy functions
extern char g_default_luminance_palette[256];
void init_default_luminance_palette(void);

typedef struct {
  char *data; // pointer to allocated buffer
  size_t len; // number of bytes currently used
  size_t cap; // total bytes allocated
} Str;

// String utility functions
void str_init(Str *s);
void str_free(Str *s);
void str_reserve(Str *s, size_t need);
void str_append_bytes(Str *s, const void *src, size_t n);
void str_append_c(Str *s, char c);
void str_printf(Str *s, const char *fmt, ...);

// RLE state for ANSI color optimization (used by NEON renderer)
typedef struct {
  int cFR, cFG, cFB, cBR, cBG, cBB;
  int runLen;
  int seeded;
} RLEState;

typedef rgb_t rgb_pixel_t;

// ImageRGB structure for NEON renderers
// Based on usage in ascii_simd_neon.c where img->w, img->h, img->pixels are accessed
typedef struct {
  int w, h;
  uint8_t *pixels; // RGB data: w * h * 3 bytes
} ImageRGB;

// Allocate a new ImageRGB (RGB8), abort on OOM
ImageRGB alloc_image(int w, int h);

// Fallback scalar version
void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count, const char luminance_palette[256]);
char *convert_pixels_scalar_with_newlines(image_t *image, const char luminance_palette[256]);

// Row-based functions removed - use image_print_color() instead

// Benchmark functions
typedef struct {
  double scalar_time;
  double sse2_time;
  double ssse3_time;
  double avx2_time;
  double neon_time;
  double sve_time;
  double speedup_best;
  const char *best_method;
} simd_benchmark_t;

simd_benchmark_t benchmark_simd_conversion(int width, int height, int iterations);
simd_benchmark_t benchmark_simd_color_conversion(int width, int height, int iterations, bool background_mode);

// Enhanced benchmark functions with image source support
simd_benchmark_t benchmark_simd_conversion_with_source(int width, int height, int iterations, bool background_mode,
                                                       const image_t *source_image, bool use_256color);
simd_benchmark_t benchmark_simd_color_conversion_with_source(int width, int height, int iterations,
                                                             bool background_mode, const image_t *source_image,
                                                             bool use_256color);
void print_simd_capabilities(void);

char *image_print_simd(image_t *image, const char *ascii_chars);
char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_256color, const char *ascii_chars);

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
                                          int width, char *dst, size_t cap, bool is_truecolor);

// Include architecture-specific implementations
#ifdef SIMD_SUPPORT_SSE2
#include "image2ascii/simd/sse2.h"
#endif
#ifdef SIMD_SUPPORT_SSSE3
#include "image2ascii/simd/ssse3.h"
#endif
#ifdef SIMD_SUPPORT_AVX2
#include "image2ascii/simd/avx2.h"
#endif
#ifdef SIMD_SUPPORT_SVE
#include "image2ascii/simd/sve.h"
#endif
#ifdef SIMD_SUPPORT_NEON
#include "image2ascii/simd/neon.h"
#endif
