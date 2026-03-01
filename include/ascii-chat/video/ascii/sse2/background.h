#pragma once

/**
 * @file video/render/sse2/background.h
 * @brief SSE2 background color rendering functions
 * @ingroup video
 *
 * Functions for rendering ASCII art with background colors using SSE2.
 */

#include <ascii-chat/video/rgba/image.h>

#if SIMD_SUPPORT_SSE2

/**
 * @brief Render image as ASCII with background colors using SSE2
 * @param image Source image
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI background codes (caller must free), or NULL on error
 */
char *render_ascii_sse2_background(const image_t *image, bool use_256color, const char *ascii_chars);

#endif
