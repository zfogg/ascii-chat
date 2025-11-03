#pragma once

/**
 * @file image2ascii/simd/avx2.h
 * @ingroup image2ascii
 * @brief AVX2-optimized ASCII rendering functions
 *
 * This header provides AVX2 (Advanced Vector Extensions 2) optimized
 * functions for converting images to ASCII art on modern x86-64 CPUs.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include "../image.h"

#if SIMD_SUPPORT_AVX2

/**
 * @brief Render image as monochrome ASCII using AVX2
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with color using AVX2 (unified optimized)
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

/**
 * @brief Destroy AVX2 caches
 *
 * @ingroup image2ascii
 */
void avx2_caches_destroy(void);

#endif /* SIMD_SUPPORT_AVX2 */
