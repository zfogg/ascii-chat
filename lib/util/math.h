#pragma once

/**
 * @file util/math.h
 * @brief 🔢 Mathematical Utility Functions
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides lightweight mathematical utilities for image processing,
 * including rounding and RGB value clamping.
 *
 * CORE FEATURES:
 * ==============
 * - RGB value clamping (0-255 range)
 * - Floating-point to integer rounding
 *
 * @note For aspect ratio calculations, see util/aspect_ratio.h
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
