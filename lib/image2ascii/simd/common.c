#include "common.h"
#include "image2ascii/simd/common.h"
#include "hashtable.h"
#include "crc32_hw.h"
#include "ascii_simd.h"
#include <time.h>
#include <math.h>
#include <stdatomic.h>

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

// Cache eviction helper functions
uint64_t get_current_time_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000000000ULL + now.tv_nsec;
}

double calculate_cache_eviction_score(uint64_t last_access_time, uint32_t access_count,
                                      uint64_t creation_time, uint64_t current_time) {
  uint64_t age_seconds = (current_time - last_access_time) / 1000000000ULL;
  uint64_t total_age_seconds = (current_time - creation_time) / 1000000000ULL;

  // Frequency factor: high-use palettes get protection (logarithmic scaling)
  double frequency_factor = 1.0 + log10(1.0 + access_count);

  // Aging factor: frequency bonus decays over time (5-minute half-life)
  double aging_factor = exp(-age_seconds / CACHE_FREQUENCY_DECAY_TIME);

  // Recent access bonus: strong protection for recently used (1-minute protection)
  double recency_bonus = exp(-age_seconds / CACHE_RECENCY_SCALE);

  // Cache lifetime penalty: prevent immortal caches (1-hour max lifetime)
  double lifetime_penalty = total_age_seconds > CACHE_MAX_LIFETIME ? 0.5 : 1.0;

  // Final score: higher = keep longer
  return (frequency_factor * aging_factor + recency_bonus) * lifetime_penalty;
}

// Palette hash cache temporarily disabled
uint32_t get_palette_hash_cached(const char *palette) {
  // Simple fallback: just compute CRC32 directly (optimize later)
  if (!palette || strlen(palette) == 0) return 0;
  return asciichat_crc32(palette, strlen(palette));
}

// Forward declarations for eviction functions (defined after globals)
static bool try_insert_with_eviction_utf8(uint32_t hash, utf8_palette_cache_t *new_cache);
static bool try_insert_with_eviction_char_ramp(uint32_t hash, char_index_ramp_cache_t *new_cache);

// UTF-8 palette cache system with min-heap eviction
static hashtable_t *g_utf8_cache_table = NULL;
static pthread_rwlock_t g_utf8_cache_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Min-heap for O(log n) intelligent eviction
static utf8_palette_cache_t **g_utf8_heap = NULL;         // Min-heap array
static size_t g_utf8_heap_size = 0;                       // Current entries in heap
static const size_t g_utf8_heap_capacity = HASHTABLE_MAX_ENTRIES; // Max heap size

// Character index ramp cache system with min-heap eviction
static hashtable_t *g_char_ramp_cache_table = NULL;
static pthread_rwlock_t g_char_ramp_cache_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Min-heap for O(log n) intelligent eviction
static char_index_ramp_cache_t **g_char_ramp_heap = NULL;
static size_t g_char_ramp_heap_size = 0;
static const size_t g_char_ramp_heap_capacity = HASHTABLE_MAX_ENTRIES;

// Initialize UTF-8 cache system with min-heap
static void init_utf8_cache_system(void) {
  if (!g_utf8_cache_table) {
    g_utf8_cache_table = hashtable_create();
    
    // Initialize min-heap for eviction
    SAFE_MALLOC(g_utf8_heap, g_utf8_heap_capacity * sizeof(utf8_palette_cache_t*), utf8_palette_cache_t**);
    g_utf8_heap_size = 0;
  }
}

// Min-heap management functions for UTF-8 cache

// Palette hash cache system (string-based lookup for O(1) CRC32)
static palette_hash_cache_t *g_palette_hash_cache[64] = {NULL}; // Simple hash table
static pthread_rwlock_t g_palette_hash_cache_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Min-heap for palette hash cache eviction
static palette_hash_cache_t **g_palette_hash_heap = NULL;
static size_t g_palette_hash_heap_size = 0;
static const size_t g_palette_hash_heap_capacity = 64; // Separate limit for hash cache

// Min-heap management functions for UTF-8 cache
static void utf8_heap_swap(size_t i, size_t j) {
  utf8_palette_cache_t *temp = g_utf8_heap[i];
  g_utf8_heap[i] = g_utf8_heap[j];
  g_utf8_heap[j] = temp;

  // Update heap indices in cache objects
  g_utf8_heap[i]->heap_index = i;
  g_utf8_heap[j]->heap_index = j;
}

static void utf8_heap_bubble_up(size_t index) {
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (g_utf8_heap[index]->cached_score >= g_utf8_heap[parent]->cached_score) break;
    utf8_heap_swap(index, parent);
    index = parent;
  }
}

