#include "common.h"
#include "image2ascii/simd/common.h"
#include "hashtable.h"
#include "crc32_hw.h"
#include "ascii_simd.h"

// Include SIMD architecture headers for cleanup functions
#ifdef SIMD_SUPPORT_NEON
#include "neon.h"
#endif
#ifdef SIMD_SUPPORT_SSE2
#include "sse2.h"
#endif
#ifdef SIMD_SUPPORT_SSSE3
#include "ssse3.h"
#endif
#ifdef SIMD_SUPPORT_AVX2
#include "avx2.h"
#endif
#ifdef SIMD_SUPPORT_SVE
#include "sve.h"
#endif

// Build 64-entry glyph LUT for vqtbl4q_u8 and other architecture's instrinsics (UTF-8 aware)
void build_ramp64(uint8_t ramp64[RAMP64_SIZE], const char *ascii_chars) {
  if (!ascii_chars) {
    // Fallback to space character
    for (int i = 0; i < RAMP64_SIZE; i++) {
      ramp64[i] = ' ';
    }
    return;
  }

  // Build character boundary map for UTF-8 support
  // First, find all character start positions
  int char_starts[256]; // More than enough for any reasonable palette
  int char_count = 0;

  const char *p = ascii_chars;
  while (*p && char_count < 255) {
    char_starts[char_count] = (int)(p - ascii_chars); // Byte offset of this character
    char_count++;

    // Skip to next UTF-8 character
    if ((*p & 0x80) == 0) {
      // ASCII character (1 byte)
      p++;
    } else if ((*p & 0xE0) == 0xC0) {
      // 2-byte UTF-8 character
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      // 3-byte UTF-8 character
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      // 4-byte UTF-8 character
      p += 4;
    } else {
      // Invalid UTF-8, skip 1 byte
      p++;
    }
  }

  if (char_count == 0) {
    // No valid characters found, use space
    for (int i = 0; i < RAMP64_SIZE; i++) {
      ramp64[i] = ' ';
    }
    return;
  }

  // Now build the ramp64 lookup using character indices, not byte indices
  for (int i = 0; i < RAMP64_SIZE; i++) {
    // Map 0-63 to 0-(char_count-1) using proper character indexing
    int char_idx = (i * (char_count - 1) + (RAMP64_SIZE - 1) / 2) / (RAMP64_SIZE - 1);
    if (char_idx >= char_count) {
      char_idx = char_count - 1;
    }

    // Get the first byte of the character at this character index
    int byte_offset = char_starts[char_idx];
    ramp64[i] = (uint8_t)ascii_chars[byte_offset];
  }
}

// UTF-8 palette cache system
static hashtable_t *g_utf8_cache_table = NULL;
static pthread_rwlock_t g_utf8_cache_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Character index ramp cache system (typedef in common.h)
static hashtable_t *g_char_ramp_cache_table = NULL;
static pthread_rwlock_t g_char_ramp_cache_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Initialize UTF-8 cache system
static void init_utf8_cache_system(void) {
  if (!g_utf8_cache_table) {
    g_utf8_cache_table = hashtable_create();
  }
}

// Get or create UTF-8 palette cache for a given palette
utf8_palette_cache_t *get_utf8_palette_cache(const char *ascii_chars) {
  if (!ascii_chars)
    return NULL;

  // Create hash of palette for cache key
  uint32_t palette_hash = asciichat_crc32(ascii_chars, strlen(ascii_chars));

  // Fast path: Try read-only lookup first (most common case)
  pthread_rwlock_rdlock(&g_utf8_cache_rwlock);
  if (g_utf8_cache_table) {
    utf8_palette_cache_t *cache = (utf8_palette_cache_t *)hashtable_lookup(g_utf8_cache_table, palette_hash);
    if (cache) {
      pthread_rwlock_unlock(&g_utf8_cache_rwlock);
      return cache;
    }
  }
  pthread_rwlock_unlock(&g_utf8_cache_rwlock);

  // Slow path: Need to create cache entry, acquire write lock
  pthread_rwlock_wrlock(&g_utf8_cache_rwlock);

  init_utf8_cache_system();

  // Double-check: another thread might have created the cache
  utf8_palette_cache_t *cache = (utf8_palette_cache_t *)hashtable_lookup(g_utf8_cache_table, palette_hash);
  if (!cache) {
    // Create new cache
    SAFE_MALLOC(cache, sizeof(utf8_palette_cache_t), utf8_palette_cache_t *);
    memset(cache, 0, sizeof(utf8_palette_cache_t));

    // Build both cache types
    build_utf8_luminance_cache(ascii_chars, cache->cache);
    build_utf8_ramp64_cache(ascii_chars, cache->cache64, cache->char_index_ramp);

    // Store palette hash for validation
    strncpy(cache->palette_hash, ascii_chars, sizeof(cache->palette_hash) - 1);
    cache->palette_hash[sizeof(cache->palette_hash) - 1] = '\0';
    cache->is_valid = true;

    // Store in hashtable
    hashtable_insert(g_utf8_cache_table, palette_hash, cache);

    log_debug("UTF8_CACHE: Created new cache for palette='%s' (hash=0x%x)", ascii_chars, palette_hash);
  }

  pthread_rwlock_unlock(&g_utf8_cache_rwlock);
  return cache;
}

