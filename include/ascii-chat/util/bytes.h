#pragma once

/**
 * @file util/bytes.h
 * @brief 🔢 Byte-Level Access and Arithmetic Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides low-level utilities for safely reading and writing
 * multi-byte values from/to unaligned memory addresses, with overflow detection
 * for size calculations.
 *
 * CORE FEATURES:
 * ==============
 * - Safe unaligned memory access (16-bit and 32-bit values)
 * - Safe size_t multiplication with overflow detection
 * - Platform-safe byte manipulation (uses memcpy for portability)
 *
 * UNALIGNED ACCESS:
 * =================
 * Direct pointer casts like *(uint32_t*)ptr cause undefined behavior on
 * architectures requiring aligned access (ARM, SPARC, etc.). These functions
 * use memcpy which compilers optimize to single instructions when possible
 * while remaining safe on all platforms.
 *
 * @note These functions are used extensively in network and crypto modules
 *       for parsing and generating packets.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

/* ============================================================================
 * Unaligned Memory Access Helpers
 * ============================================================================
 * These functions safely read/write multi-byte values from/to potentially
 * unaligned memory addresses.
 */

/**
 * @brief Read a 16-bit value from potentially unaligned memory
 * @param ptr Pointer to memory (may be unaligned)
 * @return 16-bit value in host byte order
 *
 * Safely reads a 16-bit unsigned integer from any memory location,
 * even if the address is not aligned on a 2-byte boundary. Uses memcpy
 * which compilers optimize to single instructions when possible while
 * maintaining safety on all platforms.
 *
 * @par Example
 * @code
 * const uint8_t buffer[4] = {0x12, 0x34, 0x56, 0x78};
 * uint16_t value = bytes_read_u16_unaligned(&buffer[1]);
 * // Returns: 0x3412 (little-endian) or 0x3456 (big-endian, platform-dependent)
 * @endcode
 */
static inline uint16_t bytes_read_u16_unaligned(const void *ptr) {
  uint16_t value;
  __builtin_memcpy(&value, ptr, sizeof(value));
  return value;
}

/**
 * @brief Read a 32-bit value from potentially unaligned memory
 * @param ptr Pointer to memory (may be unaligned)
 * @return 32-bit value in host byte order
 *
 * Safely reads a 32-bit unsigned integer from any memory location,
 * even if the address is not aligned on a 4-byte boundary. Uses memcpy
 * which compilers optimize to single instructions when possible while
 * maintaining safety on all platforms.
 *
 * @par Example
 * @code
 * const uint8_t buffer[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
 * uint32_t value = bytes_read_u32_unaligned(&buffer[1]);
 * // Returns: 0x04030201 (little-endian) or similar, platform-dependent
 * @endcode
 */
static inline uint32_t bytes_read_u32_unaligned(const void *ptr) {
  uint32_t value;
  __builtin_memcpy(&value, ptr, sizeof(value));
  return value;
}

/**
 * @brief Write a 16-bit value to potentially unaligned memory
 * @param ptr Pointer to memory (may be unaligned)
 * @param value 16-bit value in host byte order
 *
 * Safely writes a 16-bit unsigned integer to any memory location,
 * even if the address is not aligned on a 2-byte boundary. Uses memcpy
 * which compilers optimize to single instructions when possible while
 * maintaining safety on all platforms.
 *
 * @par Example
 * @code
 * uint8_t buffer[4];
 * bytes_write_u16_unaligned(&buffer[1], 0x3412);
 * // buffer now contains: [??, 0x12, 0x34, ??]
 * @endcode
 */
static inline void bytes_write_u16_unaligned(void *ptr, uint16_t value) {
  __builtin_memcpy(ptr, &value, sizeof(value));
}