static void utf8_heap_bubble_down(size_t index) {
  while (true) {
    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;
    size_t smallest = index;

    if (left < g_utf8_heap_size && g_utf8_heap[left]->cached_score < g_utf8_heap[smallest]->cached_score) {
      smallest = left;
    }
    if (right < g_utf8_heap_size && g_utf8_heap[right]->cached_score < g_utf8_heap[smallest]->cached_score) {
      smallest = right;
    }

    if (smallest == index) break;
    utf8_heap_swap(index, smallest);
    index = smallest;
  }
}

static void utf8_heap_insert(utf8_palette_cache_t *cache, double score) {
  if (g_utf8_heap_size >= g_utf8_heap_capacity) {
    log_error("UTF8_HEAP: Heap capacity exceeded");
    return;
  }

  cache->cached_score = score;
  cache->heap_index = g_utf8_heap_size;
  g_utf8_heap[g_utf8_heap_size] = cache;
  g_utf8_heap_size++;

  utf8_heap_bubble_up(cache->heap_index);
}

static utf8_palette_cache_t *utf8_heap_extract_min(void) {
  if (g_utf8_heap_size == 0) {
    return NULL;
  }

  utf8_palette_cache_t *min_cache = g_utf8_heap[0];

  // Move last element to root and bubble down
  g_utf8_heap_size--;
  if (g_utf8_heap_size > 0) {
    g_utf8_heap[0] = g_utf8_heap[g_utf8_heap_size];
    g_utf8_heap[0]->heap_index = 0;
    utf8_heap_bubble_down(0);
  }

  return min_cache;
}

static void utf8_heap_update_score(utf8_palette_cache_t *cache, double new_score) {
  double old_score = cache->cached_score;
  cache->cached_score = new_score;

  if (new_score < old_score) {
    utf8_heap_bubble_up(cache->heap_index);
  } else {
    utf8_heap_bubble_down(cache->heap_index);
  }
}

// Min-heap functions now replace LRU list management

// Thread-safe cache eviction implementations
static bool try_insert_with_eviction_utf8(uint32_t hash, utf8_palette_cache_t *new_cache) {
  // Already holding write lock

  // Check if hashtable is near capacity and evict proactively (80% threshold)
  if (g_utf8_cache_table->entry_count >= HASHTABLE_MAX_ENTRIES) {
    // Proactive eviction: free space before attempting insertion
    utf8_palette_cache_t *victim_cache = utf8_heap_extract_min();
    if (victim_cache) {
      uint32_t victim_key = asciichat_crc32(victim_cache->palette_hash, strlen(victim_cache->palette_hash));

      // Log clean eviction
      uint32_t victim_access_count = atomic_load(&victim_cache->access_count);
      uint64_t current_time = get_current_time_ns();
      uint64_t victim_age = (current_time - atomic_load(&victim_cache->last_access_time)) / 1000000000ULL;

      log_debug("UTF8_CACHE_EVICTION: Proactive min-heap eviction hash=0x%x (age=%lus, count=%u)",
                victim_key, victim_age, victim_access_count);

      hashtable_remove(g_utf8_cache_table, victim_key);
      SAFE_FREE(victim_cache);
    }
  }

  // Now attempt insertion (should succeed with freed space)
  if (hashtable_insert(g_utf8_cache_table, hash, new_cache)) {
    // Success: add to min-heap
    uint64_t current_time = get_current_time_ns();
    double initial_score = calculate_cache_eviction_score(current_time, 1, current_time, current_time);
    utf8_heap_insert(new_cache, initial_score);
    return true;
  }

  // Fallback: should rarely happen with proactive eviction
  utf8_palette_cache_t *victim_cache = utf8_heap_extract_min();
  if (!victim_cache) {
    log_error("UTF8_CACHE_CRITICAL: No cache entries in heap to evict");
    return false;
  }

  // Calculate victim key for hashtable removal
  uint32_t victim_key = asciichat_crc32(victim_cache->palette_hash, strlen(victim_cache->palette_hash));

  // Log eviction for debugging
  uint32_t victim_access_count = atomic_load(&victim_cache->access_count);
  uint64_t current_time = get_current_time_ns();
  uint64_t victim_age = (current_time - atomic_load(&victim_cache->last_access_time)) / 1000000000ULL;

  log_debug("UTF8_CACHE_EVICTION: Min-heap evicting worst cache hash=0x%x (score=%.3f, age=%lus, count=%u)",
            victim_key, victim_cache->cached_score, victim_age, victim_access_count);

  // Remove from hashtable and free
  hashtable_remove(g_utf8_cache_table, victim_key);
  SAFE_FREE(victim_cache);

  // Insert new cache into both hashtable and heap
  if (hashtable_insert(g_utf8_cache_table, hash, new_cache)) {
    double initial_score = calculate_cache_eviction_score(current_time, 1, current_time, current_time);
    utf8_heap_insert(new_cache, initial_score);
    return true;
  } else {
    log_error("UTF8_CACHE_CRITICAL: Failed to insert after eviction");
    return false;
  }
}

