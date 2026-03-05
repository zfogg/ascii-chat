#pragma once

/**
 * @file video/ascii/sse2.h
 * @brief SSE2-optimized ASCII rendering functions
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * This header provides SSE2 (Streaming SIMD Extensions 2) optimized
 * functions for converting images to ASCII art on x86-64 CPUs.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/video/rgba/image.h>

#if SIMD_SUPPORT_SSE2
#if (!defined(__SSE2__) && !defined(_M_X64) && !(defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#error "SSE2 support required"
#endif

/**
 * @brief Render image as monochrome ASCII using SSE2
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Matches scalar image_print() interface for compatibility.
 *
 * @ingroup video
 */
char *render_ascii_mono_sse2(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with color using SSE2
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup video
 */
char *render_ascii_color_sse2(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

/**
 * @brief Destroy SSE2 caches
 *
 * @ingroup video
 */
void sse2_caches_destroy(void);

#endif /* SIMD_SUPPORT_SSE2 */

/** @} */
