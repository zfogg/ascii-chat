#pragma once

/**
 * @file video/scalar/256color.h
 * @brief Scalar 256-color ASCII rendering
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * Scalar (non-SIMD) implementation for converting RGB images to
 * 256-color ANSI ASCII art.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#include <stddef.h>
#include "image.h"

/**
 * @brief Convert image to 256-color ANSI ASCII art
 * @param image Image pointer
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with 256-color codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using the ANSI 256-color palette,
 * providing more color accuracy than the 16-color palette.
 *
 * @ingroup video
 */
char *image_print_256color(const image_t *image, const char *palette);

/** @} */
