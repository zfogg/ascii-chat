#pragma once

/**
 * @file video/scalar/16color_dithered.h
 * @brief Scalar dithered 16-color ASCII rendering
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * Scalar (non-SIMD) implementations for converting RGB images to
 * 16-color ANSI ASCII art with dithering for better color approximation.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#include <stddef.h>
#include <stdbool.h>
#include "image.h"

/**
 * @brief Convert image to 16-color dithered ASCII art
 * @param image Image pointer
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with dithered 16-color codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using 16-color palette with
 * ordered dithering for improved color approximation.
 *
 * @ingroup video
 */
char *image_print_16color_dithered(const image_t *image, const char *palette);

/**
 * @brief Convert image to 16-color dithered ASCII art with background colors
 * @param image Image pointer
 * @param use_background Whether to use background colors
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with dithered 16-color codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using 16-color palette with dithering,
 * optionally using background colors for better fidelity.
 *
 * @ingroup video
 */
char *image_print_16color_dithered_with_background(const image_t *image, bool use_background, const char *palette);

/** @} */
