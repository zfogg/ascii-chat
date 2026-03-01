#pragma once

/**
 * @file video/render/scalar/background.h
 * @brief Scalar background color rendering functions
 * @ingroup video
 *
 * Scalar (non-SIMD) functions for rendering ASCII art with background colors.
 */

#include "image.h"
#include "platform/terminal.h"

/**
 * @brief Convert image to colored ASCII art with background colors
 * @param p Image pointer
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with background color codes (caller must free), or NULL on error
 */
char *image_print_color_background(const image_t *p, const char *palette);
