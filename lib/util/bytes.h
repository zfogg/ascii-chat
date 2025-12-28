#pragma once

/**
 * @file util/bytes.h
 * @brief 🔄 Byte Serialization and Deserialization Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * Portable utilities for reading and writing multi-byte integers with
 * specified endianness. Eliminates manual bit-shifting and makes code
 * clearer and less error-prone.
 *
 * CORE FEATURES:
 * ==============
 * - Big-endian and little-endian serialization
 * - Type-safe integer reading/writing
 * - Support for 16-bit, 32-bit, and 64-bit integers
 *
 * USAGE:
 * ======
 * // Writing data to buffer
 * uint8_t buf[4];
 * write_u32_be(buf, 0x12345678);  // Buffer now contains: 12 34 56 78
 *
 * // Reading data from buffer
 * uint32_t value = read_u32_be(buf);  // Returns 0x12345678
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <string.h>

/* ============================================================================
 * Big-Endian (Network Byte Order) Operations
 * ============================================================================
 */

/**
 * @brief Write 16-bit unsigned integer as big-endian
 * @param buf Output buffer (must have at least 2 bytes)
 * @param value Value to write
 *
 * Writes a 16-bit unsigned integer to the buffer in big-endian byte order
 * (most significant byte first).
 *
 * @par Example
 * @code
 * uint8_t buf[2];
 * write_u16_be(buf, 0x1234);
 * // buf[0] = 0x12, buf[1] = 0x34
 * @endcode
 *
 * @ingroup util
 */
static inline void write_u16_be(uint8_t *buf, uint16_t value) {
  buf[0] = (uint8_t)((value >> 8) & 0xFF);
  buf[1] = (uint8_t)(value & 0xFF);
}

/**
 * @brief Read 16-bit unsigned integer as big-endian
 * @param buf Input buffer (must have at least 2 bytes)
 * @return The read value in host byte order
 *
 * Reads a 16-bit unsigned integer from the buffer assuming big-endian
 * byte order (most significant byte first).
 *
 * @par Example
 * @code
 * uint8_t buf[2] = {0x12, 0x34};
 * uint16_t value = read_u16_be(buf);  // Returns 0x1234
 * @endcode
 *
 * @ingroup util
 */
static inline uint16_t read_u16_be(const uint8_t *buf) {
  return ((uint16_t)buf[0] << 8) | buf[1];
}

/**
 * @brief Write 32-bit unsigned integer as big-endian
 * @param buf Output buffer (must have at least 4 bytes)
 * @param value Value to write
 *
 * Writes a 32-bit unsigned integer to the buffer in big-endian byte order.
 * This is the most commonly used function for network protocols.
 *
 * @par Example
 * @code
 * uint8_t buf[4];
 * write_u32_be(buf, 0x12345678);
 * // buf[0] = 0x12, buf[1] = 0x34, buf[2] = 0x56, buf[3] = 0x78
 * @endcode
 *
 * @ingroup util
 */
static inline void write_u32_be(uint8_t *buf, uint32_t value) {
  buf[0] = (uint8_t)((value >> 24) & 0xFF);
  buf[1] = (uint8_t)((value >> 16) & 0xFF);
  buf[2] = (uint8_t)((value >> 8) & 0xFF);
  buf[3] = (uint8_t)(value & 0xFF);
}

/**
 * @brief Read 32-bit unsigned integer as big-endian
 * @param buf Input buffer (must have at least 4 bytes)
 * @return The read value in host byte order
 *
 * Reads a 32-bit unsigned integer from the buffer assuming big-endian
 * byte order. This is the most commonly used function for network protocols.
 *
 * @par Example
 * @code
 * uint8_t buf[4] = {0x12, 0x34, 0x56, 0x78};
 * uint32_t value = read_u32_be(buf);  // Returns 0x12345678
 * @endcode
 *
 * @ingroup util
 */
static inline uint32_t read_u32_be(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | buf[3];
}

/**
 * @brief Write 64-bit unsigned integer as big-endian
 * @param buf Output buffer (must have at least 8 bytes)
 * @param value Value to write
 *
 * Writes a 64-bit unsigned integer to the buffer in big-endian byte order.
 *
 * @par Example
 * @code
 * uint8_t buf[8];
 * write_u64_be(buf, 0x0102030405060708LL);
 * // buf[0..7] = 01 02 03 04 05 06 07 08
 * @endcode
 *
 * @ingroup util
 */
