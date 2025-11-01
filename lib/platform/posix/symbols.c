/**
 * @file symbols.c
 * @brief Symbol resolution cache implementation
 *
 * Caches addr2line results to avoid expensive subprocess spawns on every backtrace.
 * Uses hashtable for O(1) lookups of previously resolved addresses.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2025
 */

#ifndef _WIN32

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdatomic.h>

#include "../symbols.h"
#include "../../common.h"
#include "../../hashtable.h"
#include "../../util/path.h"

// ============================================================================
// Constants
// ============================================================================

// Sentinel string for failed allocations (replaces NULL in middle of array)
#define NULL_SENTINEL "[NULL]"

// ============================================================================
// Cache State
// ============================================================================

typedef struct {
  void *addr;   // Address key
  char *symbol; // Resolved symbol string (owned by this entry)
} symbol_entry_t;

static hashtable_t *g_symbol_cache = NULL;
static atomic_bool g_symbol_cache_initialized = false;

// Statistics
static atomic_uint_fast64_t g_cache_hits = 0;
static atomic_uint_fast64_t g_cache_misses = 0;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Hash a pointer address to uint32_t for hashtable
 */
static inline uint32_t hash_address(void *addr) {
  uintptr_t val = (uintptr_t)addr;
  // Use XOR folding to avoid overflow issues
  // This is guaranteed to not overflow and provides good distribution
  uint32_t low = (uint32_t)val;
  uint32_t high = (uint32_t)(val >> 32);

  // Fold 64-bit address into 32-bit hash
  // XOR folding is safe and avoids any overflow concerns
  return low ^ high;
}

/**
 * @brief Cleanup callback for hashtable_foreach during cache cleanup
 */
static void cleanup_symbol_entry_callback(uint32_t key, void *value, void *user_data) {
  (void)key;
  (void)user_data;
  symbol_entry_t *entry = (symbol_entry_t *)value;
  if (entry) {
    if (entry->symbol) {
      SAFE_FREE(entry->symbol);
    }
    SAFE_FREE(entry);
  }
}

// ============================================================================
// Public API Implementation
// ============================================================================

int symbol_cache_init(void) {
  bool expected = false;
  if (!atomic_compare_exchange_strong(&g_symbol_cache_initialized, &expected, true)) {
    return 0; // Already initialized
  }

  // Create hashtable (thread-safe with internal rwlocks)
  g_symbol_cache = hashtable_create();
  if (!g_symbol_cache) {
    atomic_store(&g_symbol_cache_initialized, false);
    return -1;
  }

  atomic_store(&g_cache_hits, 0);
  atomic_store(&g_cache_misses, 0);

  log_debug("Symbol cache initialized");
  return 0;
}

void symbol_cache_cleanup(void) {
  if (!atomic_load(&g_symbol_cache_initialized)) {
    return;
  }

  // Mark as uninitialized FIRST to prevent new inserts during cleanup
  atomic_store(&g_symbol_cache_initialized, false);

  if (g_symbol_cache) {
    // Acquire write lock to prevent any concurrent operations
    hashtable_write_lock(g_symbol_cache);

    // Free all symbol entries using hashtable_foreach
    // We iterate through all buckets with the lock held, ensuring no new entries
    // can be inserted (since initialized=false prevents new inserts)
    hashtable_foreach(g_symbol_cache, cleanup_symbol_entry_callback, NULL);

    // Release lock before destroying
    hashtable_write_unlock(g_symbol_cache);

    hashtable_destroy(g_symbol_cache);
    g_symbol_cache = NULL;
  }

  log_debug("Symbol cache cleaned up (hits=%llu, misses=%llu)", (unsigned long long)atomic_load(&g_cache_hits),
            (unsigned long long)atomic_load(&g_cache_misses));
}

const char *symbol_cache_lookup(void *addr) {
  if (!atomic_load(&g_symbol_cache_initialized) || !g_symbol_cache) {
    return NULL;
  }

  uint32_t key = hash_address(addr);
  symbol_entry_t *entry = (symbol_entry_t *)hashtable_lookup(g_symbol_cache, key);

  if (entry && entry->addr == addr) {
    atomic_fetch_add(&g_cache_hits, 1);
    return entry->symbol;
  }

  atomic_fetch_add(&g_cache_misses, 1);
  return NULL;
}

