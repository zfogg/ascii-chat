#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "common.h"
#include "image.h"

// Use the project's existing rgb_t for consistency
#include "image.h"

// Check for SIMD support and include architecture-specific headers
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

// Luminance calculation constants (shared across all files)
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
// REMOVED: static bool palette_initialized = false;

// Global cache declaration - defined in ascii_simd.c
extern struct ascii_color_cache g_ascii_cache;
// REMOVED: static char luminance_palette[256];

// Pre-computed ANSI escape code templates
// static const char ANSI_FG_PREFIX[] = "\033[38;2;";  // Unused, replaced by inline constants
// static const char ANSI_BG_PREFIX[] = "\033[48;2;";  // Unused, replaced by inline constants
// static const char ANSI_SUFFIX[] = "m";
static const char ANSI_RESET[] = "\033[0m";

void init_dec3(void);
void init_palette(void);
void ascii_simd_init(void);

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
void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count);

// Row-based functions removed - use image_print_color() instead

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
char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_fast_path);

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
#include "image2ascii/simd/sse2.h"
#include "image2ascii/simd/ssse3.h"
#include "image2ascii/simd/avx2.h"
#include "image2ascii/simd/sve.h"
#include "image2ascii/simd/neon.h"
