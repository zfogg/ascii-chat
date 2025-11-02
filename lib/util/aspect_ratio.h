#pragma once

/**
 * @file aspect_ratio.h
 * @brief Aspect Ratio Calculation Functions
 *
 * This header provides functions for calculating image dimensions while
 * maintaining aspect ratio. Functions handle terminal character correction
 * for ASCII art rendering and pixel-based calculations for image scaling.
 *
 * CORE FEATURES:
 * ==============
 * - Aspect ratio calculations with terminal character correction
 * - Simple aspect ratio calculations without terminal correction
 * - Image dimension fitting within maximum bounds
 * - Stretch/no-stretch mode support
 * - Pixel-based and character-based calculations
 *
 * TERMINAL CHARACTER CORRECTION:
 * =============================
 * Terminal characters have different aspect ratios than square pixels:
 * - Terminal characters are typically taller than wide (e.g., 2:1 ratio)
 * - This correction ensures ASCII art maintains proper proportions
 * - aspect_ratio() applies terminal correction automatically
 * - aspect_ratio2() skips correction for pixel-based calculations
 *
 * ASPECT RATIO CALCULATIONS:
 * ==========================
 * Functions calculate dimensions that maintain image aspect ratio:
 * - aspect_ratio(): With terminal character correction (for ASCII art)
 * - aspect_ratio2(): Without terminal correction (simple pixel math)
 * - calculate_fit_dimensions_pixel(): Fit image within maximum bounds
 *
 * @note Terminal character aspect ratio correction is essential for proper
 *       ASCII art rendering that matches the original image proportions.
 * @note All output parameters are modified only on successful calculation.
 *
 * @note This file contains functions based on jp2a code:
 *       Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 *       Distributed under the GNU General Public License (GPL) v2.
 *       Project homepage on http://jp2a.sf.net
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "platform/abstraction.h"
#include <stdbool.h>

/* ============================================================================
 * Aspect Ratio Calculation Functions
 * @{
 */

/**
 * @brief Calculate aspect ratio with terminal character correction
 * @param img_width Input image width in pixels (must be > 0)
 * @param img_height Input image height in pixels (must be > 0)
 * @param width Target width in terminal characters (must be > 0)
 * @param height Target height in terminal lines (must be > 0)
 * @param stretch Whether to allow stretching (true) or maintain aspect ratio (false)
 * @param out_width Output parameter for calculated width (must not be NULL)
 * @param out_height Output parameter for calculated height (must not be NULL)
 *
 * Calculates output dimensions for ASCII art rendering with terminal character
 * aspect ratio correction. Terminal characters are typically taller than wide
 * (e.g., 2:1 ratio), so this function corrects for that to maintain proper
 * image proportions in ASCII art.
 *
 * TERMINAL CORRECTION:
 * - Applies character aspect ratio correction (accounts for terminal font)
 * - Ensures ASCII art matches original image proportions
 * - When stretch=false, maintains original image aspect ratio
 * - When stretch=true, fills target dimensions exactly
 *
 * @note Output dimensions respect target size while maintaining aspect ratio.
 * @note Terminal character correction is essential for proper ASCII art rendering.
 * @note Output parameters are modified to contain calculated dimensions.
 *
 * @par Example
 * @code
 * ssize_t out_w, out_h;
 * aspect_ratio(1920, 1080, 80, 40, false, &out_w, &out_h);
 * // Calculates dimensions that fit within 80x40 terminal while maintaining 16:9 aspect
 * @endcode
 *
 * @ingroup util
 */
void aspect_ratio(const ssize_t img_width, const ssize_t img_height, const ssize_t width, const ssize_t height,
                  const bool stretch, ssize_t *out_width, ssize_t *out_height);

/**
 * @brief Simple aspect ratio calculation without terminal character correction
 * @param img_width Input image width in pixels (must be > 0)
 * @param img_height Input image height in pixels (must be > 0)
 * @param target_width Target width (must be > 0)
 * @param target_height Target height (must be > 0)
 * @param out_width Output parameter for calculated width (must not be NULL)
 * @param out_height Output parameter for calculated height (must not be NULL)
 *
 * Calculates output dimensions that maintain the input image's aspect ratio
 * while fitting within target dimensions. This is a simple pixel-based calculation
 * without terminal character correction.
 *
 * @note This function does NOT apply terminal character correction.
 * @note Use aspect_ratio() for ASCII art rendering (with terminal correction).
 * @note Output dimensions are calculated to fit within target while maintaining ratio.
 *
 * @par Example
 * @code
 * ssize_t out_w, out_h;
 * aspect_ratio2(1920, 1080, 800, 600, &out_w, &out_h);
 * // Calculates dimensions that fit within 800x600 while maintaining 16:9 aspect
 * @endcode
 *
 * @ingroup util
 */
void aspect_ratio2(const ssize_t img_width, const ssize_t img_height, const ssize_t target_width,
                   const ssize_t target_height, ssize_t *out_width, ssize_t *out_height);

/**
 * @brief Calculate fit dimensions for pixel-based images
 * @param img_width Input image width in pixels (must be > 0)
 * @param img_height Input image height in pixels (must be > 0)
 * @param max_width Maximum allowed width in pixels (must be > 0)
 * @param max_height Maximum allowed height in pixels (must be > 0)
 * @param out_width Output parameter for calculated width (must not be NULL)
 * @param out_height Output parameter for calculated height (must not be NULL)
 *
 * Calculates dimensions that fit the input image within maximum bounds while
 * maintaining the original aspect ratio. Useful for scaling images to fit
 * within display constraints.
 *
 * @note Output dimensions never exceed max_width or max_height.
 * @note Aspect ratio is always maintained (image is not stretched).
 * @note Output dimensions are calculated to fit within maximum bounds.
 *
 * @par Example
 * @code
 * int out_w, out_h;
 * calculate_fit_dimensions_pixel(1920, 1080, 1280, 720, &out_w, &out_h);
 * // Calculates dimensions that fit within 1280x720 while maintaining 16:9 aspect
 * // Result: out_w=1280, out_h=720 (fits exactly)
 * @endcode
 *
 * @ingroup util
 */
void calculate_fit_dimensions_pixel(int img_width, int img_height, int max_width, int max_height, int *out_width,
                                    int *out_height);

/** @} */