/**
 * @brief Write a 32-bit value to potentially unaligned memory
 * @param ptr Pointer to memory (may be unaligned)
 * @param value 32-bit value in host byte order
 *
 * Safely writes a 32-bit unsigned integer to any memory location,
 * even if the address is not aligned on a 4-byte boundary. Uses memcpy
 * which compilers optimize to single instructions when possible while
 * maintaining safety on all platforms.
 *
 * @par Example
 * @code
 * uint8_t buffer[6];
 * bytes_write_u32_unaligned(&buffer[1], 0x04030201);
 * // buffer[1..4] now contains: 0x01, 0x02, 0x03, 0x04 (little-endian)
 * @endcode
 */
static inline void bytes_write_u32_unaligned(void *ptr, uint32_t value) {
  __builtin_memcpy(ptr, &value, sizeof(value));
}

/* ============================================================================
 * Big-Endian (Network Byte Order) Access
 * ============================================================================
 * These functions read/write values in big-endian byte order, commonly used
 * for network protocols and SSH agent communication.
 */

/**
 * @brief Read a 32-bit big-endian value from memory
 * @param ptr Pointer to 4 bytes of data in big-endian order
 * @return 32-bit value in host byte order
 *
 * Reads a big-endian (network byte order) 32-bit integer from memory.
 * Used for parsing SSH agent protocol and other network protocols.
 *
 * @par Example
 * @code
 * const uint8_t buffer[4] = {0x00, 0x00, 0x00, 0x2A};
 * uint32_t value = read_u32_be(buffer);  // Returns 42
 * @endcode
 */
static inline uint32_t read_u32_be(const uint8_t *ptr) {
  return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3];
}

/**
 * @brief Write a 32-bit value in big-endian byte order to memory
 * @param ptr Pointer to 4 bytes of output memory
 * @param value 32-bit value in host byte order
 *
 * Writes a 32-bit integer to memory in big-endian (network byte order).
 * Used for constructing SSH agent protocol messages and other network protocols.
 *
 * @par Example
 * @code
 * uint8_t buffer[4];
 * write_u32_be(buffer, 42);
 * // buffer now contains: {0x00, 0x00, 0x00, 0x2A}
 * @endcode
 */
static inline void write_u32_be(uint8_t *ptr, uint32_t value) {
  ptr[0] = (uint8_t)(value >> 24);
  ptr[1] = (uint8_t)(value >> 16);
  ptr[2] = (uint8_t)(value >> 8);
  ptr[3] = (uint8_t)(value);
}

/* ============================================================================
 * Safe Arithmetic Functions
 * ============================================================================
 */

/**
 * @brief Safe size_t multiplication with overflow detection
 * @param a First operand
 * @param b Second operand
 * @param result Pointer to output variable for the product
 * @return true if overflow occurred or result is NULL, false on success
 *
 * Safely multiplies two size_t values and detects overflow. Returns true
 * if overflow is detected (product set to 0) or if result pointer is NULL.
 * Returns false on successful multiplication.
 *
 * **IMPORTANT: Return value semantics are counter-intuitive!**
 * - Returns TRUE if overflow occurred OR result pointer is NULL (ERROR case)
 * - Returns FALSE on successful multiplication (SUCCESS case)
 *
 * This unusual semantics allows the common pattern:
 *   if (safe_size_mul(width, height, &product)) { ... } // handle error
 *
 * @par Example
 * @code
 * // Successful case
 * size_t product;
 * if (!safe_size_mul(100, 200, &product)) {
 *   // product == 20000, all good
 * } else {
 *   // overflow or NULL pointer
 * }
 *
 * // Overflow case
 * size_t large1 = SIZE_MAX;
 * size_t large2 = 2;
 * if (safe_size_mul(large1, large2, &product)) {
 *   // Overflow detected! product was set to 0
 * }
 * @endcode
 */
static inline bool bytes_safe_size_mul(size_t a, size_t b, size_t *result) {
  if (result == NULL) {
    return true; // ERROR: NULL pointer
  }

  if (a != 0 && b > SIZE_MAX / a) {
    *result = 0;
    return true; // ERROR: Overflow detected
  }

  *result = a * b;
  return false; // SUCCESS
}

/** @} */
