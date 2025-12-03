#pragma once

/**
 * @file image2ascii/simd/ssse3.h
 * @brief SSSE3-optimized ASCII rendering functions
 * @ingroup image2ascii
 * @addtogroup image2ascii
 * @{
 *
 * This header provides SSSE3 (Supplemental Streaming SIMD Extensions 3)
 * optimized functions for converting images to ASCII art on x86-64 CPUs.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#if SIMD_SUPPORT_SSSE3
#if (!defined(__SSSE3__) && !defined(_M_X64) && !defined(_M_AMD64))
#error "SSSE3 support required"
#endif
#ifdef _WIN32
// Windows: Use immintrin.h with proper feature detection
// MSVC doesn't define __SSSE3__ but x64 always has it
#include <immintrin.h>
#else
#include <tmmintrin.h>
#endif

/**
 * @brief Render image as monochrome ASCII using SSSE3
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_image_monochrome_ssse3(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with color using SSSE3 (unified optimized)
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_ssse3_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                           const char *ascii_chars);

/**
 * @brief Destroy SSSE3 caches
 *
 * @ingroup image2ascii
 */
void ssse3_caches_destroy(void);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSSE3 */

/** @} */
