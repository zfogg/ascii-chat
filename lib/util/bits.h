#pragma once

/**
 * @file util/bits.h
 * @brief 🔢 Bit Manipulation Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * Portable bit manipulation functions including power-of-two checks,
 * bit counting, and leading/trailing zero detection across platforms.
 *
 * CORE FEATURES:
 * ==============
 * - Power-of-two validation and rounding
 * - Find first/last set bit (platform-optimized)
 * - Bit counting operations
 *
 * PLATFORM SUPPORT:
 * =================
 * - GCC/Clang: Uses built-in intrinsics for optimal performance
 * - MSVC: Uses Windows intrinsic functions
 * - Fallback: Software implementations for all operations
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

/* ============================================================================
 * Power-of-Two Operations
 * ============================================================================
 */

/**
 * @brief Check if a number is a power of two
 * @param n Value to check
 * @return true if n is a power of two, false otherwise
 *
 * Determines if a value is a power of two using bitwise operations.
 * Zero is not considered a power of two.
 *
 * @par Example
 * @code
 * is_power_of_two(1);    // Returns true  (2^0)
 * is_power_of_two(2);    // Returns true  (2^1)
 * is_power_of_two(4);    // Returns true  (2^2)
 * is_power_of_two(3);    // Returns false
 * is_power_of_two(0);    // Returns false
 * @endcode
 *
 * @ingroup util
 */
static inline bool is_power_of_two(size_t n) {
  return n && !(n & (n - 1));
}

/**
 * @brief Round up to next power of two
 * @param n Value to round up
 * @return Smallest power of two greater than or equal to n
 *
 * Returns the smallest power of two that is greater than or equal to the
 * input value. Input of 0 returns 1, input of 1 returns 1, input of 3 returns 4.
 *
 * @par Example
 * @code
 * next_power_of_two(1);   // Returns 1
 * next_power_of_two(2);   // Returns 2
 * next_power_of_two(3);   // Returns 4
 * next_power_of_two(5);   // Returns 8
 * next_power_of_two(0);   // Returns 1
 * @endcode
 *
 * @ingroup util
 */
static inline size_t next_power_of_two(size_t n) {
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

/* ============================================================================
 * Leading/Trailing Zero Detection
 * ============================================================================
 */

/**
 * @brief Find position of first set bit (trailing zeros)
 * @param mask Bit mask to search
 * @return Position of first set bit (0-63), or 64 if no bits set
 *
 * Finds the position of the least significant set bit in the mask.
 * Uses platform-optimized intrinsics where available, with De Bruijn
 * sequence fallback for maximum portability.
 *
 * @par Example
 * @code
 * find_first_set_bit(0x01);  // Returns 0 (bit 0 set)
 * find_first_set_bit(0x02);  // Returns 1 (bit 1 set)
 * find_first_set_bit(0x04);  // Returns 2 (bit 2 set)
 * find_first_set_bit(0x00);  // Returns 64 (no bits set)
 * @endcode
 *
 * @note Performance: ~1 cycle on modern CPUs with CTZ support,
 *       ~4-5 cycles on platforms using De Bruijn fallback.
 *
 * @ingroup util
 */
static inline int find_first_set_bit(uint64_t mask) {
#if defined(__GNUC__) || defined(__clang__)
  // GCC/Clang builtin (fast hardware instruction on modern CPUs)
  if (mask == 0)
    return 64;
  return __builtin_ctzll(mask);
#elif defined(_MSC_VER) && defined(_M_X64)
  // Microsoft Visual C++ x64 intrinsic
  unsigned long index;
  if (_BitScanForward64(&index, mask)) {
    return (int)index;
  }
  return 64; // No bits set
#elif defined(_MSC_VER)
  // MSVC x86 fallback
  unsigned long index;
  if (_BitScanForward(&index, (unsigned long)mask)) {
    return (int)index;
  }
  if (_BitScanForward(&index, (unsigned long)(mask >> 32))) {
    return 32 + (int)index;
  }
  return 64; // No bits set
#else
  // De Bruijn sequence fallback: O(1) with ~4-5 CPU cycles
  // Portable and production-tested (used in LLVM, GCC)
  if (mask == 0)
    return 64;

  static const int debruijn_table[64] = {
      0,  1,  2,  53, 3,  7,  54, 27, 4,  38, 41, 8,  34, 55, 48, 28,
      62, 5,  39, 46, 44, 42, 22, 9,  24, 35, 59, 56, 49, 18, 29, 11,
      63, 52, 6,  26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
      51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12};

  // Isolate rightmost bit and hash using De Bruijn multiplication
  return debruijn_table[((mask & -mask) * 0x022fdd63cc95386dULL) >> 58];
#endif
}

/* ============================================================================
 * Bit Counting Operations
 * ============================================================================
 */

/**
 * @brief Count number of set bits (popcount)
 * @param x Value to count bits in
 * @return Number of set bits in x
 *
 * Counts the number of set bits (1s) in an unsigned 64-bit integer.
 * Uses platform-optimized intrinsics where available.
 *
 * @par Example
 * @code
 * count_set_bits(0x0F);  // Returns 4
 * count_set_bits(0xFF);  // Returns 8
 * count_set_bits(0x00);  // Returns 0
 * @endcode
 *
 * @ingroup util
 */
static inline int count_set_bits(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#elif defined(_MSC_VER) && defined(_M_X64)
  return (int)__popcnt64(x);
#else
  // Software fallback
  int count = 0;
  while (x) {
    count += x & 1;
    x >>= 1;
  }
  return count;
#endif
}

/** @} */