static inline void write_u64_be(uint8_t *buf, uint64_t value) {
  buf[0] = (uint8_t)((value >> 56) & 0xFF);
  buf[1] = (uint8_t)((value >> 48) & 0xFF);
  buf[2] = (uint8_t)((value >> 40) & 0xFF);
  buf[3] = (uint8_t)((value >> 32) & 0xFF);
  buf[4] = (uint8_t)((value >> 24) & 0xFF);
  buf[5] = (uint8_t)((value >> 16) & 0xFF);
  buf[6] = (uint8_t)((value >> 8) & 0xFF);
  buf[7] = (uint8_t)(value & 0xFF);
}

/**
 * @brief Read 64-bit unsigned integer as big-endian
 * @param buf Input buffer (must have at least 8 bytes)
 * @return The read value in host byte order
 *
 * Reads a 64-bit unsigned integer from the buffer assuming big-endian
 * byte order.
 *
 * @par Example
 * @code
 * uint8_t buf[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
 * uint64_t value = read_u64_be(buf);  // Returns 0x0102030405060708LL
 * @endcode
 *
 * @ingroup util
 */
static inline uint64_t read_u64_be(const uint8_t *buf) {
  return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
         ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
         ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
         ((uint64_t)buf[6] << 8) | buf[7];
}

/* ============================================================================
 * Little-Endian Operations
 * ============================================================================
 */

/**
 * @brief Write 16-bit unsigned integer as little-endian
 * @param buf Output buffer (must have at least 2 bytes)
 * @param value Value to write
 *
 * Writes a 16-bit unsigned integer to the buffer in little-endian byte order
 * (least significant byte first).
 *
 * @ingroup util
 */
static inline void write_u16_le(uint8_t *buf, uint16_t value) {
  buf[0] = (uint8_t)(value & 0xFF);
  buf[1] = (uint8_t)((value >> 8) & 0xFF);
}

/**
 * @brief Read 16-bit unsigned integer as little-endian
 * @param buf Input buffer (must have at least 2 bytes)
 * @return The read value in host byte order
 *
 * Reads a 16-bit unsigned integer from the buffer assuming little-endian
 * byte order.
 *
 * @ingroup util
 */
static inline uint16_t read_u16_le(const uint8_t *buf) {
  return buf[0] | ((uint16_t)buf[1] << 8);
}

/**
 * @brief Write 32-bit unsigned integer as little-endian
 * @param buf Output buffer (must have at least 4 bytes)
 * @param value Value to write
 *
 * Writes a 32-bit unsigned integer to the buffer in little-endian byte order.
 *
 * @ingroup util
 */
static inline void write_u32_le(uint8_t *buf, uint32_t value) {
  buf[0] = (uint8_t)(value & 0xFF);
  buf[1] = (uint8_t)((value >> 8) & 0xFF);
  buf[2] = (uint8_t)((value >> 16) & 0xFF);
  buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * @brief Read 32-bit unsigned integer as little-endian
 * @param buf Input buffer (must have at least 4 bytes)
 * @return The read value in host byte order
 *
 * Reads a 32-bit unsigned integer from the buffer assuming little-endian
 * byte order.
 *
 * @ingroup util
 */
static inline uint32_t read_u32_le(const uint8_t *buf) {
  return buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

/**
 * @brief Write 64-bit unsigned integer as little-endian
 * @param buf Output buffer (must have at least 8 bytes)
 * @param value Value to write
 *
 * Writes a 64-bit unsigned integer to the buffer in little-endian byte order.
 *
 * @ingroup util
 */
static inline void write_u64_le(uint8_t *buf, uint64_t value) {
  buf[0] = (uint8_t)(value & 0xFF);
  buf[1] = (uint8_t)((value >> 8) & 0xFF);
  buf[2] = (uint8_t)((value >> 16) & 0xFF);
  buf[3] = (uint8_t)((value >> 24) & 0xFF);
  buf[4] = (uint8_t)((value >> 32) & 0xFF);
  buf[5] = (uint8_t)((value >> 40) & 0xFF);
  buf[6] = (uint8_t)((value >> 48) & 0xFF);
  buf[7] = (uint8_t)((value >> 56) & 0xFF);
}

/**
 * @brief Read 64-bit unsigned integer as little-endian
 * @param buf Input buffer (must have at least 8 bytes)
 * @return The read value in host byte order
 *
 * Reads a 64-bit unsigned integer from the buffer assuming little-endian
 * byte order.
 *
 * @ingroup util
 */
static inline uint64_t read_u64_le(const uint8_t *buf) {
  return buf[0] | ((uint64_t)buf[1] << 8) | ((uint64_t)buf[2] << 16) |
         ((uint64_t)buf[3] << 24) | ((uint64_t)buf[4] << 32) |
         ((uint64_t)buf[5] << 40) | ((uint64_t)buf[6] << 48) |
         ((uint64_t)buf[7] << 56);
}

/** @} */
