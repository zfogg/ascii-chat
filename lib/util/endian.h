/**
 * @file util/endian.h
 * @ingroup util
 * @brief 🔄 Network byte order conversion helpers and data serialization
 *
 * Provides type-safe wrappers and helpers for network byte order conversions
 * and binary data packing/unpacking. These consolidate common patterns used
 * throughout the network packet handling code.
 */

#pragma once

#include <stdint.h>
#include <string.h>

// Include platform-specific network byte order headers
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

/**
 * @brief Pack a 16-bit value into network byte order
 * @param value Value to pack
 * @return Value in network byte order (big-endian)
 */
static inline uint16_t endian_pack_u16(uint16_t value) {
  return htons(value);
}

/**
 * @brief Unpack a 16-bit value from network byte order
 * @param value Value in network byte order
 * @return Value in host byte order
 */
static inline uint16_t endian_unpack_u16(uint16_t value) {
  return ntohs(value);
}

/**
 * @brief Pack a 32-bit value into network byte order
 * @param value Value to pack
 * @return Value in network byte order (big-endian)
 */
static inline uint32_t endian_pack_u32(uint32_t value) {
  return htonl(value);
}

/**
 * @brief Unpack a 32-bit value from network byte order
 * @param value Value in network byte order
 * @return Value in host byte order
 */
static inline uint32_t endian_unpack_u32(uint32_t value) {
  return ntohl(value);
}

/**
 * @brief Write 16-bit value to buffer in network byte order
 * @param buffer Target buffer (at least 2 bytes)
 * @param value Value to write
 *
 * Writes a 16-bit value to the buffer in network byte order (big-endian).
 */
static inline void endian_write_u16(uint8_t *buffer, uint16_t value) {
  uint16_t network_value = htons(value);
  memcpy(buffer, &network_value, sizeof(uint16_t));
}

/**
 * @brief Write 32-bit value to buffer in network byte order
 * @param buffer Target buffer (at least 4 bytes)
 * @param value Value to write
 *
 * Writes a 32-bit value to the buffer in network byte order (big-endian).
 */
static inline void endian_write_u32(uint8_t *buffer, uint32_t value) {
  uint32_t network_value = htonl(value);
  memcpy(buffer, &network_value, sizeof(uint32_t));
}

/**
 * @brief Read 16-bit value from buffer in network byte order
 * @param buffer Source buffer (at least 2 bytes)
 * @return Value in host byte order
 *
 * Reads a 16-bit value from the buffer, assuming network byte order (big-endian).
 */
static inline uint16_t endian_read_u16(const uint8_t *buffer) {
  uint16_t value;
  memcpy(&value, buffer, sizeof(uint16_t));
  return ntohs(value);
}

/**
 * @brief Read 32-bit value from buffer in network byte order
 * @param buffer Source buffer (at least 4 bytes)
 * @return Value in host byte order
 *
 * Reads a 32-bit value from the buffer, assuming network byte order (big-endian).
 */
static inline uint32_t endian_read_u32(const uint8_t *buffer) {
  uint32_t value;
  memcpy(&value, buffer, sizeof(uint32_t));
  return ntohl(value);
}

/**
 * @brief Write 64-bit value to buffer (assumes network byte order for 64-bit is two 32-bit big-endian values)
 * @param buffer Target buffer (at least 8 bytes)
 * @param value Value to write (as two consecutive 32-bit big-endian values)
 *
 * Writes a 64-bit value as two consecutive 32-bit network byte order values.
 */
static inline void endian_write_u64(uint8_t *buffer, uint64_t value) {
  endian_write_u32(buffer, (uint32_t)(value >> 32));
  endian_write_u32(buffer + 4, (uint32_t)(value & 0xFFFFFFFFU));
}

/**
 * @brief Read 64-bit value from buffer (assumes network byte order for 64-bit is two 32-bit big-endian values)
 * @param buffer Source buffer (at least 8 bytes)
 * @return Value reconstructed from two consecutive 32-bit big-endian values
 *
 * Reads a 64-bit value from two consecutive 32-bit network byte order values.
 */
static inline uint64_t endian_read_u64(const uint8_t *buffer) {
  uint32_t high = endian_read_u32(buffer);
  uint32_t low = endian_read_u32(buffer + 4);
  return ((uint64_t)high << 32) | low;
}

/**
 * @brief Check if system is little-endian
 * @return 1 if little-endian, 0 if big-endian
 */
static inline int endian_is_little(void) {
  uint16_t x = 0x0102;
  return *(uint8_t *)&x == 0x02;
}

/**
 * @brief Get a human-readable endianness string
 * @return "little-endian" or "big-endian"
 */
static inline const char *endian_name(void) {
  return endian_is_little() ? "little-endian" : "big-endian";
}
