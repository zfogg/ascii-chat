// SPDX-License-Identifier: MIT
// Memory debugging implementation for ascii-chat debug builds

#if defined(DEBUG_MEMORY) && !defined(NDEBUG)

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "debug/memory.h"
#include "common.h"
#include "platform/mutex.h"
#include "platform/system.h"
#include "util/format.h"
#include "util/path.h"

#ifdef _WIN32
#if defined(_MSC_VER)
#include <excpt.h>
#endif
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <features.h>
#include <malloc.h>
#if !defined(_GNU_SOURCE) || !defined(__GLIBC__)
extern size_t malloc_usable_size(void *ptr);
#endif
#endif

typedef struct mem_block {
  void *ptr;
  size_t size;
  char file[256];
  int line;
  bool is_aligned;
  struct mem_block *next;
} mem_block_t;

static __thread bool g_in_debug_memory = false;

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
  mutex_t mutex;
  atomic_int mutex_state;
  bool quiet_mode;
} g_mem = {.head = NULL,
           .total_allocated = 0,
           .total_freed = 0,
           .current_usage = 0,
           .peak_usage = 0,
           .malloc_calls = 0,
           .free_calls = 0,
           .calloc_calls = 0,
           .realloc_calls = 0,
           .mutex_state = 0,
           .quiet_mode = false};

#undef malloc
#undef free
#undef calloc
#undef realloc

static atomic_flag g_logged_mutex_init_failure = ATOMIC_FLAG_INIT;

static bool ensure_mutex_initialized(void) {
  for (;;) {
    int state = atomic_load_explicit(&g_mem.mutex_state, memory_order_acquire);
    if (state == 2) {
      return true;
    }

    if (state == 0) {
      int expected = 0;
      if (atomic_compare_exchange_strong_explicit(&g_mem.mutex_state, &expected, 1, memory_order_acq_rel,
                                                  memory_order_acquire)) {
        if (mutex_init(&g_mem.mutex) == 0) {
          atomic_store_explicit(&g_mem.mutex_state, 2, memory_order_release);
          return true;
        }

        atomic_store_explicit(&g_mem.mutex_state, 0, memory_order_release);
        if (!atomic_flag_test_and_set(&g_logged_mutex_init_failure)) {
          log_error("Failed to initialize debug memory mutex; memory tracking will run without locking");
        }
        return false;
      }
      continue;
    }

    platform_sleep_ms(1);
  }
}

void *debug_malloc(size_t size, const char *file, int line) {
  void *ptr = (void *)malloc(size);
  if (!ptr)
    return NULL;

  if (g_in_debug_memory) {
    return ptr;
  }

  g_in_debug_memory = true;

  atomic_fetch_add(&g_mem.malloc_calls, 1);
  atomic_fetch_add(&g_mem.total_allocated, size);
  size_t new_usage = atomic_fetch_add(&g_mem.current_usage, size) + size;

  size_t peak = atomic_load(&g_mem.peak_usage);
  while (new_usage > peak) {
    if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
      break;
  }

  bool have_mutex = ensure_mutex_initialized();
  if (have_mutex) {
    mutex_lock(&g_mem.mutex);

    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = ptr;
      block->size = size;
      block->is_aligned = false;
      const char *normalized_file = extract_project_relative_path(file);
      SAFE_STRNCPY(block->file, normalized_file, sizeof(block->file) - 1);
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;
    }

    mutex_unlock(&g_mem.mutex);
  }

  g_in_debug_memory = false;
  return ptr;
}

void debug_track_aligned(void *ptr, size_t size, const char *file, int line) {
  if (!ptr)
    return;

  if (g_in_debug_memory) {
    return;
  }

  g_in_debug_memory = true;

  atomic_fetch_add(&g_mem.malloc_calls, 1);
  atomic_fetch_add(&g_mem.total_allocated, size);
  size_t new_usage = atomic_fetch_add(&g_mem.current_usage, size) + size;

  size_t peak = atomic_load(&g_mem.peak_usage);
  while (new_usage > peak) {
    if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
      break;
  }

  bool have_mutex = ensure_mutex_initialized();
  if (have_mutex) {
    mutex_lock(&g_mem.mutex);

    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = ptr;
      block->size = size;
      block->is_aligned = true;
      const char *normalized_file = extract_project_relative_path(file);
      SAFE_STRNCPY(block->file, normalized_file, sizeof(block->file) - 1);
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;
    }

    mutex_unlock(&g_mem.mutex);
  }

  g_in_debug_memory = false;
}

