/**
 * @file image2ascii/simd/common.c
 * @ingroup image2ascii
 * @brief 🔧 Shared SIMD utilities: initialization, cleanup, and architecture-specific resource management
 */

#include "common.h"
#include "image2ascii/simd/common.h"
#include "uthash.h"
#include "palette.h"
#include "util/fnv1a.h"
#include <time.h>
#include <math.h>
#include <stdatomic.h>

// Include SIMD architecture headers for cleanup functions
// Note: Only ONE SIMD implementation is compiled based on highest available instruction set
#if SIMD_SUPPORT_NEON
#include "neon.h"
#elif defined(SIMD_SUPPORT_AVX2)
#include "avx2.h"
#elif defined(SIMD_SUPPORT_SSSE3)
#include "ssse3.h"
#elif defined(SIMD_SUPPORT_SSE2)
#include "sse2.h"
#elif defined(SIMD_SUPPORT_SVE)
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

  // Use UTF-8 palette functions for proper character handling
  utf8_palette_t *utf8_pal = utf8_palette_create(ascii_chars);
  if (!utf8_pal) {
    // Fallback to space character
    for (int i = 0; i < RAMP64_SIZE; i++) {
      ramp64[i] = ' ';
    }
    return;
  }

  size_t char_count = utf8_palette_get_char_count(utf8_pal);
  if (char_count == 0) {
    // No valid characters found, use space
    for (int i = 0; i < RAMP64_SIZE; i++) {
      ramp64[i] = ' ';
    }
    utf8_palette_destroy(utf8_pal);
    return;
  }

  // Build the ramp64 lookup using UTF-8 character indices
  for (int i = 0; i < RAMP64_SIZE; i++) {
    // Map 0-63 to 0-(char_count-1) using proper character indexing
    size_t char_idx = (i * (char_count - 1) + (RAMP64_SIZE - 1) / 2) / (RAMP64_SIZE - 1);
    if (char_idx >= char_count) {
      char_idx = char_count - 1;
    }

    // Get the first byte of the character at this character index
    const utf8_char_info_t *char_info = utf8_palette_get_char(utf8_pal, char_idx);
    if (char_info) {
      ramp64[i] = (uint8_t)char_info->bytes[0];
    } else {
      ramp64[i] = ' '; // Fallback
    }
  }

  utf8_palette_destroy(utf8_pal);
}

// Cache eviction helper functions
uint64_t get_current_time_ns(void) {
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000000000ULL + now.tv_nsec;
}

double calculate_cache_eviction_score(uint64_t last_access_time, uint32_t access_count, uint64_t creation_time,
                                      uint64_t current_time) {
  // Protect against unsigned underflow if times are inconsistent (clock adjustments, etc.)
  uint64_t age_ns = (current_time >= last_access_time) ? (current_time - last_access_time) : 0;
  uint64_t total_age_ns = (current_time >= creation_time) ? (current_time - creation_time) : 0;

  uint64_t age_seconds = age_ns / 1000000000ULL;
  uint64_t total_age_seconds = total_age_ns / 1000000000ULL;

  // Frequency factor: high-use palettes get protection (logarithmic scaling)
  double frequency_factor = 1.0 + log10(1.0 + access_count);

  // Aging factor: frequency bonus decays over time (5-minute half-life)
  double aging_factor = exp(-(double)age_seconds / CACHE_FREQUENCY_DECAY_TIME);

  // Recent access bonus: strong protection for recently used (1-minute protection)
  double recency_bonus = exp(-(double)age_seconds / CACHE_RECENCY_SCALE);

  // Cache lifetime penalty: prevent immortal caches (1-hour max lifetime)
  double lifetime_penalty = total_age_seconds > CACHE_MAX_LIFETIME ? 0.5 : 1.0;

  // Final score: higher = keep longer
  return (frequency_factor * aging_factor + recency_bonus) * lifetime_penalty;
}

// Fast palette string hashing using shared FNV-1a hash function
static inline uint32_t hash_palette_string(const char *palette) {
  return fnv1a_hash_string(palette);
}

// Forward declarations for eviction functions (defined after globals)
static bool try_insert_with_eviction_utf8(uint32_t hash, utf8_palette_cache_t *new_cache);

// UTF-8 palette cache system with min-heap eviction
static utf8_palette_cache_t *g_utf8_cache_table = NULL; // uthash uses structure pointer as head
static rwlock_t g_utf8_cache_rwlock = {0};
static _Atomic(bool) g_utf8_cache_initialized = false;

// Min-heap for O(log n) intelligent eviction
static utf8_palette_cache_t **g_utf8_heap = NULL; // Min-heap array
static size_t g_utf8_heap_size = 0;               // Current entries in heap
static const size_t g_utf8_heap_capacity = 2048;  // Max heap size (matching uthash capacity)

// char_index_ramp_cache removed - data is already stored in utf8_palette_cache_t.char_index_ramp[64]

