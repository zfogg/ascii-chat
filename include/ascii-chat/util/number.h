#pragma once

/**
 * @file util/number.h
 * @brief 🔢 Number Formatting and Conversion Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * Utilities for converting numbers to strings and calculating digit counts.
 * Designed for minimal overhead, suitable for real-time video processing.
 *
 * CORE FEATURES:
 * ==============
 * - Digit counting for integers (no string allocation needed)
 * - Decimal number to string conversion
 * - Byte-to-string formatting (1-3 digits)
 * - Fast and allocation-free implementations
 *
 * USAGE:
 * ======
 * // Count digits needed for allocation
 * int digits = digits_u32(12345);  // Returns 5
 * char buffer[digits];
 * size_t len = write_decimal(12345, buffer);
 *
 * // Format u8 into ANSI sequence
 * char ansi_seq[3];
 * char *end = write_u8(ansi_seq, 255);  // Write "255"
 * // ansi_seq now contains "255", end points after last char
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Digit Counting
 * ============================================================================
 */

/**
 * @brief Calculate number of decimal digits in a 32-bit unsigned integer
 * @param v Value to measure
 * @return Number of decimal digits (minimum 1)
 *
 * Returns the number of digits needed to represent the value in decimal.
 * Useful for pre-allocating buffers or calculating string lengths without
 * actually converting the number.
 *
 * @par Example
 * @code
 * int digits = digits_u32(12345);  // Returns 5
 * int digits = digits_u32(0);       // Returns 1
 * int digits = digits_u32(1000000000);  // Returns 10
 * @endcode
 *
 * @ingroup util
 */
static inline int digits_u32(uint32_t v) {
  if (v >= 1000000000u)
    return 10;
  if (v >= 100000000u)
    return 9;
  if (v >= 10000000u)
    return 8;
  if (v >= 1000000u)
    return 7;
  if (v >= 100000u)
    return 6;
  if (v >= 10000u)
    return 5;
  if (v >= 1000u)
    return 4;
  if (v >= 100u)
    return 3;
  if (v >= 10u)
    return 2;
  return 1;
}

/**
 * @brief Calculate number of decimal digits in a 16-bit unsigned integer
 * @param v Value to measure
 * @return Number of decimal digits (minimum 1)
 *
 * Returns the number of digits needed to represent the value in decimal.
 *
 * @par Example
 * @code
 * int digits = digits_u16(1234);   // Returns 4
 * int digits = digits_u16(0);      // Returns 1
 * int digits = digits_u16(65535);  // Returns 5
 * @endcode
 *
 * @ingroup util
 */
static inline int digits_u16(uint16_t v) {
  if (v >= 10000u)
    return 5;
  if (v >= 1000u)
    return 4;
  if (v >= 100u)
    return 3;
  if (v >= 10u)
    return 2;
  return 1;
}

/**
 * @brief Calculate number of decimal digits in an 8-bit unsigned integer
 * @param v Value to measure
 * @return Number of decimal digits (minimum 1)
 *
 * Returns the number of digits needed to represent the value in decimal.
 *
 * @par Example
 * @code
 * int digits = digits_u8(123);  // Returns 3
 * int digits = digits_u8(0);    // Returns 1
 * int digits = digits_u8(255);  // Returns 3
 * @endcode
 *
 * @ingroup util
 */
static inline int digits_u8(uint8_t v) {
  if (v >= 100u)
    return 3;
  if (v >= 10u)
    return 2;
  return 1;
}

/* ============================================================================
 * Decimal Conversion
 * ============================================================================
 */

/**
 * @brief Write an integer as decimal digits to buffer
 * @param value Integer value to write (non-negative)
 * @param dst Output buffer (must have space for at least 10 digits)
 * @return Number of characters written
 *
 * Converts an integer to its decimal string representation and writes to
 * the buffer. Only writes digits, no null terminator.
 *
 * @note Buffer must be large enough for the value. Use digits_u32() to
 *       calculate required size if needed.
 * @note This function does NOT null-terminate the output. Caller must do that.
 * @note Returns 0 and doesn't write anything for negative values (implementation detail).
 *
 * @par Example
 * @code
 * char buffer[10];
 * size_t len = write_decimal(12345, buffer);  // len = 5
 * // buffer[0..4] = "12345", not null-terminated
 * @endcode
 *
 * @ingroup util
 */
