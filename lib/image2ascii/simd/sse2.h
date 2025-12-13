#pragma once

/**
 * @file image2ascii/simd/sse2.h
 * @brief SSE2-optimized ASCII rendering functions
 * @ingroup image2ascii
 * @addtogroup image2ascii
 * @{
 *
 * This header provides SSE2 (Streaming SIMD Extensions 2) optimized
 * functions for converting images to ASCII art on x86-64 CPUs.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdbool.h>
#include "../image.h"

#if SIMD_SUPPORT_SSE2
#if (!defined(__SSE2__) && !defined(_M_X64) && !(defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#error "SSE2 support required"
#endif
#ifdef _WIN32
// Windows: Use immintrin.h with proper feature detection
// MSVC defines _M_X64 for x64 (which always has SSE2) or _M_IX86_FP >= 2 for x86 with SSE2
#include <immintrin.h>
#else
#include <emmintrin.h>
#endif

/**
 * @brief Render image as monochrome ASCII using SSE2
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_image_monochrome_sse2(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with color using SSE2 (unified optimized)
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_sse2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

/**
 * @brief Destroy SSE2 caches
 *
 * @ingroup image2ascii
 */
void sse2_caches_destroy(void);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSE2 */

/** @} */
