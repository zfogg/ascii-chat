#include "common.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>

void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity) {
  const double MB = 1024.0 * 1024.0;
  const double GB = MB * 1024.0;
  const double TB = GB * 1024.0;

  if ((double)bytes < MB) {
    snprintf(out, out_capacity, "%zu B", bytes);
  } else if ((double)bytes < GB) {
    double value = (double)bytes / MB;
    snprintf(out, out_capacity, "%.2f MB", value);
  } else if ((double)bytes < TB) {
    double value = (double)bytes / GB;
    snprintf(out, out_capacity, "%.2f GB", value);
  } else {
    double value = (double)bytes / TB;
    snprintf(out, out_capacity, "%.2f TB", value);
  }
}

/* ============================================================================
 * Memory Debugging (Debug builds only)
 * ============================================================================
 */

#ifdef DEBUG_MEMORY

typedef struct mem_block {
  void *ptr;
  size_t size;
  char file[256];
  int line;
  struct mem_block *next;
} mem_block_t;

static struct {
  mem_block_t *head;
  size_t total_allocated;
  size_t total_freed;
  size_t current_usage;
  size_t peak_usage;
  size_t malloc_calls;
  size_t free_calls;
  size_t calloc_calls;
  size_t realloc_calls;
  pthread_mutex_t mutex;
  bool quiet_mode; /* Control stderr output for memory report */
} g_mem = {.head = NULL,
           .total_allocated = 0,
           .total_freed = 0,
           .current_usage = 0,
           .peak_usage = 0,
           .malloc_calls = 0,
           .free_calls = 0,
           .calloc_calls = 0,
           .realloc_calls = 0,
           .mutex = PTHREAD_MUTEX_INITIALIZER,
           .quiet_mode = false};

/* Use real libc allocators inside debug allocator to avoid recursion */
#undef malloc
#undef free
#undef calloc
#undef realloc

