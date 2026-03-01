#pragma once

/**
 * @file video/render/ssse3/background.h
 * @brief SSSE3 background color rendering functions
 * @ingroup video
 *
 * Functions for rendering ASCII art with background colors using SSSE3.
 */

#include <ascii-chat/video/image.h>

#if SIMD_SUPPORT_SSSE3

/**
 * @brief Render image as ASCII with background colors using SSSE3
 * @param image Source image
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI background codes (caller must free), or NULL on error
 */
char *render_ascii_ssse3_background(const image_t *image, bool use_256color, const char *ascii_chars);

#endif
