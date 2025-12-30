#pragma once

/**
 * @file util/overflow.h
 * @brief ✅ Safe Integer Arithmetic and Overflow Detection
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * Safe arithmetic operations that detect and handle integer overflow/underflow
 * without undefined behavior. Essential for buffer size calculations and
 * dimension validations to prevent security issues and crashes.
 *
 * CORE FEATURES:
 * ==============
 * - Safe multiplication with overflow detection
 * - Safe addition with overflow detection
 * - Checked size calculations (width * height * depth)
 * - Clear error reporting
 *
 * MOTIVATION:
 * ===========
 * Integer overflow in buffer size calculations is a common source of:
 * - Buffer overflows (security vulnerability)
 * - Crashes due to undersized allocations
 * - Silent memory corruption
 *
 * USAGE:
 * ======
 * // Safe multiplication
 * size_t result;
 * if (checked_size_mul(width, height, &result) != ASCIICHAT_OK) {
 *     log_error("Dimension overflow: %zu x %zu", width, height);
 *     return ERROR_OVERFLOW;
 * }
 * uint8_t *buffer = SAFE_MALLOC(result, uint8_t *);
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include "../common.h"

/* ============================================================================
 * Overflow Prediction Functions
 * ============================================================================
 */

/**
 * @brief Predict if multiplication would overflow
 * @param a First operand
 * @param b Second operand
 * @return true if a * b would overflow size_t, false otherwise
 *
 * Determines if multiplying two size_t values would overflow without
 * actually performing the multiplication or triggering undefined behavior.
 *
 * @par Example
 * @code
 * if (size_mul_would_overflow(1920, 1080)) {
 *     log_error("Image too large");
 *     return ERROR_OVERFLOW;
 * }
 * @endcode
 *
 * @ingroup util
 */
static inline bool size_mul_would_overflow(size_t a, size_t b) {
  // Handle zero case (no overflow)
  if (a == 0 || b == 0)
    return false;

  // Check if a * b would exceed SIZE_MAX
  // This is safe: if a != 0, we can divide
  return b > SIZE_MAX / a;
}

/**
 * @brief Predict if addition would overflow
 * @param a First operand
 * @param b Second operand
 * @return true if a + b would overflow size_t, false otherwise
 *
 * Determines if adding two size_t values would overflow.
 *
 * @par Example
 * @code
 * if (size_add_would_overflow(buffer_size, extra_bytes)) {
 *     log_error("Total size too large");
 *     return ERROR_OVERFLOW;
 * }
 * @endcode
 *
 * @ingroup util
 */
static inline bool size_add_would_overflow(size_t a, size_t b) {
  return b > SIZE_MAX - a;
}

/* ============================================================================
 * Checked Arithmetic Operations
 * ============================================================================
 */

/**
 * @brief Safely multiply two size_t values
 * @param a First operand
 * @param b Second operand
 * @param result Pointer to store result (only valid if function returns ASCIICHAT_OK)
 * @return ASCIICHAT_OK on success, ERROR_OVERFLOW if multiplication would overflow
 *
 * Multiplies two size_t values, returning the result through the output
 * parameter and an error code indicating success or overflow.
 *
 * @par Example
 * @code
 * size_t buffer_size;
 * asciichat_error_t err = checked_size_mul(width, height, &buffer_size);
 * if (err != ASCIICHAT_OK) {
 *     return err;  // Overflow detected
 * }
 * uint8_t *buffer = SAFE_MALLOC(buffer_size, uint8_t *);
 * @endcode
 *
 * @note result is only valid when function returns ASCIICHAT_OK
 *
 * @ingroup util
 */
static inline asciichat_error_t checked_size_mul(size_t a, size_t b, size_t *result) {
  if (size_mul_would_overflow(a, b)) {
    return 1; // ERROR_OVERFLOW - see asciichat_errno.h for exact value
  }
  *result = a * b;
  return 0; // ASCIICHAT_OK
}

/**
 * @brief Safely add two size_t values
 * @param a First operand
 * @param b Second operand
 * @param result Pointer to store result (only valid if function returns ASCIICHAT_OK)
 * @return ASCIICHAT_OK on success, ERROR_OVERFLOW if addition would overflow
 *
 * Adds two size_t values, returning the result through the output
 * parameter and an error code indicating success or overflow.
 *
 * @par Example
 * @code
 * size_t total_size;
 * asciichat_error_t err = checked_size_add(header_size, payload_size, &total_size);
 * if (err != ASCIICHAT_OK) {
 *     return err;  // Overflow detected
 * }
 * @endcode
 *
 * @ingroup util
 */
static inline asciichat_error_t checked_size_add(size_t a, size_t b, size_t *result) {
  if (size_add_would_overflow(a, b)) {
    return 1; // ERROR_OVERFLOW
  }
  *result = a + b;
  return 0; // ASCIICHAT_OK
}

/**
 * @brief Safely multiply three size_t values (common for 3D calculations)
 * @param width First dimension
 * @param height Second dimension
 * @param depth Third dimension (bytes per pixel, etc.)
 * @param result Pointer to store result (only valid if function returns ASCIICHAT_OK)
 * @return ASCIICHAT_OK on success, ERROR_OVERFLOW if any multiplication would overflow
 *
 * Common operation for calculating buffer sizes from dimensions:
 * buffer_size = width * height * depth
 *
 * @par Example
 * @code
 * // Calculate RGB image buffer size: width * height * 3
 * size_t buffer_size;
 * asciichat_error_t err = checked_size_mul3(width, height, 3, &buffer_size);
 * if (err != ASCIICHAT_OK) {
 *     log_error("Image dimensions %zu x %zu x 3 too large", width, height);
 *     return err;
 * }
 * @endcode
 *
 * @ingroup util
 */
static inline asciichat_error_t checked_size_mul3(size_t width, size_t height, size_t depth, size_t *result) {
  // First multiply width * height
  if (size_mul_would_overflow(width, height)) {
    return 1; // ERROR_OVERFLOW
  }
  size_t intermediate = width * height;

  // Then multiply result * depth
  if (size_mul_would_overflow(intermediate, depth)) {
    return 1; // ERROR_OVERFLOW
  }
  *result = intermediate * depth;
  return 0; // ASCIICHAT_OK
}

/* ============================================================================
 * Unchecked Variants (For Performance-Critical Code)
 * ============================================================================
 */

/**
 * @brief Multiply with overflow check inline (macro form for tight loops)
 * @param a First operand
 * @param b Second operand
 * @return Product if no overflow, SIZE_MAX if would overflow
 *
 * Macro version of checked multiplication that returns SIZE_MAX on overflow.
 * Useful in performance-critical loops where you want to avoid function calls.
 *
 * @warning The result SIZE_MAX might be a valid product. Use the function
 *          version (checked_size_mul) for proper error handling in most code.
 *
 * @ingroup util
 */
#define SIZE_MUL_SAFE(a, b) (size_mul_would_overflow(a, b) ? SIZE_MAX : (a) * (b))

/**
 * @brief Add with overflow check inline (macro form for tight loops)
 * @param a First operand
 * @param b Second operand
 * @return Sum if no overflow, SIZE_MAX if would overflow
 *
 * Macro version of checked addition that returns SIZE_MAX on overflow.
 *
 * @warning The result SIZE_MAX might be a valid sum. Use the function
 *          version (checked_size_add) for proper error handling in most code.
 *
 * @ingroup util
 */
#define SIZE_ADD_SAFE(a, b) (size_add_would_overflow(a, b) ? SIZE_MAX : (a) + (b))

/** @} */
