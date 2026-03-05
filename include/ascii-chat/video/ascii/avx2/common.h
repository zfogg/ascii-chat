#pragma once

/**
 * @file video/ascii/avx2/common.h
 * @brief AVX2-optimized ASCII rendering functions
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * This header provides AVX2 (Advanced Vector Extensions 2) optimized
 * functions for converting images to ASCII art on modern x86-64 CPUs.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/video/rgba/image.h>

#if SIMD_SUPPORT_AVX2

/**
 * @brief Render image as monochrome ASCII using AVX2
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Matches scalar image_print() interface for compatibility.
 *
 * @ingroup video
 */
char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with foreground colors using AVX2 (unified optimized)
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup video
 */
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

/**
 * @brief Render image as ASCII with background colors using AVX2
 * @param image Source image
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI background codes (caller must free), or NULL on error
 *
 * @ingroup video
 */
char *render_ascii_avx2_background(const image_t *image, bool use_256color, const char *ascii_chars);

#endif

/** @} */
