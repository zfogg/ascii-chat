#pragma once

/**
 * @file image2ascii/simd/common.h
 * @ingroup image2ascii
 * @brief Common SIMD utilities and structures
 *
 * This header provides common utilities, cache systems, and helper functions
 * shared across all SIMD implementations.
 *
 * The interface provides:
 * - UTF-8 character caching system
 * - Cache eviction algorithms
 * - Architecture capability detection
 * - ANSI color sequence generation
 * - Helper functions for decimal conversion
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "util/uthash.h"
#include "../image.h"

/** @brief RGB pixel type alias */
typedef rgb_t rgb_pixel_t;

/** @brief Ramp64 cache size (64 entries) */
#define RAMP64_SIZE 64

/**
 * @name Cache Eviction Algorithm Parameters
 * @{
 */

/** @brief Frequency bonus decay time (5 minutes in seconds) */
#define CACHE_FREQUENCY_DECAY_TIME 300.0
/** @brief Recency importance scale (1 minute in seconds) */
#define CACHE_RECENCY_SCALE 60.0
/** @brief Recent access protection threshold (10 seconds) */
#define CACHE_RECENT_ACCESS_THRESHOLD 10
/** @brief Maximum cache lifetime (1 hour in seconds) */
#define CACHE_MAX_LIFETIME 3600
/** @brief Minimum accesses for frequency protection */
#define CACHE_MIN_ACCESS_THRESHOLD 3

/** @} */

/**
 * @brief Build 64-entry character ramp cache
 * @param ramp64 Output array for ramp indices
 * @param ascii_chars Character palette string
 *
 * @ingroup image2ascii
 */
void build_ramp64(uint8_t ramp64[RAMP64_SIZE], const char *ascii_chars);

/**
 * @brief Calculate cache eviction score
 * @param last_access_time Last access timestamp (nanoseconds)
 * @param access_count Total access count
 * @param creation_time Cache creation timestamp (nanoseconds)
 * @param current_time Current timestamp (nanoseconds)
 * @return Eviction score (lower = more likely to evict)
 *
 * @ingroup image2ascii
 */
double calculate_cache_eviction_score(uint64_t last_access_time, uint32_t access_count, uint64_t creation_time,
                                      uint64_t current_time);

/**
 * @brief Get current time in nanoseconds
 * @return Current timestamp in nanoseconds since epoch
 *
 * @ingroup image2ascii
 */
uint64_t get_current_time_ns(void);

/**
 * @brief UTF-8 character structure
 *
 * @ingroup image2ascii
 */
typedef struct {
  char utf8_bytes[4]; /**< UTF-8 character bytes (up to 4) */
  uint8_t byte_len;   /**< Actual byte length (1-4) */
} utf8_char_t;

/**
 * @brief UTF-8 palette cache structure
 *
 * Thread-safe cache system for UTF-8 character lookup with eviction tracking.
 *
 * @ingroup image2ascii
 */
typedef struct utf8_palette_cache_s {
  uint32_t key;                /**< Hash key for uthash lookup */
  utf8_char_t cache[256];      /**< 256-entry cache for direct luminance lookup (monochrome) */
  utf8_char_t cache64[64];     /**< 64-entry cache for SIMD color lookup */
  uint8_t char_index_ramp[64]; /**< Character indices for vqtbl4q_u8 lookup */
  char palette_hash[64];       /**< Hash of palette for cache validation */
  bool is_valid;               /**< Whether cache is valid */

  /** Thread-safe eviction tracking */
  _Atomic uint64_t last_access_time; /**< Nanoseconds since epoch (atomic) */
  _Atomic uint32_t access_count;     /**< Total access count (atomic) */
  uint64_t creation_time;            /**< When cache was created (immutable) */

  /** Min-heap eviction management (protected by write lock) */
  size_t heap_index;   /**< Position in min-heap (for O(log n) updates) */
  double cached_score; /**< Last calculated eviction score */

  /** uthash handle (required for hash table operations) */
  UT_hash_handle hh;
} utf8_palette_cache_t;