// Build 256-entry UTF-8 cache for direct luminance lookup (monochrome renderers)
void build_utf8_luminance_cache(const char *ascii_chars, utf8_char_t cache[256]) {
  if (!ascii_chars || !cache)
    return;

  // Parse characters
  typedef struct {
    const char *start;
    int byte_len;
  } char_info_t;

  char_info_t char_infos[256];
  int char_count = 0;
  const char *p = ascii_chars;

  while (*p && char_count < 255) {
    char_infos[char_count].start = p;

    if ((*p & 0x80) == 0) {
      char_infos[char_count].byte_len = 1;
      p++;
    } else if ((*p & 0xE0) == 0xC0) {
      char_infos[char_count].byte_len = 2;
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      char_infos[char_count].byte_len = 3;
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      char_infos[char_count].byte_len = 4;
      p += 4;
    } else {
      char_infos[char_count].byte_len = 1;
      p++;
    }
    char_count++;
  }

  // Build 256-entry cache
  for (int i = 0; i < 256; i++) {
    int char_idx = char_count > 1 ? (i * (char_count - 1) + 127) / 255 : 0;
    if (char_idx >= char_count)
      char_idx = char_count - 1;

    cache[i].byte_len = char_infos[char_idx].byte_len;
    memcpy(cache[i].utf8_bytes, char_infos[char_idx].start, char_infos[char_idx].byte_len);
    if (cache[i].byte_len < 4) {
      cache[i].utf8_bytes[cache[i].byte_len] = '\0';
    }
  }
}

// Build 64-entry UTF-8 cache for SIMD color lookup
void build_utf8_ramp64_cache(const char *ascii_chars, utf8_char_t cache64[64], uint8_t char_index_ramp[64]) {
  if (!ascii_chars || !cache64 || !char_index_ramp)
    return;

  // Reuse the luminance cache building logic but for 64 entries
  // (Same UTF-8 parsing as above, but map to 64 entries instead of 256)

  // Parse characters (same as above)
  typedef struct {
    const char *start;
    int byte_len;
  } char_info_t;

  char_info_t char_infos[256];
  int char_count = 0;
  const char *p = ascii_chars;

  while (*p && char_count < 255) {
    char_infos[char_count].start = p;

    if ((*p & 0x80) == 0) {
      char_infos[char_count].byte_len = 1;
      p++;
    } else if ((*p & 0xE0) == 0xC0) {
      char_infos[char_count].byte_len = 2;
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      char_infos[char_count].byte_len = 3;
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      char_infos[char_count].byte_len = 4;
      p += 4;
    } else {
      char_infos[char_count].byte_len = 1;
      p++;
    }
    char_count++;
  }

  // Build 64-entry cache and index ramp
  for (int i = 0; i < 64; i++) {
    int char_idx = char_count > 1 ? (i * (char_count - 1) + 31) / 63 : 0;
    if (char_idx >= char_count)
      char_idx = char_count - 1;

    // Store cache index for SIMD lookup
    char_index_ramp[i] = (uint8_t)i;

    // Cache UTF-8 character
    cache64[i].byte_len = char_infos[char_idx].byte_len;
    memcpy(cache64[i].utf8_bytes, char_infos[char_idx].start, char_infos[char_idx].byte_len);
    if (cache64[i].byte_len < 4) {
      cache64[i].utf8_bytes[cache64[i].byte_len] = '\0';
    }
  }
}