void debug_free(void *ptr, const char *file, int line) {
  if (!ptr)
    return;

  if (g_in_debug_memory) {
    free(ptr);
    return;
  }

  g_in_debug_memory = true;

  atomic_fetch_add(&g_mem.free_calls, 1);

  size_t freed_size = 0;
  bool found = false;
#ifdef _WIN32
  bool was_aligned = false;
#endif

  bool have_mutex = ensure_mutex_initialized();
  if (have_mutex) {
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
#ifdef _WIN32
        was_aligned = curr->is_aligned;
#endif
        free(curr);
        break;
      }
      prev = curr;
      curr = curr->next;
    }

    if (!found) {
      log_warn_every(1000000, "Freeing untracked pointer %p at %s:%d", ptr, file, line);
      platform_print_backtrace(1);
    }

    mutex_unlock(&g_mem.mutex);
  } else {
    log_warn_every(1000000, "Debug memory mutex unavailable while freeing %p at %s:%d", ptr, file, line);
  }

  if (found) {
    atomic_fetch_add(&g_mem.total_freed, freed_size);
    atomic_fetch_sub(&g_mem.current_usage, freed_size);
  } else {
    size_t real_size = 0;
#ifdef _WIN32
#if defined(_MSC_VER)
    __try {
      real_size = _msize(ptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      real_size = 0;
    }
#else
    real_size = _msize(ptr);
#endif
#elif defined(__APPLE__)
    real_size = malloc_size(ptr);
#elif defined(__linux__)
    real_size = malloc_usable_size(ptr);
#endif

    if (real_size > 0) {
      atomic_fetch_add(&g_mem.total_freed, real_size);
      atomic_fetch_sub(&g_mem.current_usage, real_size);
    }
  }

#ifdef _WIN32
  if (was_aligned) {
    _aligned_free(ptr);
  } else {
    free(ptr);
  }
#else
  free(ptr);
#endif

  g_in_debug_memory = false;
}

void *debug_calloc(size_t count, size_t size, const char *file, int line) {
  size_t total = count * size;
  void *ptr = calloc(count, size);
  if (!ptr)
    return NULL;

  if (g_in_debug_memory) {
    return ptr;
  }

  g_in_debug_memory = true;

  atomic_fetch_add(&g_mem.calloc_calls, 1);
  atomic_fetch_add(&g_mem.total_allocated, total);
  size_t new_usage = atomic_fetch_add(&g_mem.current_usage, total) + total;

  size_t peak = atomic_load(&g_mem.peak_usage);
  while (new_usage > peak) {
    if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
      break;
  }

  bool have_mutex = ensure_mutex_initialized();
  if (have_mutex) {
    mutex_lock(&g_mem.mutex);

    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = ptr;
      block->size = total;
      block->is_aligned = false;
      SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;
    }

    mutex_unlock(&g_mem.mutex);
  }

  g_in_debug_memory = false;
  return ptr;
}