// Initialize UTF-8 cache system with min-heap (thread-safe)
static void init_utf8_cache_system(void) {
  // Fast path: already initialized
  if (atomic_load(&g_utf8_cache_initialized)) {
    return;
  }

  // Slow path: need to initialize
  static mutex_t init_mutex = {0};
  static bool init_mutex_initialized = false;

  // Initialize the init mutex itself (safe because it's the first thing that runs)
  if (!init_mutex_initialized) {
    mutex_init(&init_mutex);
    init_mutex_initialized = true;
  }

  mutex_lock(&init_mutex);

  // Double-check after acquiring lock
  if (!atomic_load(&g_utf8_cache_initialized)) {
    // Initialize the cache rwlock
    rwlock_init(&g_utf8_cache_rwlock);

    // Initialize uthash head to NULL (required)
    g_utf8_cache_table = NULL;
    g_utf8_heap = SAFE_MALLOC(g_utf8_heap_capacity * sizeof(utf8_palette_cache_t *), utf8_palette_cache_t **);
    g_utf8_heap_size = 0;

    // Mark as initialized
    atomic_store(&g_utf8_cache_initialized, true);
  }

  mutex_unlock(&init_mutex);
}

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
    if (g_utf8_heap[index]->cached_score >= g_utf8_heap[parent]->cached_score)
      break;
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

    if (smallest == index)
      break;
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
  // Note: key should already be set by caller, but ensure it's set
  new_cache->key = hash;

  // Check if hash table is near capacity and evict proactively (80% threshold)
  size_t entry_count = HASH_COUNT(g_utf8_cache_table);
  if (entry_count >= g_utf8_heap_capacity) {
    // Proactive eviction: free space before attempting insertion
    utf8_palette_cache_t *victim_cache = utf8_heap_extract_min();
    if (victim_cache) {
      uint32_t victim_key = victim_cache->key;

      // Log clean eviction
      uint32_t victim_access_count = atomic_load(&victim_cache->access_count);
      uint64_t current_time = get_current_time_ns();
      uint64_t victim_age = (current_time - atomic_load(&victim_cache->last_access_time)) / 1000000000ULL;

      log_debug("UTF8_CACHE_EVICTION: Proactive min-heap eviction hash=0x%x (age=%lus, count=%u)", victim_key,
                victim_age, victim_access_count);

      // NOLINTNEXTLINE: uthash macros use void* casts internally (standard C practice, safe)
      HASH_DEL(g_utf8_cache_table, victim_cache);
      SAFE_FREE(victim_cache);
    }
  }

  // Now attempt insertion (should succeed with freed space)
  // Note: uthash doesn't have a failure path for insertion, so we handle eviction proactively
  HASH_ADD_INT(g_utf8_cache_table, key, new_cache);

  // Success: add to min-heap
  uint64_t current_time = get_current_time_ns();
  double initial_score = calculate_cache_eviction_score(current_time, 1, current_time, current_time);
  utf8_heap_insert(new_cache, initial_score);
  return true;
}

// char_ramp_cache functions removed - data already available in utf8_palette_cache_t

