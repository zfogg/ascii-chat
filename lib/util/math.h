#pragma once

/**
 * @file util/math.h
 * @ingroup util
 * @brief Mathematical Utility Functions
 *
 * This header provides mathematical utilities for image processing, including
 * rounding, clamping, and aspect ratio calculations. Functions handle terminal
 * character aspect ratio correction for proper ASCII art rendering.
 *
 * CORE FEATURES:
 * ==============
 * - RGB value clamping (0-255 range)
 * - Floating-point to integer rounding
 * - Aspect ratio calculations with terminal correction
 * - Image dimension fitting and scaling
 * - Stretch/no-stretch mode support
 *
 * TERMINAL CHARACTER CORRECTION:
 * ==============================
 * Terminal characters have different aspect ratios than square pixels:
 * - Terminal characters are typically taller than wide (e.g., 2:1 ratio)
 * - This correction ensures ASCII art maintains proper proportions
 * - aspect_ratio() applies terminal correction automatically
 * - aspect_ratio2() skips correction for pixel-based calculations
 *
 * ASPECT RATIO CALCULATIONS:
 * ==========================
 * Functions calculate dimensions that maintain image aspect ratio:
 * - aspect_ratio(): With terminal character correction
 * - aspect_ratio2(): Without terminal correction (simple pixel math)
 * - calculate_fit_dimensions_pixel(): Fit image within maximum bounds
 *
 * @note Terminal character aspect ratio correction is essential for proper
 *       ASCII art rendering that matches the original image proportions.
 * @note All output parameters are modified only on successful calculation.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "platform/internal.h" // For ssize_t on all platforms

/* ============================================================================
 * Mathematical Macros
 * ============================================================================
 */

/**
 * @brief Round floating-point value to nearest integer
 * @param xfloat Floating-point value to round
 * @return Nearest integer value
 *
 * Rounds a floating-point value to the nearest integer using standard rounding
 * rules (0.5 rounds up). Uses simple addition of 0.5 and truncation.
 *
 * @note This is a macro for performance (inlined at compile time).
 * @note Uses standard rounding (banker's rounding not applied).
 *
 * @par Example
 * @code
 * int rounded = ROUND(3.7);   // Returns 4
 * int rounded = ROUND(3.2);   // Returns 3
 * int rounded = ROUND(-2.5);  // Returns -2
 * @endcode
 *
 * @ingroup util
 */
#define ROUND(xfloat) (int)(0.5f + (xfloat))

/* ============================================================================
 * Clamping Functions
 * ============================================================================
 */

/**
 * @brief Clamp value to valid RGB range (0-255)
 * @param value Integer value to clamp
 * @return Clamped value in range [0, 255]
 *
 * Clamps an integer value to the valid RGB color component range (0-255).
 * Values below 0 are clamped to 0, values above 255 are clamped to 255.
 * Useful for color calculations that may overflow or underflow.
 *
 * @note This is an inline function for performance (inlined at compile time).
 * @note Returns uint8_t to match RGB component type.
 *
 * @par Example
 * @code
 * uint8_t r = clamp_rgb(-10);  // Returns 0
 * uint8_t g = clamp_rgb(128);  // Returns 128
 * uint8_t b = clamp_rgb(300);  // Returns 255
 * @endcode
 *
 * @ingroup util
 */
static inline uint8_t clamp_rgb(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (uint8_t)value;
}

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