void *debug_malloc(size_t size, const char *file, int line) {
  void *ptr = malloc(size);
  if (!ptr)
    return NULL;

  pthread_mutex_lock(&g_mem.mutex);

  g_mem.malloc_calls++;

  mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
  if (block) {
    block->ptr = ptr;
    block->size = size;
    SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
    block->line = line;
    block->next = g_mem.head;
    g_mem.head = block;

    g_mem.total_allocated += size;
    g_mem.current_usage += size;
    if (g_mem.current_usage > g_mem.peak_usage) {
      g_mem.peak_usage = g_mem.current_usage;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return ptr;
}

void debug_free(void *ptr, const char *file, int line) {
  if (!ptr)
    return;

  pthread_mutex_lock(&g_mem.mutex);

  g_mem.free_calls++;

  mem_block_t *prev = NULL;
  mem_block_t *curr = g_mem.head;

  while (curr) {
    if (curr->ptr == ptr) {
      if (prev) {
        prev->next = curr->next;
      } else {
        g_mem.head = curr->next;
      }

      g_mem.total_freed += curr->size;
      g_mem.current_usage -= curr->size;

      free(curr);
      break;
    }
    prev = curr;
    curr = curr->next;
  }

  if (!curr) {
    log_warn("Freeing untracked pointer %p at %s:%d", ptr, file, line);
  }

  pthread_mutex_unlock(&g_mem.mutex);

  free(ptr);
}

void *debug_calloc(size_t count, size_t size, const char *file, int line) {
  size_t total = count * size;
  void *ptr = calloc(count, size);
  if (!ptr)
    return NULL;

  pthread_mutex_lock(&g_mem.mutex);

  g_mem.calloc_calls++;

  mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
  if (block) {
    block->ptr = ptr;
    block->size = total;
    SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
    block->line = line;
    block->next = g_mem.head;
    g_mem.head = block;

    g_mem.total_allocated += total;
    g_mem.current_usage += total;
    if (g_mem.current_usage > g_mem.peak_usage) {
      g_mem.peak_usage = g_mem.current_usage;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return ptr;
}

void *debug_realloc(void *ptr, size_t size, const char *file, int line) {
  // Count the realloc call even if it delegates to malloc or free
  pthread_mutex_lock(&g_mem.mutex);
  g_mem.realloc_calls++;
  pthread_mutex_unlock(&g_mem.mutex);

  if (ptr == NULL) {
    return debug_malloc(size, file, line);
  }
  if (size == 0) {
    debug_free(ptr, file, line);
    return NULL;
  }

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *prev = NULL;
  mem_block_t *curr = g_mem.head;
  while (curr && curr->ptr != ptr) {
    prev = curr;
    curr = curr->next;
  }

  (void)prev; // Unused variable (for now 🤔 what could i do with this...)

  void *new_ptr = realloc(ptr, size);
  if (!new_ptr) {
    pthread_mutex_unlock(&g_mem.mutex);
    return NULL;
  }

  if (curr) {
    if (size >= curr->size) {
      size_t delta = size - curr->size;
      g_mem.total_allocated += delta;
      g_mem.current_usage += delta;
      if (g_mem.current_usage > g_mem.peak_usage) {
        g_mem.peak_usage = g_mem.current_usage;
      }
    } else {
      size_t delta = curr->size - size;
      g_mem.total_freed += delta;
      g_mem.current_usage -= delta;
    }
    curr->ptr = new_ptr;
    curr->size = size;
    SAFE_STRNCPY(curr->file, file, sizeof(curr->file) - 1);
    curr->file[sizeof(curr->file) - 1] = '\0';
    curr->line = line;
  } else {
    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = new_ptr;
      block->size = size;
      SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;

      g_mem.total_allocated += size;
      g_mem.current_usage += size;
      if (g_mem.current_usage > g_mem.peak_usage) {
        g_mem.peak_usage = g_mem.current_usage;
      }
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return new_ptr;
}

void debug_memory_set_quiet_mode(bool quiet) {
  pthread_mutex_lock(&g_mem.mutex);
  g_mem.quiet_mode = quiet;
  pthread_mutex_unlock(&g_mem.mutex);
}

void debug_memory_report(void) {
  bool quiet = g_mem.quiet_mode;
  if (!quiet) {
    pthread_mutex_lock(&g_mem.mutex);
    // NOTE: Write directly to stderr in case logging is already destroyed at exit
    fprintf(stderr, "\n=== Memory Report ===\n");

    char pretty_total[64];
    char pretty_freed[64];
    char pretty_current[64];
    char pretty_peak[64];
    format_bytes_pretty(g_mem.total_allocated, pretty_total, sizeof(pretty_total));
    format_bytes_pretty(g_mem.total_freed, pretty_freed, sizeof(pretty_freed));
    format_bytes_pretty(g_mem.current_usage, pretty_current, sizeof(pretty_current));
    format_bytes_pretty(g_mem.peak_usage, pretty_peak, sizeof(pretty_peak));

    fprintf(stderr, "Total allocated: %s\n", pretty_total);
    fprintf(stderr, "Total freed: %s\n", pretty_freed);
    fprintf(stderr, "Current usage: %s\n", pretty_current);
    fprintf(stderr, "Peak usage: %s\n", pretty_peak);
    fprintf(stderr, "malloc calls: %zu\n", g_mem.malloc_calls);
    fprintf(stderr, "calloc calls: %zu\n", g_mem.calloc_calls);
    // fprintf(stderr, "realloc calls: %zu\n", g_mem.realloc_calls);
    fprintf(stderr, "free calls: %zu\n", g_mem.free_calls);
    size_t diff = (g_mem.malloc_calls + g_mem.calloc_calls) - g_mem.free_calls;
    fprintf(stderr, "(malloc calls + calloc calls) - free calls = %zd\n", (ssize_t)diff);

    if (g_mem.head) {
      fprintf(stderr, "\nCurrent allocations:\n");
      mem_block_t *curr = g_mem.head;
      while (curr) {
        char pretty_size[64];
        format_bytes_pretty(curr->size, pretty_size, sizeof(pretty_size));
        fprintf(stderr, "  - %s:%d - %s\n", curr->file, curr->line, pretty_size);
        curr = curr->next;
      }
    }

    pthread_mutex_unlock(&g_mem.mutex);
  }
}

#endif /* DEBUG_MEMORY */

/* ============================================================================
 * New Function for Coverage Testing
 * ============================================================================
 */

/**
 * Calculate memory usage statistics for coverage testing
 * This function will be covered by our new test
 */
void calculate_memory_stats(size_t *total_allocated, size_t *total_freed, size_t *current_usage) {
  if (!total_allocated || !total_freed || !current_usage) {
    return;
  }

#ifdef DEBUG_MEMORY
  pthread_mutex_lock(&g_mem.mutex);
  *total_allocated = g_mem.total_allocated;
  *total_freed = g_mem.total_freed;
  *current_usage = g_mem.current_usage;
  pthread_mutex_unlock(&g_mem.mutex);
#else
  // In non-debug builds, return zeros
  *total_allocated = 0;
  *total_freed = 0;
  *current_usage = 0;
#endif
}

/**
 * Validate memory pattern in a buffer
 * This function tests a new code path for coverage
 */
bool validate_memory_pattern(const void *ptr, size_t size, uint8_t expected_pattern) {
  if (!ptr || size == 0) {
    return false;
  }

  const uint8_t *byte_ptr = (const uint8_t *)ptr;

  // Check first and last bytes
  if (byte_ptr[0] != expected_pattern || byte_ptr[size - 1] != expected_pattern) {
    return false;
  }

  // Check every 100th byte for large buffers
  if (size > 1000) {
    for (size_t i = 100; i < size - 100; i += 100) {
      if (byte_ptr[i] != expected_pattern) {
        return false;
      }
    }
  }

  return true;
}
// Test comment for codecov