// Get or create UTF-8 palette cache for a given palette
utf8_palette_cache_t *get_utf8_palette_cache(const char *ascii_chars) {
  if (!ascii_chars)
    return NULL;

  // Check for empty string
  if (ascii_chars[0] == '\0')
    return NULL;

  // Create hash of palette for cache key
  uint32_t palette_hash = hash_palette_string(ascii_chars);

  // Ensure the cache system is initialized
  init_utf8_cache_system();

  // First try: read lock for cache lookup (allows multiple concurrent readers)
  rwlock_rdlock(&g_utf8_cache_rwlock);

  // Check if cache exists
  utf8_palette_cache_t *cache = NULL;
  // NOLINTNEXTLINE: uthash macros use void* casts internally (standard C practice, safe)
  HASH_FIND_INT(g_utf8_cache_table, &palette_hash, cache);
  if (cache) {
    // Cache hit: Update access tracking (atomics are thread-safe under rdlock)
    uint64_t current_time = get_current_time_ns();
    atomic_store(&cache->last_access_time, current_time);
    uint32_t new_access_count = atomic_fetch_add(&cache->access_count, 1) + 1;

    // Every 10th access: Update heap position (requires write lock)
    if (new_access_count % 10 == 0) {
      // Release read lock and upgrade to write lock
      rwlock_rdunlock(&g_utf8_cache_rwlock);
      rwlock_wrlock(&g_utf8_cache_rwlock);

      // Update heap position (modifies heap structure)
      uint64_t last_access = atomic_load(&cache->last_access_time);
      uint32_t access_count = atomic_load(&cache->access_count);
      double new_score = calculate_cache_eviction_score(last_access, access_count, cache->creation_time, current_time);
      utf8_heap_update_score(cache, new_score);

      rwlock_wrunlock(&g_utf8_cache_rwlock);
    } else {
      rwlock_rdunlock(&g_utf8_cache_rwlock);
    }

    return cache;
  }

  // Cache miss: Need to create new entry
  // Release read lock and acquire write lock
  rwlock_rdunlock(&g_utf8_cache_rwlock);
  rwlock_wrlock(&g_utf8_cache_rwlock);

  // Double-check: another thread might have created it while we upgraded locks
  cache = NULL;
  // NOLINTNEXTLINE: uthash macros use void* casts internally (standard C practice, safe)
  HASH_FIND_INT(g_utf8_cache_table, &palette_hash, cache);
  if (cache) {
    // Found it! Just update access tracking and return
    uint64_t current_time = get_current_time_ns();
    atomic_store(&cache->last_access_time, current_time);
    atomic_fetch_add(&cache->access_count, 1);
    rwlock_wrunlock(&g_utf8_cache_rwlock);
    return cache;
  }

  // Create new cache entry (holding write lock)
  cache = SAFE_MALLOC(sizeof(utf8_palette_cache_t), utf8_palette_cache_t *);
  if (!cache) {
    rwlock_wrunlock(&g_utf8_cache_rwlock);
    return NULL;
  }
  memset(cache, 0, sizeof(utf8_palette_cache_t));

  // Set the key for uthash
  cache->key = palette_hash;

  // Build both cache types
  build_utf8_luminance_cache(ascii_chars, cache->cache);
  build_utf8_ramp64_cache(ascii_chars, cache->cache64, cache->char_index_ramp);

  // Store palette hash for validation
  SAFE_STRNCPY(cache->palette_hash, ascii_chars, sizeof(cache->palette_hash));
  cache->is_valid = true;

  // Initialize eviction tracking
  uint64_t current_time = get_current_time_ns();
  atomic_store(&cache->last_access_time, current_time);
  atomic_store(&cache->access_count, 1); // First access
  cache->creation_time = current_time;

  // Store in hash table with guaranteed eviction support
  try_insert_with_eviction_utf8(palette_hash, cache);

  log_debug("UTF8_CACHE: Created new cache for palette='%s' (hash=0x%x)", ascii_chars, palette_hash);

  rwlock_wrunlock(&g_utf8_cache_rwlock);
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

    if ((*p & 0xE0) == 0xC0) {
      char_infos[char_count].byte_len = 2;
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      char_infos[char_count].byte_len = 3;
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      char_infos[char_count].byte_len = 4;
      p += 4;
    } else {
      // ASCII characters (0x00-0x7F) and invalid sequences: treat as single byte
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

    if ((*p & 0xE0) == 0xC0) {
      char_infos[char_count].byte_len = 2;
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      char_infos[char_count].byte_len = 3;
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      char_infos[char_count].byte_len = 4;
      p += 4;
    } else {
      // ASCII characters (0x00-0x7F) and invalid sequences: treat as single byte
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

    // Store character index for SIMD lookup
    char_index_ramp[i] = (uint8_t)char_idx;

    // Cache UTF-8 character
    cache64[i].byte_len = char_infos[char_idx].byte_len;
    memcpy(cache64[i].utf8_bytes, char_infos[char_idx].start, char_infos[char_idx].byte_len);
    if (cache64[i].byte_len < 4) {
      cache64[i].utf8_bytes[cache64[i].byte_len] = '\0';
    }
  }
}

// char_index_ramp_cache functions removed - data already available in utf8_palette_cache_t.char_index_ramp[64]

// No callback needed - uthash iteration handles cleanup directly

// Central cleanup function for all SIMD caches
void simd_caches_destroy_all(void) {
  log_debug("SIMD_CACHE: Starting cleanup of all SIMD caches");

  // Destroy shared UTF-8 palette cache (write lock for cleanup)
  rwlock_wrlock(&g_utf8_cache_rwlock);
  if (g_utf8_cache_table) {
    // Free all UTF-8 cache entries using HASH_ITER
    utf8_palette_cache_t *cache, *tmp;
    HASH_ITER(hh, g_utf8_cache_table, cache, tmp) {
      // NOLINTNEXTLINE: uthash macros use void* casts internally (standard C practice, safe)
      HASH_DEL(g_utf8_cache_table, cache);
      SAFE_FREE(cache);
    }
    g_utf8_cache_table = NULL;
    log_debug("UTF8_CACHE: Destroyed shared UTF-8 palette cache");
  }
  // Clean up heap arrays
  if (g_utf8_heap) {
    SAFE_FREE(g_utf8_heap);
    g_utf8_heap = NULL;
    g_utf8_heap_size = 0;
  }
  // Reset initialization flag so system can be reinitialized
  atomic_store(&g_utf8_cache_initialized, false);
  rwlock_wrunlock(&g_utf8_cache_rwlock);

  // Call architecture-specific cache cleanup functions
  // Note: Only ONE SIMD implementation is compiled based on highest available instruction set
  // Higher instruction sets (AVX2, SSSE3) handle cleanup for lower ones (SSE2)
#if SIMD_SUPPORT_NEON
  neon_caches_destroy();
#elif defined(SIMD_SUPPORT_AVX2)
  avx2_caches_destroy();
#elif defined(SIMD_SUPPORT_SSSE3)
  ssse3_caches_destroy();
#elif defined(SIMD_SUPPORT_SSE2)
  sse2_caches_destroy();
#elif defined(SIMD_SUPPORT_SVE)
  sve_caches_destroy();
#endif

  log_debug("SIMD_CACHE: All SIMD caches destroyed");
}

// Output buffer functions moved to lib/image2ascii/output_buffer.c