// Min-heap management for char_ramp_cache (same pattern as UTF-8)
static void char_ramp_heap_swap(size_t i, size_t j) {
  char_index_ramp_cache_t *temp = g_char_ramp_heap[i];
  g_char_ramp_heap[i] = g_char_ramp_heap[j];
  g_char_ramp_heap[j] = temp;

  g_char_ramp_heap[i]->heap_index = i;
  g_char_ramp_heap[j]->heap_index = j;
}

static void char_ramp_heap_bubble_up(size_t index) {
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (g_char_ramp_heap[index]->cached_score >= g_char_ramp_heap[parent]->cached_score) break;
    char_ramp_heap_swap(index, parent);
    index = parent;
  }
}

static void char_ramp_heap_bubble_down(size_t index) {
  while (true) {
    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;
    size_t smallest = index;

    if (left < g_char_ramp_heap_size && g_char_ramp_heap[left]->cached_score < g_char_ramp_heap[smallest]->cached_score) {
      smallest = left;
    }
    if (right < g_char_ramp_heap_size && g_char_ramp_heap[right]->cached_score < g_char_ramp_heap[smallest]->cached_score) {
      smallest = right;
    }

    if (smallest == index) break;
    char_ramp_heap_swap(index, smallest);
    index = smallest;
  }
}

static void char_ramp_heap_insert(char_index_ramp_cache_t *cache, double score) {
  if (g_char_ramp_heap_size >= g_char_ramp_heap_capacity) {
    log_error("CHAR_RAMP_HEAP: Heap capacity exceeded");
    return;
  }

  cache->cached_score = score;
  cache->heap_index = g_char_ramp_heap_size;
  g_char_ramp_heap[g_char_ramp_heap_size] = cache;
  g_char_ramp_heap_size++;

  char_ramp_heap_bubble_up(cache->heap_index);
}

static char_index_ramp_cache_t *char_ramp_heap_extract_min(void) {
  if (g_char_ramp_heap_size == 0) {
    return NULL;
  }

  char_index_ramp_cache_t *min_cache = g_char_ramp_heap[0];

  g_char_ramp_heap_size--;
  if (g_char_ramp_heap_size > 0) {
    g_char_ramp_heap[0] = g_char_ramp_heap[g_char_ramp_heap_size];
    g_char_ramp_heap[0]->heap_index = 0;
    char_ramp_heap_bubble_down(0);
  }

  return min_cache;
}

static bool try_insert_with_eviction_char_ramp(uint32_t hash, char_index_ramp_cache_t *new_cache) {
  // Already holding write lock

  // Check if hashtable is near capacity and evict proactively (80% threshold)
  if (g_char_ramp_cache_table->entry_count >= HASHTABLE_MAX_ENTRIES) {
    // Proactive eviction: free space before attempting insertion
    char_index_ramp_cache_t *victim_cache = char_ramp_heap_extract_min();
    if (victim_cache) {
      uint32_t victim_key = asciichat_crc32(victim_cache->palette_hash, strlen(victim_cache->palette_hash));

      // Log clean eviction
      uint32_t victim_access_count = atomic_load(&victim_cache->access_count);
      uint64_t current_time = get_current_time_ns();
      uint64_t victim_age = (current_time - atomic_load(&victim_cache->last_access_time)) / 1000000000ULL;

      log_debug("CHAR_RAMP_EVICTION: Proactive min-heap eviction hash=0x%x (age=%lus, count=%u)",
                victim_key, victim_age, victim_access_count);

      hashtable_remove(g_char_ramp_cache_table, victim_key);
      SAFE_FREE(victim_cache);
    }
  }

  // Now attempt insertion (should succeed with freed space)
  if (hashtable_insert(g_char_ramp_cache_table, hash, new_cache)) {
    // Success: add to min-heap
    uint64_t current_time = get_current_time_ns();
    double initial_score = calculate_cache_eviction_score(current_time, 1, current_time, current_time);
    char_ramp_heap_insert(new_cache, initial_score);
    return true;
  }

  // Fallback: should rarely happen with proactive eviction
  char_index_ramp_cache_t *victim_cache = char_ramp_heap_extract_min();
  if (!victim_cache) {
    log_error("CHAR_RAMP_CACHE_CRITICAL: No cache entries in heap to evict");
    return false;
  }

  // Emergency eviction
  uint32_t victim_key = asciichat_crc32(victim_cache->palette_hash, strlen(victim_cache->palette_hash));
  log_debug("CHAR_RAMP_EVICTION: Emergency min-heap eviction hash=0x%x", victim_key);

  hashtable_remove(g_char_ramp_cache_table, victim_key);
  SAFE_FREE(victim_cache);

  // Insert new cache
  if (hashtable_insert(g_char_ramp_cache_table, hash, new_cache)) {
    uint64_t current_time = get_current_time_ns();
    double initial_score = calculate_cache_eviction_score(current_time, 1, current_time, current_time);
    char_ramp_heap_insert(new_cache, initial_score);
    return true;
  }

  log_error("CHAR_RAMP_CACHE_CRITICAL: Failed to insert after eviction");
  return false;
}

