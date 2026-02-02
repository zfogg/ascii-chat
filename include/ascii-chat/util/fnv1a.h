#pragma once

/**
 * @file util/fnv1a.h
 * @brief #️⃣ FNV-1a Hash Function Implementation
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides a shared FNV-1a hash function implementation used
 * throughout the codebase for consistent hashing with UBSan-safe arithmetic.
 *
 * FNV-1a (Fowler-Noll-Vo) is a fast, non-cryptographic hash function with
 * good distribution properties. This implementation uses 64-bit arithmetic
 * to avoid unsigned overflow, then masks to 32-bit for the final result.
 *
 * All functions are static inline for performance, allowing the compiler
 * to inline them at call sites.
 *
 * @note This hash function is NOT for cryptographic purposes.
 * @note All arithmetic is done in 64-bit space to avoid UBSan warnings.
 */

#include <stddef.h>
#include <stdint.h>

// NOTE: This is a low-level hash utility. To avoid circular dependencies with
// asciichat_errno.h and common.h, we don't log errors here. Invalid inputs return 0.

/* ============================================================================
 * FNV-1a Constants
 * ============================================================================ */

/** @brief FNV-1a 32-bit offset basis */
#define FNV1A_32_OFFSET_BASIS 2166136261ULL

/** @brief FNV-1a 32-bit prime */
#define FNV1A_32_PRIME 16777619ULL

/** @brief FNV-1a 32-bit mask */
#define FNV1A_32_MASK 0xFFFFFFFFULL

/* ============================================================================
 * Macros
 * ============================================================================ */

/** @brief FNV-1a 32-bit hash macro */
#define FNV1A_INIT(hash) uint64_t hash = FNV1A_32_OFFSET_BASIS;

/** @brief FNV-1a 32-bit hash macro for a single byte */
#define FNV1A_32_HASH(hash, byte)                                                                                      \
  do {                                                                                                                 \
    (hash) = (((hash) ^ (uint64_t)(byte)) * FNV1A_32_PRIME) & FNV1A_32_MASK;                                           \
  } while (0)

/* ============================================================================
 * FNV-1a Hash Functions
 * ============================================================================ */

/**
 * @brief Hash a byte array using FNV-1a
 * @param data Pointer to the data to hash
 * @param len Length of the data in bytes
 * @return 32-bit hash value
 *
 * This function hashes arbitrary byte data using the FNV-1a algorithm.
 * It uses 64-bit arithmetic to avoid overflow, then masks to 32-bit.
 *
 * @ingroup util
 */
static inline uint32_t fnv1a_hash_bytes(const void *data, size_t len) {
  // Return 0 for invalid input (no error logging to avoid circular dependencies)
  if (!data || len == 0) {
    return 0;
  }

  uint64_t hash = FNV1A_32_OFFSET_BASIS;
  const unsigned char *bytes = (const unsigned char *)data;
  const unsigned char *end = bytes + len;

  while (bytes < end) {
    uint64_t byte = (uint64_t)*bytes++;
    hash = ((hash ^ byte) * FNV1A_32_PRIME) & FNV1A_32_MASK;
  }

  return (uint32_t)hash;
}

/**
 * @brief Hash a null-terminated string using FNV-1a
 * @param str Null-terminated string to hash
 * @return 32-bit hash value, or 0 if str is NULL
 *
 * This function hashes a null-terminated string using FNV-1a.
 * It includes the null terminator in the hash calculation.
 *
 * @ingroup util
 */
static inline uint32_t fnv1a_hash_string(const char *str) {
  // Return 0 for invalid input (no error logging to avoid circular dependencies)
  if (!str) {
    return 0;
  }

  uint64_t hash = FNV1A_32_OFFSET_BASIS;
  const unsigned char *p = (const unsigned char *)str;

  while (*p) {
    uint64_t byte = (uint64_t)*p++;
    hash = ((hash ^ byte) * FNV1A_32_PRIME) & FNV1A_32_MASK;
  }

  return (uint32_t)hash;
}

/**
 * @brief Hash a 32-bit integer using FNV-1a
 * @param value 32-bit integer to hash
 * @return 32-bit hash value
 *
 * This function hashes a 32-bit integer by hashing each byte.
 * Optimized for integers by hashing bytes directly instead of using a loop.
 *
 * @ingroup util
 */
static inline uint32_t fnv1a_hash_uint32(uint32_t value) {
  uint64_t hash = FNV1A_32_OFFSET_BASIS;

  // Hash each byte of the 32-bit value
  FNV1A_32_HASH(hash, (value >> 0) & FNV1A_32_MASK);
  FNV1A_32_HASH(hash, (value >> 8) & FNV1A_32_MASK);
  FNV1A_32_HASH(hash, (value >> 16) & FNV1A_32_MASK);
  FNV1A_32_HASH(hash, (value >> 24) & FNV1A_32_MASK);

  return (uint32_t)hash;
}

/**
 * @brief Hash a 64-bit integer using FNV-1a
 * @param value 64-bit integer to hash
 * @return 32-bit hash value
 *
 * This function hashes a 64-bit integer by hashing each byte.
 * Optimized for integers by hashing bytes directly instead of using a loop.
 *
 * @ingroup util
 */
static inline uint32_t fnv1a_hash_uint64(uint64_t value) {
  uint64_t hash = FNV1A_32_OFFSET_BASIS;

  // Hash each byte of the 64-bit value
  for (int i = 0; i < 8; i++) {
    uint64_t byte = (value >> (i * 8)) & FNV1A_32_MASK;
    hash = ((hash ^ byte) * FNV1A_32_PRIME) & FNV1A_32_MASK;
  }

  return (uint32_t)hash;
}

/** @} */