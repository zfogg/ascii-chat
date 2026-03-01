#pragma once

/**
 * @file video/scalar/16color.h
 * @brief Scalar 16-color ASCII rendering
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * Scalar (non-SIMD) implementation for converting RGB images to
 * 16-color ANSI ASCII art with optional dithering.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#include <stddef.h>
#include <stdbool.h>
#include "image.h"

/**
 * @brief Convert image to 16-color ANSI ASCII art
 * @param image Image pointer
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with 16-color codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using the ANSI 16-color palette.
 * This is the standard 8 colors + bold 8 colors palette.
 *
 * @ingroup video
 */
char *image_print_16color(const image_t *image, const char *palette);

/** @} */