// Get or create UTF-8 palette cache for a given palette
utf8_palette_cache_t *get_utf8_palette_cache(const char *ascii_chars) {
  if (!ascii_chars)
    return NULL;

  // Check for empty string
  if (ascii_chars[0] == '\0')
    return NULL;

  // Create hash of palette for cache key
  uint32_t palette_hash = asciichat_crc32(ascii_chars, strlen(ascii_chars));

  // Fast path: Try read-only lookup first (most common case)
  pthread_rwlock_rdlock(&g_utf8_cache_rwlock);
  if (g_utf8_cache_table) {
    utf8_palette_cache_t *cache = (utf8_palette_cache_t *)hashtable_lookup(g_utf8_cache_table, palette_hash);
    if (cache) {
      // ATOMIC: Update access tracking without lock upgrade
      uint64_t current_time = get_current_time_ns();
      atomic_store(&cache->last_access_time, current_time);
      uint32_t new_access_count = atomic_fetch_add(&cache->access_count, 1) + 1;

      // Every 10th access: Update heap position (amortized O(log n))
      if (new_access_count % 10 == 0) {
        // Need to upgrade to write lock for heap updates
        pthread_rwlock_unlock(&g_utf8_cache_rwlock);
        pthread_rwlock_wrlock(&g_utf8_cache_rwlock);

        // Recalculate score and update heap position
        uint64_t last_access = atomic_load(&cache->last_access_time);
        uint32_t access_count = atomic_load(&cache->access_count);
        double new_score = calculate_cache_eviction_score(last_access, access_count,
                                                          cache->creation_time, current_time);
        utf8_heap_update_score(cache, new_score);

        pthread_rwlock_unlock(&g_utf8_cache_rwlock);
        return cache;
      }

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

    // Initialize eviction tracking
    uint64_t current_time = get_current_time_ns();
    atomic_store(&cache->last_access_time, current_time);
    atomic_store(&cache->access_count, 1); // First access
    cache->creation_time = current_time;

    // Store in hashtable with guaranteed eviction support
    if (!try_insert_with_eviction_utf8(palette_hash, cache)) {
      log_error("UTF8_CACHE_CRITICAL: Failed to insert cache even after eviction - system overloaded");
      SAFE_FREE(cache);
      pthread_rwlock_unlock(&g_utf8_cache_rwlock);
      return NULL;
    }

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
      // ATOMIC: Update access tracking without lock upgrade
      uint64_t current_time = get_current_time_ns();
      atomic_store(&cache->last_access_time, current_time);
      atomic_fetch_add(&cache->access_count, 1);

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

    // Initialize min-heap for eviction
    SAFE_MALLOC(g_char_ramp_heap, g_char_ramp_heap_capacity * sizeof(char_index_ramp_cache_t*), char_index_ramp_cache_t**);
    g_char_ramp_heap_size = 0;
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

    // Initialize eviction tracking
    uint64_t current_time = get_current_time_ns();
    atomic_store(&cache->last_access_time, current_time);
    atomic_store(&cache->access_count, 1); // First access
    cache->creation_time = current_time;
    cache->heap_index = 0; // Will be set by heap_insert
    cache->cached_score = 0.0; // Will be calculated by heap_insert

    // Store in hashtable with guaranteed min-heap eviction support
    if (!try_insert_with_eviction_char_ramp(palette_hash, cache)) {
      log_error("CHAR_RAMP_CACHE_CRITICAL: Failed to insert cache even after heap eviction");
      SAFE_FREE(cache);
      pthread_rwlock_unlock(&g_char_ramp_cache_rwlock);
      return NULL;
    }

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
  // Clean up heap arrays
  if (g_char_ramp_heap) {
    SAFE_FREE(g_char_ramp_heap);
    g_char_ramp_heap_size = 0;
  }
  pthread_rwlock_unlock(&g_char_ramp_cache_rwlock);

  // Destroy shared UTF-8 palette cache
  pthread_rwlock_wrlock(&g_utf8_cache_rwlock);
  if (g_utf8_cache_table) {
    hashtable_destroy(g_utf8_cache_table);
    g_utf8_cache_table = NULL;
    log_debug("UTF8_CACHE: Destroyed shared UTF-8 palette cache");
  }
  // Clean up heap arrays
  if (g_utf8_heap) {
    SAFE_FREE(g_utf8_heap);
    g_utf8_heap_size = 0;
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
