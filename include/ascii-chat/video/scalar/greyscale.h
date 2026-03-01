#pragma once

/**
 * @file video/scalar/greyscale.h
 * @brief Scalar greyscale and color ASCII rendering
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * Scalar (non-SIMD) implementations for converting RGB images to
 * greyscale (monochrome) and colored ASCII art using character
 * palettes.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#include <stddef.h>
#include "image.h"
#include "platform/terminal.h"

/**
 * @brief Convert image to greyscale ASCII art
 * @param p Image pointer
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Converts an image to greyscale ASCII using a character palette
 * based on luminance values (ITU-R BT.601).
 *
 * @ingroup video
 */
char *image_print(const image_t *p, const char *palette);

/**
 * @brief Convert image to colored ASCII art
 * @param p Image pointer
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with color codes (caller must free), or NULL on error
 *
 * Converts an image to colored ASCII using ANSI color codes.
 * Uses ANSI 16-color palette for color approximation.
 *
 * @ingroup video
 */
char *image_print_color(const image_t *p, const char *palette);

/** @} */
