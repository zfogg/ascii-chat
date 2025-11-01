/**
 * @file symbols.c (Windows stub)
 * @brief Symbol resolution cache stub for Windows
 *
 * Windows stub implementation - no addr2line support on Windows.
 * Returns raw addresses instead of resolved symbols.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#ifdef _WIN32

#include "../symbols.h"
#include "../../common.h"
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Public API Stub Implementation
// ============================================================================

int symbol_cache_init(void) {
  // No-op on Windows
  return 0;
}

void symbol_cache_cleanup(void) {
  // No-op on Windows
}

const char *symbol_cache_lookup(void *addr) {
  (void)addr;
  return NULL; // Always miss on Windows
}

bool symbol_cache_insert(void *addr, const char *symbol) {
  (void)addr;
  (void)symbol;
  return false; // Not supported on Windows
}

void symbol_cache_get_stats(uint64_t *hits_out, uint64_t *misses_out, size_t *entries_out) {
  if (hits_out)
    *hits_out = 0;
  if (misses_out)
    *misses_out = 0;
  if (entries_out)
    *entries_out = 0;
}

void symbol_cache_print_stats(void) {
  // No-op on Windows
}

char **symbol_cache_resolve_batch(void *const *buffer, int size) {
  if (size <= 0 || !buffer) {
    return NULL;
  }

  // Allocate result array
  char **result = SAFE_CALLOC((size_t)(size + 1), sizeof(char *), char **);
  if (!result) {
    return NULL;
  }

  // Just return raw addresses on Windows
  for (int i = 0; i < size; i++) {
    result[i] = SAFE_MALLOC(32, char *);
    if (result[i]) {
      SAFE_SNPRINTF(result[i], 32, "%p", buffer[i]);
    }
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

#endif // _WIN32