// Helper function to hash uint32_t (duplicated from hashtable.c to avoid dependency)
static inline uint32_t hash_uint32_for_cache(uint32_t key) {
  // FNV-1a hash - same as hashtable.c
  uint64_t hash = 2166136261ULL;
  const uint64_t fnv_prime = 16777619ULL;

  hash ^= (key & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;
  hash ^= ((key >> 8) & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;
  hash ^= ((key >> 16) & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;
  hash ^= ((key >> 24) & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;

  // Use bit masking for power-of-2 bucket count
  return (uint32_t)hash & (HASHTABLE_BUCKET_COUNT - 1);
}

bool symbol_cache_insert(void *addr, const char *symbol) {
  if (!atomic_load(&g_symbol_cache_initialized) || !g_symbol_cache || !symbol) {
    return false;
  }

  uint32_t key = hash_address(addr);

  // Acquire write lock to make the entire operation atomic
  // This prevents other threads from inserting between our check and insert
  // Store local pointer before locking to avoid race with cleanup
  hashtable_t *cache = g_symbol_cache;
  if (!cache) {
    return false;
  }

  hashtable_write_lock(cache);

  // Double-check g_symbol_cache is still valid and initialized after acquiring lock
  // (cleanup might have destroyed it or marked it uninitialized between our check and lock acquisition)
  if (!g_symbol_cache || !atomic_load(&g_symbol_cache_initialized)) {
    hashtable_write_unlock(cache);
    return false;
  }

  // Use local cache variable for rest of function
  cache = g_symbol_cache;

  // Calculate bucket index
  uint32_t bucket_idx = hash_uint32_for_cache(key);

  // Check if entry already exists by directly accessing hashtable buckets
  // We can do this because we hold the write lock
  hashtable_entry_t *curr = cache->buckets[bucket_idx];
  hashtable_entry_t *prev = NULL;
  hashtable_entry_t *existing_entry = NULL;
  symbol_entry_t *existing_value = NULL;

  while (curr) {
    if (curr->key == key && curr->in_use) {
      existing_entry = curr;
      existing_value = (symbol_entry_t *)curr->value;
      break;
    }
    prev = curr;
    curr = curr->next;
  }

  // If entry exists with same address, nothing to do
  if (existing_value && existing_value->addr == addr) {
    hashtable_write_unlock(cache);
    return true;
  }

  // If key exists but address doesn't match, remove and free the old entry
  if (existing_entry) {
    // Free the old entry's symbol string and structure
    if (existing_value) {
      if (existing_value->symbol) {
        SAFE_FREE(existing_value->symbol);
      }
      SAFE_FREE(existing_value);
    }

    // Remove from chain (we already have prev and existing_entry)
    if (prev) {
      prev->next = existing_entry->next;
    } else {
      cache->buckets[bucket_idx] = existing_entry->next;
    }

    // Return entry to pool
    existing_entry->key = 0;
    existing_entry->value = NULL;
    existing_entry->in_use = false;
    existing_entry->next = cache->free_list;
    cache->free_list = existing_entry;

    cache->entry_count--;
    cache->deletions++;
  }

  // Create new entry
  symbol_entry_t *entry = SAFE_MALLOC(sizeof(symbol_entry_t), symbol_entry_t *);
  if (!entry) {
    hashtable_write_unlock(cache);
    return false;
  }

  entry->addr = addr;
  entry->symbol = strdup(symbol);
  if (!entry->symbol) {
    SAFE_FREE(entry);
    hashtable_write_unlock(cache);
    return false;
  }

  // Get a free entry from pool
  hashtable_entry_t *ht_entry = cache->free_list;
  if (!ht_entry) {
    // Pool exhausted
    SAFE_FREE(entry->symbol);
    SAFE_FREE(entry);
    hashtable_write_unlock(cache);
    SET_ERRNO(ERROR_MEMORY, "Hashtable entry pool exhausted");
    return false;
  }

  cache->free_list = ht_entry->next;
  ht_entry->next = NULL;
  ht_entry->in_use = true;

  // Insert new entry
  ht_entry->key = key;
  ht_entry->value = entry;
  ht_entry->next = cache->buckets[bucket_idx];
  if (cache->buckets[bucket_idx]) {
    cache->collisions++;
  }
  cache->buckets[bucket_idx] = ht_entry;

  cache->entry_count++;
  cache->insertions++;

  // Release lock
  hashtable_write_unlock(cache);

  return true;
}

void symbol_cache_get_stats(uint64_t *hits_out, uint64_t *misses_out, size_t *entries_out) {
  if (hits_out) {
    *hits_out = atomic_load(&g_cache_hits);
  }
  if (misses_out) {
    *misses_out = atomic_load(&g_cache_misses);
  }
  if (entries_out && g_symbol_cache) {
    *entries_out = hashtable_size(g_symbol_cache);
  }
}

void symbol_cache_print_stats(void) {
  uint64_t hits = atomic_load(&g_cache_hits);
  uint64_t misses = atomic_load(&g_cache_misses);
  size_t entries = g_symbol_cache ? hashtable_size(g_symbol_cache) : 0;

  uint64_t total = hits + misses;
  double hit_rate = total > 0 ? (100.0 * hits / total) : 0.0;

  log_info("Symbol Cache Stats: %zu entries, %llu hits, %llu misses (%.1f%% hit rate)", entries,
           (unsigned long long)hits, (unsigned long long)misses, hit_rate);
}

// ============================================================================
// Batch Resolution with addr2line
// ============================================================================

/**
 * @brief Run addr2line on a batch of addresses and parse results
 * @param buffer Array of addresses
 * @param size Number of addresses
 * @return Array of symbol strings (must be freed by caller)
 */
static char **run_addr2line_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    return NULL;
  }

  // Get executable path
  char exe_path[1024];
#ifdef __linux__
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) {
    return NULL;
  }
  exe_path[len] = '\0';
#elif defined(__APPLE__)
  uint32_t bufsize = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &bufsize) != 0) {
    return NULL;
  }