/**
 * @brief Get or create UTF-8 palette cache
 * @param ascii_chars Character palette string
 * @return Cache pointer (may be shared, do not free)
 *
 * Thread-safe cache lookup with automatic creation and eviction.
 *
 * @ingroup image2ascii
 */
utf8_palette_cache_t *get_utf8_palette_cache(const char *ascii_chars);

/**
 * @brief Build UTF-8 luminance cache
 * @param ascii_chars Character palette string
 * @param cache Output cache array (256 entries)
 *
 * @ingroup image2ascii
 */
void build_utf8_luminance_cache(const char *ascii_chars, utf8_char_t cache[256]);

/**
 * @brief Build UTF-8 ramp64 cache
 * @param ascii_chars Character palette string
 * @param cache64 Output cache array (64 entries)
 * @param char_index_ramp Output character index ramp (64 entries)
 *
 * @ingroup image2ascii
 */
void build_utf8_ramp64_cache(const char *ascii_chars, utf8_char_t cache64[64], uint8_t char_index_ramp[64]);

// Note: char_index_ramp_cache removed - data already available in utf8_palette_cache_t.char_index_ramp[64]

// Fast palette hash function - no caching needed (djb2 hash is fast enough)
// Note: Palette hash caching was removed as djb2 hash (~100-200ns) is negligible
// compared to frame rendering costs (~16.7ms budget)

/**
 * @brief Destroy all SIMD caches
 *
 * Cleans up all caches and frees associated memory.
 *
 * @ingroup image2ascii
 */
void simd_caches_destroy_all(void);

/**
 * @brief Check if SSE2 is supported
 * @return true if SSE2 is available, false otherwise
 *
 * @ingroup image2ascii
 */
bool has_sse2_support(void);

/**
 * @brief Check if SSSE3 is supported
 * @return true if SSSE3 is available, false otherwise
 *
 * @ingroup image2ascii
 */
bool has_ssse3_support(void);

/**
 * @brief Check if AVX2 is supported
 * @return true if AVX2 is available, false otherwise
 *
 * @ingroup image2ascii
 */
bool has_avx2_support(void);

/**
 * @brief Check if NEON is supported
 * @return true if NEON is available, false otherwise
 *
 * @ingroup image2ascii
 */
bool has_neon_support(void);

/**
 * @brief Check if SVE is supported
 * @return true if SVE is available, false otherwise
 *
 * @ingroup image2ascii
 */
bool has_sve_support(void);

/**
 * @brief Append truecolor foreground SGR sequence
 * @param dst Destination buffer pointer
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Append truecolor background SGR sequence
 * @param dst Destination buffer pointer
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Append truecolor foreground and background SGR sequence
 * @param dst Destination buffer pointer
 * @param fr Foreground red component (0-255)
 * @param fg Foreground green component (0-255)
 * @param fb Foreground blue component (0-255)
 * @param br Background red component (0-255)
 * @param bg Background green component (0-255)
 * @param bb Background blue component (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb);

/**
 * @brief Append ANSI reset sequence
 * @param dst Destination buffer pointer
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_sgr_reset(char *dst);

/**
 * @brief Write decimal RGB triplet using dec3 cache
 * @param value RGB component value (0-255)
 * @param dst Destination buffer pointer
 * @return Number of bytes written
 *
 * @note Defined in ascii_simd.c to avoid circular dependencies.
 *
 * @ingroup image2ascii
 */
size_t write_rgb_triplet(uint8_t value, char *dst);

/**
 * @brief Write decimal number to buffer
 * @param value Integer value to write
 * @param dst Destination buffer pointer
 * @return Number of bytes written
 *
 * Simple decimal writer for REP counts (can be larger than 255).
 *
 * @ingroup image2ascii
 */
static inline size_t write_decimal(int value, char *dst) {
  if (value == 0) {
    *dst = '0';
    return 1;
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

  return pos;
}

// Fast decimal for REP counts - now in output_buffer.h