void *debug_realloc(void *ptr, size_t size, const char *file, int line) {
  if (g_in_debug_memory) {
    return realloc(ptr, size);
  }

  g_in_debug_memory = true;

  atomic_fetch_add(&g_mem.realloc_calls, 1);

  if (ptr == NULL) {
    g_in_debug_memory = false;
    return debug_malloc(size, file, line);
  }
  if (size == 0) {
    g_in_debug_memory = false;
    debug_free(ptr, file, line);
    return NULL;
  }

  size_t old_size = 0;
  bool have_mutex = ensure_mutex_initialized();

  if (have_mutex) {
    mutex_lock(&g_mem.mutex);

    mem_block_t *curr = g_mem.head;
    while (curr && curr->ptr != ptr) {
      curr = curr->next;
    }
    if (curr) {
      old_size = curr->size;
    }

    mutex_unlock(&g_mem.mutex);
  } else {
    log_warn_every(1000000, "Debug memory mutex unavailable while reallocating %p at %s:%d", ptr, file, line);
  }

  void *new_ptr = SAFE_REALLOC(ptr, size, void *);
  if (!new_ptr) {
    g_in_debug_memory = false;
    return NULL;
  }

  if (old_size > 0) {
    if (size >= old_size) {
      size_t delta = size - old_size;
      atomic_fetch_add(&g_mem.total_allocated, delta);
      size_t new_usage = atomic_fetch_add(&g_mem.current_usage, delta) + delta;

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
    atomic_fetch_add(&g_mem.total_allocated, size);
    size_t new_usage = atomic_fetch_add(&g_mem.current_usage, size) + size;

    size_t peak = atomic_load(&g_mem.peak_usage);
    while (new_usage > peak) {
      if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
        break;
    }
  }

  if (ensure_mutex_initialized()) {
    mutex_lock(&g_mem.mutex);

    mem_block_t *curr = g_mem.head;
    while (curr && curr->ptr != ptr) {
      curr = curr->next;
    }

    if (curr) {
      curr->ptr = new_ptr;
      curr->size = size;
      curr->is_aligned = false;
      SAFE_STRNCPY(curr->file, file, sizeof(curr->file) - 1);
      curr->file[sizeof(curr->file) - 1] = '\0';
      curr->line = line;
    } else {
      mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
      if (block) {
        block->ptr = new_ptr;
        block->size = size;
        block->is_aligned = false;
        SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
        block->line = line;
        block->next = g_mem.head;
        g_mem.head = block;
      }
    }

    mutex_unlock(&g_mem.mutex);
  } else {
    log_warn_every(1000000, "Debug memory mutex unavailable while updating realloc block %p -> %p at %s:%d", ptr,
                   new_ptr, file, line);
  }

  g_in_debug_memory = false;
  return new_ptr;
}

void debug_memory_set_quiet_mode(bool quiet) {
  g_mem.quiet_mode = quiet;
}

static const char *strip_project_path(const char *full_path) {
  return extract_project_relative_path(full_path);
}

void debug_memory_report(void) {
  extern void asciichat_errno_cleanup(void);
  asciichat_errno_cleanup();

  bool quiet = g_mem.quiet_mode;
  if (!quiet) {
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "\n=== Memory Report ===\n"));

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

    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "Total allocated: %s\n", pretty_total));
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "Total freed: %s\n", pretty_freed));
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "Current usage: %s\n", pretty_current));
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "Peak usage: %s\n", pretty_peak));
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "malloc calls: %zu\n", malloc_calls));
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "calloc calls: %zu\n", calloc_calls));
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "free calls: %zu\n", free_calls));
    size_t diff = (malloc_calls + calloc_calls) - free_calls;
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "(malloc calls + calloc calls) - free calls = %zu\n", diff));

    if (g_mem.head) {
      if (ensure_mutex_initialized()) {
        mutex_lock(&g_mem.mutex);

        SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "\nCurrent allocations:\n"));
        mem_block_t *curr = g_mem.head;
        while (curr) {
          char pretty_size[64];
          format_bytes_pretty(curr->size, pretty_size, sizeof(pretty_size));
          SAFE_IGNORE_PRINTF_RESULT(
              safe_fprintf(stderr, "  - %s:%d - %s\n", strip_project_path(curr->file), curr->line, pretty_size));
          curr = curr->next;
        }

        mutex_unlock(&g_mem.mutex);
      } else {
        SAFE_IGNORE_PRINTF_RESULT(
            safe_fprintf(stderr, "\nCurrent allocations unavailable: failed to initialize debug memory mutex\n"));
      }
    }
  }
}

#elif defined(DEBUG_MEMORY)

void debug_memory_set_quiet_mode(bool quiet) {
  (void)quiet;
}

void debug_memory_report(void) {}

void *debug_malloc(size_t size, const char *file, int line) {
  (void)file;
  (void)line;
  return malloc(size);
}

void *debug_calloc(size_t count, size_t size, const char *file, int line) {
  (void)file;
  (void)line;
  return calloc(count, size);
}

void *debug_realloc(void *ptr, size_t size, const char *file, int line) {
  (void)file;
  (void)line;
  return realloc(ptr, size);
}

void debug_free(void *ptr, const char *file, int line) {
  (void)file;
  (void)line;
  free(ptr);
}

void debug_track_aligned(void *ptr, size_t size, const char *file, int line) {
  (void)ptr;
  (void)size;
  (void)file;
  (void)line;
}

#endif