#else
  return NULL; // Unsupported platform
#endif

  // Build addr2line command
  char cmd[4096];
  int offset = snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -i ", exe_path);
  if (offset <= 0 || offset >= (int)sizeof(cmd)) {
    return NULL;
  }

  for (int i = 0; i < size; i++) {
    int n = snprintf(cmd + offset, sizeof(cmd) - offset, "0x%lx ", (unsigned long)buffer[i]);
    if (n <= 0 || offset + n >= (int)sizeof(cmd)) {
      break;
    }
    offset += n;
  }

  // Execute addr2line
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    return NULL;
  }

  // Allocate result array
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    pclose(fp);
    return NULL;
  }

  // Parse output
  for (int i = 0; i < size; i++) {
    char func_name[256];
    char file_line[512];

    if (fgets(func_name, sizeof(func_name), fp) == NULL) {
      break;
    }
    if (fgets(file_line, sizeof(file_line), fp) == NULL) {
      break;
    }

    // Remove newlines
    func_name[strcspn(func_name, "\n")] = '\0';
    file_line[strcspn(file_line, "\n")] = '\0';

    // Extract relative path
    const char *rel_path = extract_project_relative_path(file_line);

    // Allocate result buffer
    result[i] = SAFE_MALLOC(1024, char *);
    if (!result[i]) {
      break;
    }

    // Format symbol
    bool has_func = (strcmp(func_name, "??") != 0);
    bool has_file = (strcmp(file_line, "??:0") != 0 && strcmp(file_line, "??:?") != 0);

    if (!has_func && !has_file) {
      // Complete unknown - show raw address
      SAFE_SNPRINTF(result[i], 1024, "%p", buffer[i]);
    } else if (has_func && has_file) {
      // Best case - both function and file:line known
      if (strstr(rel_path, ":") != NULL) {
        SAFE_SNPRINTF(result[i], 1024, "%s in %s()", rel_path, func_name);
      } else {
        SAFE_SNPRINTF(result[i], 1024, "%s() at %s", func_name, rel_path);
      }
    } else if (has_func) {
      // Function known but file unknown (common for library functions)
      SAFE_SNPRINTF(result[i], 1024, "%s() at %p", func_name, buffer[i]);
    } else {
      // File known but function unknown (rare)
      SAFE_SNPRINTF(result[i], 1024, "%s (unknown function)", rel_path);
    }
  }

  pclose(fp);
  return result;
}

