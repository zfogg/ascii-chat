#pragma once

/**
 * @file image2ascii/simd/sve.h
 * @ingroup image2ascii
 * @brief SVE-optimized ASCII rendering functions
 *
 * This header provides SVE (Scalable Vector Extension) optimized
 * functions for converting images to ASCII art on ARM processors
 * with SVE support.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#if SIMD_SUPPORT_SVE
#if (defined(__aarch64__) && !defined(__ARM_FEATURE_SVE))
#error "SVE support required for ARM64"
#endif
#include <arm_sve.h>

/**
 * @brief Render image as monochrome ASCII using SVE
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * @ingroup module_video
 */
char *render_ascii_image_monochrome_sve(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with color using SVE (unified optimized)
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup module_video
 */
char *render_ascii_sve_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                         const char *ascii_chars);

/**
 * @brief Destroy SVE caches
 *
 * @ingroup module_video
 */
void sve_caches_destroy(void);

#endif /* SIMD_SUPPORT_SVE */
