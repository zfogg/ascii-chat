#pragma once

/**
 * @file image2ascii/simd/ascii_simd.h
 * @ingroup image2ascii
 * @brief SIMD-optimized ASCII conversion interface
 *
 * This header provides SIMD-optimized functions for converting images to ASCII art.
 * Supports multiple architectures including SSE2, SSSE3, AVX2 (x86), NEON (ARM), and SVE (ARM).
 *
 * The interface provides:
 * - SIMD initialization and setup
 * - Architecture-specific rendering functions
 * - Benchmarking utilities
 * - Cache management
 * - UTF-8 character handling
 *
 * @note SIMD support is automatically detected and enabled based on CPU capabilities.
 *       CMake controls SIMD levels via ENABLE_SIMD_* options.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include "../image.h"
#include "common.h"

// Check for SIMD support and include architecture-specific headers
// CMake explicitly controls which SIMD levels to enable via ENABLE_SIMD_* options
// and defines SIMD_SUPPORT_* to 1 (enabled) or 0 (disabled).
//
// Only auto-detect from compiler macros if CMake hasn't already defined them.
// This ensures CMake's explicit choices (e.g., AVX2-only, no SSE2/SSSE3) are respected.
//
// Disable SIMD on Windows with Clang due to intrinsics compatibility issues
#if defined(_WIN32) && defined(__clang__) && __clang_major__ >= 20
// Windows with Clang 20+ has issues with MMX intrinsics
// Keep SIMD disabled until fixed
#else
// Only auto-detect if CMake hasn't already defined the macro
#if defined(__ARM_FEATURE_SVE) && !defined(SIMD_SUPPORT_SVE)
#define SIMD_SUPPORT_SVE 1
#endif

#if defined(__AVX2__) && !defined(SIMD_SUPPORT_AVX2)
#define SIMD_SUPPORT_AVX2 1
#endif

#if defined(__SSE2__) && !defined(SIMD_SUPPORT_SSE2)
#define SIMD_SUPPORT_SSE2 1
#endif

#if defined(__SSSE3__) && !defined(SIMD_SUPPORT_SSSE3)
#define SIMD_SUPPORT_SSSE3 1
#endif

#if defined(__ARM_NEON) && !defined(SIMD_SUPPORT_NEON)
#define SIMD_SUPPORT_NEON 1
#endif
#endif

/**
 * @name Luminance Calculation Constants
 * @{
 */

/** @brief Luminance red coefficient (0.299 * 256 = 77) */
#define LUMA_RED 77
/** @brief Luminance green coefficient (0.587 * 256 = 150) */
#define LUMA_GREEN 150
/** @brief Luminance blue coefficient (0.114 * 256 = 29) */
#define LUMA_BLUE 29
/** @brief Luminance threshold for rounding */
#define LUMA_THRESHOLD 128

/** @} */

/**
 * @brief Decimal conversion cache structure (1-3 digits)
 *
 * @ingroup image2ascii
 */
typedef struct {
  uint8_t len; /**< Number of digits (1-3) */
  char s[3];   /**< Digit string (no null terminator) */
} dec3_t;

/**
 * @brief Global decimal cache for digit conversion
 *
 * @ingroup image2ascii
 */
typedef struct {
  dec3_t dec3_table[256]; /**< Lookup table for 0-255 */
  bool dec3_initialized;  /**< Whether cache is initialized */
} global_dec3_cache_t;

/** @brief Global decimal cache instance */
extern global_dec3_cache_t g_dec3_cache;

// Pre-computed ANSI escape code templates
// static const char ANSI_FG_PREFIX[] = "\033[38;2;";  // Unused, replaced by inline constants
// static const char ANSI_BG_PREFIX[] = "\033[48;2;";  // Unused, replaced by inline constants
// static const char ANSI_SUFFIX[] = "m";
static const char ANSI_RESET[] = "\033[0m";

/**
 * @brief Initialize decimal lookup table
 *
 * Must be called before using any SIMD conversion functions.
 *
 * @ingroup image2ascii
 */
void init_dec3(void);

/**
 * @brief Initialize SIMD subsystem
 *
 * Initializes all SIMD subsystems and caches. Must be called once at startup.
 *
 * @ingroup image2ascii
 */
void ascii_simd_init(void);

/** @brief Default luminance palette (256 characters) */
extern char g_default_luminance_palette[256];

/**
 * @brief Initialize default luminance palette
 *
 * @ingroup image2ascii
 */
void init_default_luminance_palette(void);

/**
 * @brief Dynamic string buffer structure
 *
 * @ingroup image2ascii
 */
