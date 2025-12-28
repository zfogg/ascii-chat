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

/* ============================================================================
 * Power-of-Two Utilities
 * ============================================================================
 */

/**
 * @brief Check if a value is a power of two
 * @param n Value to check
 * @return true if n is a power of two (1, 2, 4, 8, 16, ...), false otherwise
 *
 * Efficiently checks whether a value is a power of two using a bitwise trick.
 * Zero returns false (0 is not a power of two).
 *
 * Uses the mathematical property: if n is a power of two, then n & (n-1) == 0.
 * For example:
 * - 8 (binary 1000) & 7 (binary 0111) = 0 ✓ (power of two)
 * - 7 (binary 0111) & 6 (binary 0110) = 6 ✗ (not power of two)
 *
 * @par Example
 * @code
 * bool b1 = math_is_power_of_two(16);  // Returns true
 * bool b2 = math_is_power_of_two(17);  // Returns false
 * bool b3 = math_is_power_of_two(0);   // Returns false
 * bool b4 = math_is_power_of_two(1);   // Returns true (2^0)
 * @endcode
 *
 * @ingroup util
 */
static inline bool math_is_power_of_two(size_t n) {
  return n && !(n & (n - 1));
}

/**
 * @brief Round up to the next power of two
 * @param n Input value
 * @return Smallest power of two greater than or equal to n
 *
 * Rounds up to the nearest power of two. Returns 1 for n=0.
 * Uses bit-spreading technique to efficiently compute the result.
 *
 * Algorithm: Spread the highest set bit to all lower positions, then add 1.
 * Example: 1001 (9) → 1111 (15) → 10000 (16) = 2^4
 *
 * @note This function is commonly used to size circular buffers and hash tables.
 * @note For large values near SIZE_MAX, result may wrap/overflow. Check before using.
 *
 * @par Example
 * @code
 * size_t p1 = math_next_power_of_two(9);    // Returns 16 (2^4)
 * size_t p2 = math_next_power_of_two(16);   // Returns 16 (already power of two)
 * size_t p3 = math_next_power_of_two(1000); // Returns 1024 (2^10)
 * size_t p4 = math_next_power_of_two(0);    // Returns 1 (2^0)
 * @endcode
 *
 * @ingroup util
 */
static inline size_t math_next_power_of_two(size_t n) {
  if (n == 0)
    return 1;
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  if (sizeof(size_t) > 4) {
    n |= n >> 32;
  }
  return n + 1;
}
