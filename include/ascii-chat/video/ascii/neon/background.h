#pragma once

/**
 * @file video/render/neon/background.h
 * @brief ARM NEON background color rendering functions
 * @ingroup video
 *
 * Functions for rendering ASCII art with background colors using ARM NEON.
 */

#include <ascii-chat/video/rgba/image.h>

#if SIMD_SUPPORT_NEON

/**
 * @brief Render image as ASCII with background colors using NEON
 * @param image Source image
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI background codes (caller must free), or NULL on error
 */
char *render_ascii_neon_background(const image_t *image, bool use_256color, const char *ascii_chars);

#endif