typedef struct {
  char *data; /**< Pointer to allocated buffer */
  size_t len; /**< Number of bytes currently used */
  size_t cap; /**< Total bytes allocated */
} Str;

/**
 * @brief Initialize string buffer
 * @param s String buffer structure
 *
 * @ingroup image2ascii
 */
void str_init(Str *s);

/**
 * @brief Free string buffer
 * @param s String buffer structure
 *
 * @ingroup image2ascii
 */
void str_free(Str *s);

/**
 * @brief Reserve space in string buffer
 * @param s String buffer structure
 * @param need Minimum bytes needed
 *
 * @ingroup image2ascii
 */
void str_reserve(Str *s, size_t need);

/**
 * @brief Append bytes to string buffer
 * @param s String buffer structure
 * @param src Source data
 * @param n Number of bytes to append
 *
 * @ingroup image2ascii
 */
void str_append_bytes(Str *s, const void *src, size_t n);

/**
 * @brief Append character to string buffer
 * @param s String buffer structure
 * @param c Character to append
 *
 * @ingroup image2ascii
 */
void str_append_c(Str *s, char c);

/**
 * @brief Append formatted string to buffer
 * @param s String buffer structure
 * @param fmt Format string
 * @param ... Variable arguments
 *
 * @ingroup image2ascii
 */
void str_printf(Str *s, const char *fmt, ...);

/**
 * @brief Run-length encoding state for ANSI color optimization
 *
 * Used by NEON renderer for efficient color output.
 *
 * @ingroup image2ascii
 */
typedef struct {
  int cFR, cFG, cFB; /**< Current foreground RGB */
  int cBR, cBG, cBB; /**< Current background RGB */
  int runLen;        /**< Current run length */
  int seeded;        /**< Whether state is initialized */
} RLEState;

/** @brief RGB pixel type alias */
typedef rgb_t rgb_pixel_t;

/**
 * @brief ImageRGB structure for NEON renderers
 *
 * Simplified RGB image format for NEON optimizations.
 *
 * @ingroup image2ascii
 */
typedef struct {
  int w;           /**< Image width in pixels */
  int h;           /**< Image height in pixels */
  uint8_t *pixels; /**< RGB data: w * h * 3 bytes */
} ImageRGB;

/**
 * @brief Allocate a new ImageRGB (RGB8 format)
 * @param w Image width
 * @param h Image height
 * @return Allocated ImageRGB structure
 *
 * @note Aborts on out-of-memory condition.
 *
 * @ingroup image2ascii
 */
ImageRGB alloc_image(int w, int h);

/**
 * @brief Convert pixels to ASCII (scalar fallback)
 * @param pixels RGB pixel array
 * @param ascii_chars Output buffer for ASCII characters
 * @param count Number of pixels to convert
 * @param luminance_palette Luminance-to-character mapping palette
 *
 * @ingroup image2ascii
 */
void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count, const char luminance_palette[256]);

/**
 * @brief Convert image to ASCII with newlines (scalar fallback)
 * @param image Source image
 * @param luminance_palette Luminance-to-character mapping palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *convert_pixels_scalar_with_newlines(image_t *image, const char luminance_palette[256]);

// Row-based functions removed - use image_print_color() instead

/**
 * @brief SIMD benchmark results structure
 *
 * @ingroup image2ascii
 */
typedef struct {
  double scalar_time;      /**< Scalar implementation time (seconds) */
  double sse2_time;        /**< SSE2 implementation time (seconds) */
  double ssse3_time;       /**< SSSE3 implementation time (seconds) */
  double avx2_time;        /**< AVX2 implementation time (seconds) */
  double neon_time;        /**< NEON implementation time (seconds) */
  double sve_time;         /**< SVE implementation time (seconds) */
  double speedup_best;     /**< Best speedup ratio */
  const char *best_method; /**< Name of fastest method */
} simd_benchmark_t;

/**
 * @brief Benchmark SIMD conversion methods (monochrome)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param iterations Number of benchmark iterations
 * @return Benchmark results structure
 *
 * @ingroup image2ascii
 */
simd_benchmark_t benchmark_simd_conversion(int width, int height, int iterations);

/**
 * @brief Benchmark SIMD color conversion methods
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param iterations Number of benchmark iterations
 * @param background_mode Use background colors
 * @return Benchmark results structure
 *
 * @ingroup image2ascii
 */
simd_benchmark_t benchmark_simd_color_conversion(int width, int height, int iterations, bool background_mode);

