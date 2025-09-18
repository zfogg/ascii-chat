#include "common.h"
#include "platform/abstraction.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

// Global frame rate variable - can be set via command line
int g_max_fps = 0; // 0 means use default

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
  atomic_size_t total_allocated;
  atomic_size_t total_freed;
  atomic_size_t current_usage;
  atomic_size_t peak_usage;
  atomic_size_t malloc_calls;
  atomic_size_t free_calls;
  atomic_size_t calloc_calls;
  atomic_size_t realloc_calls;
  mutex_t mutex; /* Only for linked list operations */
  bool mutex_initialized;
  bool quiet_mode;        /* Control stderr output for memory report */
  bool track_allocations; /* Whether to maintain the linked list */
} g_mem = {.head = NULL,
           .total_allocated = 0,
           .total_freed = 0,
           .current_usage = 0,
           .peak_usage = 0,
           .malloc_calls = 0,
           .free_calls = 0,
           .calloc_calls = 0,
           .realloc_calls = 0,
           .mutex_initialized = false,
           .quiet_mode = false,
#ifdef DEBUG_MEMORY_FULL_TRACKING
           .track_allocations = true
#else
           .track_allocations = false
#endif
};

/* Use real libc allocators inside debug allocator to avoid recursion */
#undef malloc
#undef free
#undef calloc
#undef realloc

static void ensure_mutex_initialized(void) {
  if (!g_mem.mutex_initialized) {
    mutex_init(&g_mem.mutex);
    g_mem.mutex_initialized = true;
  }
}

void *debug_malloc(size_t size, const char *file, int line) {
  void *ptr = malloc(size);
  if (!ptr)
    return NULL;

  /* Update statistics with atomic operations - no locks needed */
  atomic_fetch_add(&g_mem.malloc_calls, 1);
  atomic_fetch_add(&g_mem.total_allocated, size);
  size_t new_usage = atomic_fetch_add(&g_mem.current_usage, size) + size;

  /* Update peak usage if needed */
  size_t peak = atomic_load(&g_mem.peak_usage);
  while (new_usage > peak) {
    if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
      break;
  }

  /* Only track individual allocations if enabled */
  if (g_mem.track_allocations) {
    ensure_mutex_initialized();
    mutex_lock(&g_mem.mutex);

    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = ptr;
      block->size = size;
      SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;
    }

    mutex_unlock(&g_mem.mutex);
  }

  return ptr;
}

void debug_free(void *ptr, const char *file, int line) {
  if (!ptr)
    return;

  atomic_fetch_add(&g_mem.free_calls, 1);

  size_t freed_size = 0;
  bool found = false;

  /* Only search for allocation if tracking is enabled */
  if (g_mem.track_allocations) {
    ensure_mutex_initialized();
    mutex_lock(&g_mem.mutex);

    mem_block_t *prev = NULL;
    mem_block_t *curr = g_mem.head;

    while (curr) {
      if (curr->ptr == ptr) {
        if (prev) {
          prev->next = curr->next;
        } else {
          g_mem.head = curr->next;
        }

        freed_size = curr->size;
        found = true;
        free(curr);
        break;
      }
      prev = curr;
      curr = curr->next;
    }

    if (!found && g_mem.track_allocations) {
      log_warn("Freeing untracked pointer %p at %s:%d", ptr, file, line);
    }

    mutex_unlock(&g_mem.mutex);
  }

  /* Update stats atomically after releasing mutex */
  if (found) {
    atomic_fetch_add(&g_mem.total_freed, freed_size);
    atomic_fetch_sub(&g_mem.current_usage, freed_size);
  }

  free(ptr);
}

void *debug_calloc(size_t count, size_t size, const char *file, int line) {
  size_t total = count * size;
  void *ptr = calloc(count, size);
  if (!ptr)
    return NULL;

  /* Update statistics atomically */
  atomic_fetch_add(&g_mem.calloc_calls, 1);
  atomic_fetch_add(&g_mem.total_allocated, total);
  size_t new_usage = atomic_fetch_add(&g_mem.current_usage, total) + total;

  /* Update peak usage if needed */
  size_t peak = atomic_load(&g_mem.peak_usage);
  while (new_usage > peak) {
    if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
      break;
  }

  /* Only track individual allocations if enabled */
  if (g_mem.track_allocations) {
    ensure_mutex_initialized();
    mutex_lock(&g_mem.mutex);

    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = ptr;
      block->size = total;
      SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;
    }

    mutex_unlock(&g_mem.mutex);
  }

  return ptr;
}

