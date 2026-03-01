#pragma once

/**
 * @file video/render/avx2/background.h
 * @brief AVX2 background color rendering functions
 * @ingroup video
 *
 * Functions for rendering ASCII art with background colors using AVX2.
 */

#include <ascii-chat/video/rgba/image.h>

#if SIMD_SUPPORT_AVX2

/**
 * @brief Render image as ASCII with background colors using AVX2
 * @param image Source image
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI background codes (caller must free), or NULL on error
 */
char *render_ascii_avx2_background(const image_t *image, bool use_256color, const char *ascii_chars);

#endif