/**
 * @brief Benchmark SIMD conversion with source image
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param iterations Number of benchmark iterations
 * @param background_mode Use background colors
 * @param source_image Source image for conversion
 * @param use_256color Use 256-color mode
 * @return Benchmark results structure
 *
 * @ingroup image2ascii
 */
simd_benchmark_t benchmark_simd_conversion_with_source(int width, int height, int iterations, bool background_mode,
                                                       const image_t *source_image, bool use_256color);

/**
 * @brief Benchmark SIMD color conversion with source image
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param iterations Number of benchmark iterations
 * @param background_mode Use background colors
 * @param source_image Source image for conversion
 * @param use_256color Use 256-color mode
 * @return Benchmark results structure
 *
 * @ingroup image2ascii
 */
simd_benchmark_t benchmark_simd_color_conversion_with_source(int width, int height, int iterations,
                                                             bool background_mode, const image_t *source_image,
                                                             bool use_256color);

/**
 * @brief Print detected SIMD capabilities
 *
 * Prints available SIMD instruction sets to stdout.
 *
 * @ingroup image2ascii
 */
void print_simd_capabilities(void);

/**
 * @brief Print image as ASCII using SIMD (monochrome)
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *image_print_simd(image_t *image, const char *ascii_chars);

/**
 * @brief Print image as ASCII with color using SIMD
 * @param image Source image
 * @param use_background_mode Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_256color, const char *ascii_chars);

/**
 * @brief Set color quality mode
 * @param high_quality true for 24-bit truecolor, false for 256-color
 *
 * Controls tradeoff between color fidelity and rendering speed.
 *
 * @ingroup image2ascii
 */
void set_color_quality_mode(bool high_quality);

/**
 * @brief Get 256-color fast path setting
 * @return true if 256-color mode is enabled, false for truecolor
 *
 * @ingroup image2ascii
 */
bool get_256_color_fast_path(void);

/**
 * @brief Prewarm 256-color foreground cache for benchmarks
 *
 * Prevents first-frame performance penalty in benchmarks.
 *
 * @ingroup image2ascii
 */
void prewarm_sgr256_fg_cache(void);

/**
 * @brief Prewarm 256-color foreground/background cache for benchmarks
 *
 * Prevents first-frame performance penalty in benchmarks.
 *
 * @ingroup image2ascii
 */
void prewarm_sgr256_cache(void);

/**
 * @brief Get 256-color foreground ANSI sequence string
 * @param fg 256-color palette index (0-255)
 * @param len_out Output parameter for sequence length
 * @return Pointer to ANSI sequence string (cached)
 *
 * @ingroup image2ascii
 */
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out);

/**
 * @brief Get 256-color foreground/background ANSI sequence string
 * @param fg Foreground palette index (0-255)
 * @param bg Background palette index (0-255)
 * @param len_out Output parameter for sequence length
 * @return Pointer to ANSI sequence string (cached)
 *
 * @ingroup image2ascii
 */
char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out);

/**
 * @brief Enhanced REP compression writer
 * @param fg_r Foreground red array
 * @param fg_g Foreground green array
 * @param fg_b Foreground blue array
 * @param bg_r Background red array
 * @param bg_g Background green array
 * @param bg_b Background blue array
 * @param fg_idx Foreground color indices
 * @param bg_idx Background color indices
 * @param ascii_chars Character palette
 * @param width Image width in pixels
 * @param dst Output buffer
 * @param cap Output buffer capacity
 * @param is_truecolor Use truecolor mode
 * @return Number of bytes written
 *
 * Used by scalar fallbacks and declared in NEON implementation.
 *
 * @ingroup image2ascii
 */
size_t write_row_rep_from_arrays_enhanced(const uint8_t *fg_r, const uint8_t *fg_g, const uint8_t *fg_b,
                                          const uint8_t *bg_r, const uint8_t *bg_g, const uint8_t *bg_b,
                                          const uint8_t *fg_idx, const uint8_t *bg_idx, const char *ascii_chars,
                                          int width, char *dst, size_t cap, bool is_truecolor);

// Include architecture-specific implementations
#if SIMD_SUPPORT_SSE2
#include "image2ascii/simd/sse2.h"
#endif
#if SIMD_SUPPORT_SSSE3
#include "image2ascii/simd/ssse3.h"
#endif
#if SIMD_SUPPORT_AVX2
#include "image2ascii/simd/avx2.h"
#endif
#if SIMD_SUPPORT_SVE
#include "image2ascii/simd/sve.h"
#endif
#if SIMD_SUPPORT_NEON
#include "image2ascii/simd/neon.h"
#endif