void *debug_realloc(void *ptr, size_t size, const char *file, int line) {
  // Count the realloc call even if it delegates to malloc or free
  atomic_fetch_add(&g_mem.realloc_calls, 1);

  if (ptr == NULL) {
    return debug_malloc(size, file, line);
  }
  if (size == 0) {
    debug_free(ptr, file, line);
    return NULL;
  }

  size_t old_size = 0;
  mem_block_t *curr = NULL;

  /* Find the old allocation if tracking is enabled */
  if (g_mem.track_allocations) {
    ensure_mutex_initialized();
    mutex_lock(&g_mem.mutex);

    mem_block_t *prev = NULL;
    curr = g_mem.head;
    while (curr && curr->ptr != ptr) {
      prev = curr;
      curr = curr->next;
    }
    if (curr) {
      old_size = curr->size;
    }
    (void)prev; // Unused variable (for now 🤔 what could i do with this...)
    mutex_unlock(&g_mem.mutex);
  }

  void *new_ptr = realloc(ptr, size);
  if (!new_ptr) {
    return NULL;
  }

  /* Update statistics atomically */
  if (old_size > 0) {
    if (size >= old_size) {
      size_t delta = size - old_size;
      atomic_fetch_add(&g_mem.total_allocated, delta);
      size_t new_usage = atomic_fetch_add(&g_mem.current_usage, delta) + delta;

      /* Update peak usage if needed */
      size_t peak = atomic_load(&g_mem.peak_usage);
      while (new_usage > peak) {
        if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
          break;
      }
    } else {
      size_t delta = old_size - size;
      atomic_fetch_add(&g_mem.total_freed, delta);
      atomic_fetch_sub(&g_mem.current_usage, delta);
    }
  } else {
    /* New allocation */
    atomic_fetch_add(&g_mem.total_allocated, size);
    size_t new_usage = atomic_fetch_add(&g_mem.current_usage, size) + size;

    /* Update peak usage if needed */
    size_t peak = atomic_load(&g_mem.peak_usage);
    while (new_usage > peak) {
      if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
        break;
    }
  }

  /* Update the linked list if tracking is enabled */
  if (g_mem.track_allocations) {
    ensure_mutex_initialized();
    mutex_lock(&g_mem.mutex);

    /* Find the block again (it might have changed) */
    curr = g_mem.head;
    while (curr && curr->ptr != ptr) {
      curr = curr->next;
    }

    if (curr) {
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
      }
    }

    mutex_unlock(&g_mem.mutex);
  }

  return new_ptr;
}

void debug_memory_set_quiet_mode(bool quiet) {
  g_mem.quiet_mode = quiet;
}

void debug_memory_report(void) {
  bool quiet = g_mem.quiet_mode;
  if (!quiet) {
    // NOTE: Write directly to stderr in case logging is already destroyed at exit
    fprintf(stderr, "\n=== Memory Report ===\n");

    /* Read atomic values once */
    size_t total_allocated = atomic_load(&g_mem.total_allocated);
    size_t total_freed = atomic_load(&g_mem.total_freed);
    size_t current_usage = atomic_load(&g_mem.current_usage);
    size_t peak_usage = atomic_load(&g_mem.peak_usage);
    size_t malloc_calls = atomic_load(&g_mem.malloc_calls);
    size_t calloc_calls = atomic_load(&g_mem.calloc_calls);
    size_t free_calls = atomic_load(&g_mem.free_calls);

    char pretty_total[64];
    char pretty_freed[64];
    char pretty_current[64];
    char pretty_peak[64];
    format_bytes_pretty(total_allocated, pretty_total, sizeof(pretty_total));
    format_bytes_pretty(total_freed, pretty_freed, sizeof(pretty_freed));
    format_bytes_pretty(current_usage, pretty_current, sizeof(pretty_current));
    format_bytes_pretty(peak_usage, pretty_peak, sizeof(pretty_peak));

    fprintf(stderr, "Total allocated: %s\n", pretty_total);
    fprintf(stderr, "Total freed: %s\n", pretty_freed);
    fprintf(stderr, "Current usage: %s\n", pretty_current);
    fprintf(stderr, "Peak usage: %s\n", pretty_peak);
    fprintf(stderr, "malloc calls: %zu\n", malloc_calls);
    fprintf(stderr, "calloc calls: %zu\n", calloc_calls);
    // fprintf(stderr, "realloc calls: %zu\n", atomic_load(&g_mem.realloc_calls));
    fprintf(stderr, "free calls: %zu\n", free_calls);
    size_t diff = (malloc_calls + calloc_calls) - free_calls;
    fprintf(stderr, "(malloc calls + calloc calls) - free calls = %zu\n", diff);

    /* Only show allocations if tracking is enabled */
    if (g_mem.track_allocations && g_mem.head) {
      ensure_mutex_initialized();
      mutex_lock(&g_mem.mutex);

      fprintf(stderr, "\nCurrent allocations:\n");
      mem_block_t *curr = g_mem.head;
      while (curr) {
        char pretty_size[64];
        format_bytes_pretty(curr->size, pretty_size, sizeof(pretty_size));
        fprintf(stderr, "  - %s:%d - %s\n", curr->file, curr->line, pretty_size);
        curr = curr->next;
      }

      mutex_unlock(&g_mem.mutex);
    }
  }
}

#endif /* DEBUG_MEMORY */