static inline size_t write_decimal(int value, char *dst) {
  if (value == 0) {
    *dst = '0';
    return 1;
  }

  if (value < 0) {
    // For negative numbers, just return without writing
    // (not typically used in this codebase but handle gracefully)
    return 0;
  }

  char temp[10]; // Enough for 32-bit int
  int pos = 0;
  int v = value;

  while (v > 0) {
    temp[pos++] = '0' + (v % 10);
    v /= 10;
  }

  // Reverse digits into dst
  for (int i = 0; i < pos; i++) {
    dst[i] = temp[pos - 1 - i];
  }

  return (size_t)pos;
}

/* ============================================================================
 * Byte Formatting (for ANSI sequences)
 * ============================================================================
 */

/**
 * @brief Write an 8-bit unsigned integer as decimal digits
 * @param p Output buffer pointer
 * @param n Byte value to write (0-255)
 * @return Pointer past the last written character
 *
 * Writes a byte as decimal digits (1-3 characters) and returns pointer
 * to position after the last written character. Designed for building
 * ANSI escape sequences where numbers need to be embedded.
 *
 * @par Example
 * @code
 * char buffer[3];
 * char *end = write_u8(buffer, 128);
 * // buffer = "128", end = buffer + 3
 *
 * char small[1];
 * char *end = write_u8(small, 5);
 * // buffer = "5", end = small + 1
 * @endcode
 *
 * @ingroup util
 */
static inline char *write_u8(char *p, uint8_t n) {
  if (n < 10) {
    *p++ = '0' + n;
  } else if (n < 100) {
    *p++ = '0' + (n / 10);
    *p++ = '0' + (n % 10);
  } else {
    *p++ = '0' + (n / 100);
    *p++ = '0' + ((n / 10) % 10);
    *p++ = '0' + (n % 10);
  }
  return p;
}

/**
 * @brief Write a 16-bit unsigned integer as decimal digits
 * @param p Output buffer pointer
 * @param n Word value to write (0-65535)
 * @return Pointer past the last written character
 *
 * Writes a 16-bit value as decimal digits (1-5 characters) and returns
 * pointer to position after the last written character.
 *
 * @par Example
 * @code
 * char buffer[5];
 * char *end = write_u16(buffer, 12345);
 * // buffer = "12345", end = buffer + 5
 * @endcode
 *
 * @ingroup util
 */
static inline char *write_u16(char *p, uint16_t n) {
  char temp[5];
  int pos = 0;
  uint16_t v = n;

  if (v == 0) {
    *p++ = '0';
    return p;
  }

  while (v > 0) {
    temp[pos++] = '0' + (v % 10);
    v /= 10;
  }

  // Reverse digits
  for (int i = pos - 1; i >= 0; i--) {
    *p++ = temp[i];
  }

  return p;
}

/**
 * @brief Write a 32-bit unsigned integer as decimal digits
 * @param p Output buffer pointer
 * @param n Double word value to write (0-4294967295)
 * @return Pointer past the last written character
 *
 * Writes a 32-bit value as decimal digits (1-10 characters) and returns
 * pointer to position after the last written character.
 *
 * @par Example
 * @code
 * char buffer[10];
 * char *end = write_u32(buffer, 1234567890);
 * // buffer = "1234567890", end = buffer + 10
 * @endcode
 *
 * @ingroup util
 */
static inline char *write_u32(char *p, uint32_t n) {
  char temp[10];
  int pos = 0;
  uint32_t v = n;

  if (v == 0) {
    *p++ = '0';
    return p;
  }

  while (v > 0) {
    temp[pos++] = '0' + (v % 10);
    v /= 10;
  }

  // Reverse digits
  for (int i = pos - 1; i >= 0; i--) {
    *p++ = temp[i];
  }

  return p;
}

/** @} */
