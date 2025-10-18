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

#include "../symbols.h"
#include "../../common.h"
#include "../../hashtable.h"
#include "../abstraction.h"
#include "../../util/path.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

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
  // Mix the bits to get better distribution
  val = ((val >> 16) ^ val) * 0x45d9f3b;
  val = ((val >> 16) ^ val) * 0x45d9f3b;
  val = (val >> 16) ^ val;
  return (uint32_t)val;
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

  if (g_symbol_cache) {
    // Free all symbol entries (hashtable is already thread-safe)
    hashtable_foreach(g_symbol_cache, cleanup_symbol_entry_callback, NULL);
    hashtable_destroy(g_symbol_cache);
    g_symbol_cache = NULL;
  }

  atomic_store(&g_symbol_cache_initialized, false);

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

bool symbol_cache_insert(void *addr, const char *symbol) {
  if (!atomic_load(&g_symbol_cache_initialized) || !g_symbol_cache || !symbol) {
    return false;
  }

  // Create new entry
  symbol_entry_t *entry = SAFE_MALLOC(sizeof(symbol_entry_t), symbol_entry_t *);
  if (!entry) {
    return false;
  }

  entry->addr = addr;
  entry->symbol = strdup(symbol); // Use strdup for symbol string
  if (!entry->symbol) {
    SAFE_FREE(entry);
    return false;
  }

  uint32_t key = hash_address(addr);

  // Check if entry already exists (race condition prevention)
  // Hashtable is already thread-safe with internal rwlocks
  symbol_entry_t *existing = (symbol_entry_t *)hashtable_lookup(g_symbol_cache, key);
  if (existing && existing->addr == addr) {
    // Entry already exists, free our new entry and return success
    SAFE_FREE(entry->symbol);
    SAFE_FREE(entry);
    return true;
  }

  // Insert new entry (hashtable handles thread safety)
  bool success = hashtable_insert(g_symbol_cache, key, entry);

  if (!success) {
    SAFE_FREE(entry->symbol);
    SAFE_FREE(entry);
    return false;
  }

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

#ifndef _WIN32 // addr2line is POSIX-specific

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
      snprintf(result[i], 1024, "%p", buffer[i]);
    } else if (has_func && has_file) {
      if (strstr(rel_path, ":") != NULL) {
        snprintf(result[i], 1024, "%s in %s()", rel_path, func_name);
      } else {
        snprintf(result[i], 1024, "%s() at %s", func_name, rel_path);
      }
    } else if (has_func) {
      snprintf(result[i], 1024, "%s() at %p", func_name, buffer[i]);
    } else {
      snprintf(result[i], 1024, "%s (unknown function)", rel_path);
    }
  }

  pclose(fp);
  return result;
}

#endif // !_WIN32

char **symbol_cache_resolve_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    return NULL;
  }

  // DO NOT auto-initialize here - causes circular dependency during lock_debug_init()
  // The cache must be initialized explicitly by platform_init() before use
  if (!atomic_load(&g_symbol_cache_initialized)) {
    // Cache not initialized - fall back to uncached resolution
    // This happens during early initialization before platform_init() completes
    #ifndef _WIN32
    return run_addr2line_batch(buffer, size);
    #else
    // Windows: just return raw addresses
    char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
    if (result) {
      for (int i = 0; i < size; i++) {
        result[i] = SAFE_MALLOC(32, char *);
        if (result[i]) {
          snprintf(result[i], 32, "%p", buffer[i]);
        }
      }
    }
    return result;
    #endif
  }

  // Allocate result array
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    return NULL;
  }

  // First pass: check cache for all addresses
  int uncached_count = 0;
  void *uncached_addrs[size];
  int uncached_indices[size];

  for (int i = 0; i < size; i++) {
    const char *cached = symbol_cache_lookup(buffer[i]);
    if (cached) {
      // Cache hit - duplicate the string
      result[i] = strdup(cached);
    } else {
      // Cache miss - track for batch resolution
      uncached_addrs[uncached_count] = buffer[i];
      uncached_indices[uncached_count] = i;
      uncached_count++;
    }
  }

  // Second pass: resolve uncached addresses with addr2line
  if (uncached_count > 0) {
#ifndef _WIN32
    char **resolved = run_addr2line_batch(uncached_addrs, uncached_count);

    if (resolved) {
      for (int i = 0; i < uncached_count; i++) {
        if (resolved[i]) {
          int orig_idx = uncached_indices[i];
          result[orig_idx] = strdup(resolved[i]);

          // Insert into cache
          symbol_cache_insert(uncached_addrs[i], resolved[i]);

          SAFE_FREE(resolved[i]);
        }
      }
      SAFE_FREE(resolved);
    }
#else
    // Windows: just use raw addresses
    for (int i = 0; i < uncached_count; i++) {
      int orig_idx = uncached_indices[i];
      result[orig_idx] = SAFE_MALLOC(32, char *);
      if (result[orig_idx]) {
        snprintf(result[orig_idx], 32, "%p", uncached_addrs[i]);
      }
    }
#endif
  }

  return result;
}

void symbol_cache_free_symbols(char **symbols) {
  if (!symbols) {
    return;
  }

  int i = 0;
  while (symbols[i] != NULL) {
    SAFE_FREE(symbols[i]);
    i++;
  }
  SAFE_FREE(symbols);
}