// Get or create cached character index ramp for SIMD architectures
char_index_ramp_cache_t *get_char_index_ramp_cache(const char *ascii_chars) {
  if (!ascii_chars)
    return NULL;

  // Create hash of palette for cache key
  uint32_t palette_hash = asciichat_crc32(ascii_chars, strlen(ascii_chars));

  // Fast path: Try read-only lookup first (most common case)
  pthread_rwlock_rdlock(&g_char_ramp_cache_rwlock);
  if (g_char_ramp_cache_table) {
    char_index_ramp_cache_t *cache = (char_index_ramp_cache_t *)hashtable_lookup(g_char_ramp_cache_table, palette_hash);
    if (cache) {
      pthread_rwlock_unlock(&g_char_ramp_cache_rwlock);
      return cache;
    }
  }
  pthread_rwlock_unlock(&g_char_ramp_cache_rwlock);

  // Slow path: Need to create cache entry, acquire write lock
  pthread_rwlock_wrlock(&g_char_ramp_cache_rwlock);

  // Initialize cache system if needed
  if (!g_char_ramp_cache_table) {
    g_char_ramp_cache_table = hashtable_create();
  }

  // Double-check: another thread might have created the cache
  char_index_ramp_cache_t *cache = (char_index_ramp_cache_t *)hashtable_lookup(g_char_ramp_cache_table, palette_hash);
  if (!cache) {
    // Create new cache
    SAFE_MALLOC(cache, sizeof(char_index_ramp_cache_t), char_index_ramp_cache_t *);
    memset(cache, 0, sizeof(char_index_ramp_cache_t));

    // Build character index ramp (same logic as NEON uses)
    size_t palette_len = strlen(ascii_chars);
    
    for (int i = 0; i < 64; i++) {
      int char_idx = palette_len > 1 ? (i * ((int)palette_len - 1) + 31) / 63 : 0;
      if (char_idx >= (int)palette_len) {
        char_idx = (int)palette_len - 1;
      }
      cache->char_index_ramp[i] = (uint8_t)char_idx;
    }

    // Store palette hash for validation
    strncpy(cache->palette_hash, ascii_chars, sizeof(cache->palette_hash) - 1);
    cache->palette_hash[sizeof(cache->palette_hash) - 1] = '\0';
    cache->is_valid = true;

    // Store in hashtable
    hashtable_insert(g_char_ramp_cache_table, palette_hash, cache);

    log_debug("CHAR_RAMP_CACHE: Created new ramp cache for palette='%s' (hash=0x%x)", ascii_chars, palette_hash);
  }

  pthread_rwlock_unlock(&g_char_ramp_cache_rwlock);
  return cache;
}

// Central cleanup function for all SIMD caches
void simd_caches_destroy_all(void) {
  log_debug("SIMD_CACHE: Starting cleanup of all SIMD caches");
  
  // Destroy shared character index ramp cache
  pthread_rwlock_wrlock(&g_char_ramp_cache_rwlock);
  if (g_char_ramp_cache_table) {
    hashtable_destroy(g_char_ramp_cache_table);
    g_char_ramp_cache_table = NULL;
    log_debug("CHAR_RAMP_CACHE: Destroyed shared character index ramp cache");
  }
  pthread_rwlock_unlock(&g_char_ramp_cache_rwlock);
  
  // Destroy shared UTF-8 palette cache
  pthread_rwlock_wrlock(&g_utf8_cache_rwlock);
  if (g_utf8_cache_table) {
    hashtable_destroy(g_utf8_cache_table);
    g_utf8_cache_table = NULL;
    log_debug("UTF8_CACHE: Destroyed shared UTF-8 palette cache");
  }
  pthread_rwlock_unlock(&g_utf8_cache_rwlock);

  // Call architecture-specific cache cleanup functions
#ifdef SIMD_SUPPORT_NEON
  neon_caches_destroy();
#endif
#ifdef SIMD_SUPPORT_SSE2
  sse2_caches_destroy();
#endif
#ifdef SIMD_SUPPORT_SSSE3
  ssse3_caches_destroy();
#endif
#ifdef SIMD_SUPPORT_AVX2
  avx2_caches_destroy();
#endif
#ifdef SIMD_SUPPORT_SVE
  sve_caches_destroy();
#endif

  log_debug("SIMD_CACHE: All SIMD caches destroyed");
}

// Output buffer functions moved to lib/image2ascii/output_buffer.c