char **symbol_cache_resolve_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    return NULL;
  }

  // DO NOT auto-initialize here - causes circular dependency during lock_debug_init()
  // The cache must be initialized explicitly by platform_init() before use
  if (!atomic_load(&g_symbol_cache_initialized)) {
    // Cache not initialized - fall back to uncached resolution
    // This happens during early initialization before platform_init() completes
    return run_addr2line_batch(buffer, size);
  }

  // Allocate result array (size + 1 for NULL terminator)
  // CALLOC zeros the memory, so result[size] is already NULL
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    return NULL;
  }

  // Ensure NULL terminator is explicitly set (CALLOC already did this, but be explicit)
  result[size] = NULL;

  // First pass: check cache for all addresses
  int uncached_count = 0;
  void *uncached_addrs[size];
  int uncached_indices[size];

  for (int i = 0; i < size; i++) {
    const char *cached = symbol_cache_lookup(buffer[i]);
    if (cached) {
      // Cache hit - duplicate the string
      result[i] = platform_strdup(cached);
      // If allocation failed, use sentinel string instead of NULL
      if (!result[i]) {
        result[i] = platform_strdup(NULL_SENTINEL);
      }
    } else {
      // Cache miss - track for batch resolution
      uncached_addrs[uncached_count] = buffer[i];
      uncached_indices[uncached_count] = i;
      uncached_count++;
    }
  }

  // Second pass: resolve uncached addresses with addr2line
  if (uncached_count > 0) {
    char **resolved = run_addr2line_batch(uncached_addrs, uncached_count);

    if (resolved) {
      for (int i = 0; i < uncached_count; i++) {
        if (resolved[i]) {
          int orig_idx = uncached_indices[i];
          result[orig_idx] = platform_strdup(resolved[i]);

          // If strdup failed, use sentinel string instead of NULL
          if (!result[orig_idx]) {
            result[orig_idx] = platform_strdup(NULL_SENTINEL);
          }

          // Only insert into cache if strdup succeeded (and it's not the sentinel)
          if (result[orig_idx] && strcmp(result[orig_idx], NULL_SENTINEL) != 0) {
            symbol_cache_insert(uncached_addrs[i], resolved[i]);
          }

          SAFE_FREE(resolved[i]);
        } else {
          // resolved[i] is NULL - use sentinel string
          int orig_idx = uncached_indices[i];
          if (!result[orig_idx]) {
            result[orig_idx] = platform_strdup(NULL_SENTINEL);
            // If platform_strdup fails (out of memory), we'll have NULL in the middle
            // but this should be extremely rare
          }
        }
      }
      SAFE_FREE(resolved);
    } else {
      // addr2line failed - fill uncached entries with raw addresses or sentinel
      for (int i = 0; i < uncached_count; i++) {
        int orig_idx = uncached_indices[i];
        if (!result[orig_idx]) {
          result[orig_idx] = SAFE_MALLOC(32, char *);
          if (result[orig_idx]) {
            SAFE_SNPRINTF(result[orig_idx], 32, "%p", uncached_addrs[i]);
          } else {
            // Even SAFE_MALLOC failed - use sentinel string
            result[orig_idx] = platform_strdup(NULL_SENTINEL);
            // If platform_strdup fails (out of memory), we'll have NULL in the middle
            // but this should be extremely rare
          }
        }
      }
    }
  }

  return result;
}

void symbol_cache_free_symbols(char **symbols) {
  if (!symbols) {
    return;
  }

  // The array is NULL-terminated (allocated with size+1, with result[size] = NULL)
  // The terminator is a SINGLE NULL at index 'size'
  // Failed allocations use the NULL_SENTINEL string "[NULL]" instead of NULL,
  // so there are no NULL entries in the middle - only at the terminator
  // This makes iteration safe: we can iterate until we find the first NULL (the terminator)

  // Iterate through entries, freeing all non-NULL entries until we hit the NULL terminator
  for (int i = 0; i < 64; i++) { // Reasonable limit to prevent infinite loop
    if (symbols[i] == NULL) {
      // Found NULL - this is the terminator, stop here
      break;
    }

    // Found a non-NULL entry - check if it's the sentinel string
    // Both regular strings and sentinel strings are allocated (with strdup),
    // so we free them all
    SAFE_FREE(symbols[i]);
    symbols[i] = NULL; // Clear pointer after freeing
  }

  // Free the array itself
  SAFE_FREE(symbols);
}

#endif // !_WIN32
