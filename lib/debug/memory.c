// SPDX-License-Identifier: MIT
/**
 * @file memory.c
 * @ingroup debug_util
 * @brief 🧠 Memory debugging implementation for ascii-chat debug builds
 */

#if defined(DEBUG_MEMORY) && !defined(NDEBUG)

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "debug/memory.h"
#include "common.h"
#include "common/buffer_sizes.h"
#include "asciichat_errno.h"
#include "platform/mutex.h"
#include "platform/system.h"
#include "platform/memory.h"
#include "util/format.h"
#include "util/path.h"
#include "util/string.h"
#include "util/time.h"
#include "log/logging.h"

typedef struct mem_block {
  void *ptr;
  size_t size;
  char file[BUFFER_SIZE_SMALL];
  int line;
  bool is_aligned;
  void *backtrace_ptrs[16]; // Store up to 16 return addresses
  int backtrace_count;      // Number of frames captured
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
      // Capture backtrace (skip 1 frame for this function)
      block->backtrace_count = platform_backtrace(block->backtrace_ptrs, 16);
      // Ensure valid backtrace count
      if (block->backtrace_count < 0) {
        block->backtrace_count = 0;
      }
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
      // Capture backtrace (skip 1 frame for this function)
      block->backtrace_count = platform_backtrace(block->backtrace_ptrs, 16);
      // Ensure valid backtrace count
      if (block->backtrace_count < 0) {
        block->backtrace_count = 0;
      }
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
      log_warn_every(LOG_RATE_FAST, "Freeing untracked pointer %p at %s:%d", ptr, file, line);
      // Don't print backtrace - log_warn_every already rate-limits the warning
    }

    mutex_unlock(&g_mem.mutex);
  } else {
    log_warn_every(LOG_RATE_FAST, "Debug memory mutex unavailable while freeing %p at %s:%d", ptr, file, line);
  }

  if (found) {
    atomic_fetch_add(&g_mem.total_freed, freed_size);
    atomic_fetch_sub(&g_mem.current_usage, freed_size);
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

/**
 * @brief Custom realloc implementation for debug memory tracking
 *
 * Reallocates a previously allocated memory block, updating internal
 * debug tracking structures. Records call counts, memory usage, and
 * maintains a linked list of allocation blocks for leak tracking.
 *
 * BEHAVIOR:
 * - If ptr is NULL, behaves like debug_malloc (allocate new block)
 * - If size is 0, behaves like debug_free (free block and return NULL)
 * - Otherwise, reallocates existing block and updates tracking
 *
 * MEMORY TRACKING:
 * - Tracks reallocation count globally
 * - Updates total allocated/freed based on size change
 * - Updates current memory usage (increasing or decreasing)
 * - Updates peak memory usage if new usage exceeds previous peak
 * - Maintains allocation metadata (file, line, size)
 *
 * THREAD SAFETY:
 * - Uses thread-local recursion guard (g_in_debug_memory)
 * - Mutex-protected access to allocation list
 * - Falls back to standard realloc if mutex unavailable
 * - Atomic operations for memory statistics
 *
 * RECURSION PREVENTION:
 * - Detects nested debug_realloc calls
 * - Falls back to standard realloc to prevent infinite recursion
 * - Uses thread-local storage for recursion detection
 *
 * @param ptr  Pointer to the memory block to reallocate, or NULL to allocate new
 * @param size New size for the allocation (if 0, frees the block)
 * @param file Source filename where realloc was requested
 * @param line Source line number of the allocation request
 *
 * @return Pointer to the new allocation, or NULL if allocation fails or size==0
 *
 * @note If size==0, the memory is freed and NULL is returned
 * @note If ptr==NULL, behaves like debug_malloc
 * @note If in nested debug tracking, falls back to standard realloc
 * @note Thread-safe when mutex is properly initialized
 *
 * @ingroup debug_util
 */
void *debug_realloc(void *ptr, size_t size, const char *file, int line) {
  // Prevent recursion if we're already in debug memory logic
  if (g_in_debug_memory) {
    return realloc(ptr, size);
  }

  g_in_debug_memory = true;

  // Track number of realloc calls
  atomic_fetch_add(&g_mem.realloc_calls, 1);

  // If ptr == NULL, realloc behaves like malloc
  if (ptr == NULL) {
    g_in_debug_memory = false;
    return debug_malloc(size, file, line);
  }
  // If size == 0, realloc behaves like free
  if (size == 0) {
    g_in_debug_memory = false;
    debug_free(ptr, file, line);
    return NULL;
  }

  // Look up old allocation size from tracking list
  size_t old_size = 0;
  bool have_mutex = ensure_mutex_initialized();

  if (have_mutex) {
    mutex_lock(&g_mem.mutex);

    // Find the existing allocation block in the linked list
    mem_block_t *curr = g_mem.head;
    while (curr && curr->ptr != ptr) {
      curr = curr->next;
    }
    if (curr) {
      old_size = curr->size;
    }

    mutex_unlock(&g_mem.mutex);
  } else {
    log_warn_every(LOG_RATE_FAST, "Debug memory mutex unavailable while reallocating %p at %s:%d", ptr, file, line);
  }

  // Perform actual reallocation
  void *new_ptr = SAFE_REALLOC(ptr, size, void *);
  if (!new_ptr) {
    g_in_debug_memory = false;
    return NULL;
  }

  // Update memory statistics based on size change
  if (old_size > 0) {
    // Block was tracked - update statistics
    if (size >= old_size) {
      // Growing: track additional allocation
      size_t delta = size - old_size;
      atomic_fetch_add(&g_mem.total_allocated, delta);
      size_t new_usage = atomic_fetch_add(&g_mem.current_usage, delta) + delta;

      // Update peak usage if new usage exceeds previous peak
      size_t peak = atomic_load(&g_mem.peak_usage);
      while (new_usage > peak) {
        if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
          break;
      }
    } else {
      // Shrinking: track freed memory
      size_t delta = old_size - size;
      atomic_fetch_add(&g_mem.total_freed, delta);
      atomic_fetch_sub(&g_mem.current_usage, delta);
    }
  } else {
    // Block was not tracked - treat as new allocation
    atomic_fetch_add(&g_mem.total_allocated, size);
    size_t new_usage = atomic_fetch_add(&g_mem.current_usage, size) + size;

    // Update peak usage
    size_t peak = atomic_load(&g_mem.peak_usage);
    while (new_usage > peak) {
      if (atomic_compare_exchange_weak(&g_mem.peak_usage, &peak, new_usage))
        break;
    }
  }

  // Update tracking list with new pointer and metadata
  if (ensure_mutex_initialized()) {
    mutex_lock(&g_mem.mutex);

    // Find existing block in linked list
    mem_block_t *curr = g_mem.head;
    while (curr && curr->ptr != ptr) {
      curr = curr->next;
    }

    if (curr) {
      // Update existing block with new metadata
      curr->ptr = new_ptr;
      curr->size = size;
      curr->is_aligned = false;
      SAFE_STRNCPY(curr->file, file, sizeof(curr->file) - 1);
      curr->file[sizeof(curr->file) - 1] = '\0';
      curr->line = line;
    } else {
      // Block not found - create new tracking entry
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
    log_warn_every(LOG_RATE_FAST, "Debug memory mutex unavailable while updating realloc block %p -> %p at %s:%d", ptr,
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
  asciichat_errno_cleanup();

  // Skip memory report if an action flag was passed (for clean action output)
  extern bool has_action_flag(void);
  if (has_action_flag()) {
    return;
  }

  bool quiet = g_mem.quiet_mode;
  if (!quiet) {
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "\n%s\n", colored_string(LOG_COLOR_DEV, "=== Memory Report ===")));

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

    // Calculate max label width for column alignment
    const char *label_total = "Total allocated:";
    const char *label_freed = "Total freed:";
    const char *label_current = "Current usage:";
    const char *label_peak = "Peak usage:";
    const char *label_malloc = "malloc calls:";
    const char *label_calloc = "calloc calls:";
    const char *label_free = "free calls:";
    const char *label_diff = "unfreed allocations:";

    size_t max_label_width = 0;
    max_label_width = MAX(max_label_width, strlen(label_total));
    max_label_width = MAX(max_label_width, strlen(label_freed));
    max_label_width = MAX(max_label_width, strlen(label_current));
    max_label_width = MAX(max_label_width, strlen(label_peak));
    max_label_width = MAX(max_label_width, strlen(label_malloc));
    max_label_width = MAX(max_label_width, strlen(label_calloc));
    max_label_width = MAX(max_label_width, strlen(label_free));
    max_label_width = MAX(max_label_width, strlen(label_diff));

#define PRINT_MEM_LINE(label, value_str)                                                                               \
  do {                                                                                                                 \
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "%s", colored_string(LOG_COLOR_GREY, label)));                      \
    for (size_t i = strlen(label); i < max_label_width; i++) {                                                         \
      SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, " "));                                                            \
    }                                                                                                                  \
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, " %s\n", value_str));                                               \
  } while (0)

    PRINT_MEM_LINE(label_total, pretty_total);
    PRINT_MEM_LINE(label_freed, pretty_freed);
    PRINT_MEM_LINE(label_current, pretty_current);
    PRINT_MEM_LINE(label_peak, pretty_peak);

    // malloc calls
    char malloc_str[32];
    safe_snprintf(malloc_str, sizeof(malloc_str), "%zu", malloc_calls);
    PRINT_MEM_LINE(label_malloc, malloc_str);

    // calloc calls
    char calloc_str[32];
    safe_snprintf(calloc_str, sizeof(calloc_str), "%zu", calloc_calls);
    PRINT_MEM_LINE(label_calloc, calloc_str);

    // free calls
    char free_str[32];
    safe_snprintf(free_str, sizeof(free_str), "%zu", free_calls);
    PRINT_MEM_LINE(label_free, free_str);

    // diff - count actual unfreed allocations in the linked list
    size_t unfreed_count = 0;
    if (g_mem.head) {
      if (ensure_mutex_initialized()) {
        mutex_lock(&g_mem.mutex);
        mem_block_t *curr = g_mem.head;
        while (curr) {
          unfreed_count++;
          curr = curr->next;
        }
        mutex_unlock(&g_mem.mutex);
      }
    }
    char diff_str[32];
    safe_snprintf(diff_str, sizeof(diff_str), "%zu", unfreed_count);
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "%s", colored_string(LOG_COLOR_GREY, label_diff)));
    for (size_t i = strlen(label_diff); i < max_label_width; i++) {
      SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, " "));
    }
    SAFE_IGNORE_PRINTF_RESULT(
        safe_fprintf(stderr, " %s\n", colored_string(unfreed_count == 0 ? LOG_COLOR_INFO : LOG_COLOR_WARN, diff_str)));

#undef PRINT_MEM_LINE

    if (g_mem.head) {
      if (ensure_mutex_initialized()) {
        mutex_lock(&g_mem.mutex);

        SAFE_IGNORE_PRINTF_RESULT(
            safe_fprintf(stderr, "\n%s\n", colored_string(LOG_COLOR_DEV, "Current allocations:")));

        // Check if we should print backtraces
        const char *print_backtrace = SAFE_GETENV("ASCII_CHAT_MEMORY_REPORT_BACKTRACE");
        int backtrace_count = 0;
        int backtrace_limit = (print_backtrace != NULL) ? 5 : 0; // Only print first 5 backtraces to save time

        mem_block_t *curr = g_mem.head;
        while (curr) {
          char pretty_size[64];
          format_bytes_pretty(curr->size, pretty_size, sizeof(pretty_size));
          const char *file_location = strip_project_path(curr->file);

          // Determine color based on unit (1 MB and over=red, KB=yellow, B=light blue)
          log_color_t size_color = LOG_COLOR_DEBUG; // Default to light blue for bytes
          if (strstr(pretty_size, "MB") || strstr(pretty_size, "GB") || strstr(pretty_size, "TB") ||
              strstr(pretty_size, "PB") || strstr(pretty_size, "EB")) {
            size_color = LOG_COLOR_ERROR; // Red for 1 MB and over (MB, GB, TB, PB, EB)
          } else if (strstr(pretty_size, "KB")) {
            size_color = LOG_COLOR_WARN; // Yellow for kilobytes
          } else if (strstr(pretty_size, " B")) {
            size_color = LOG_COLOR_DEBUG; // Light blue for bytes
          }

          char line_str[32];
          safe_snprintf(line_str, sizeof(line_str), "%d", curr->line);
          SAFE_IGNORE_PRINTF_RESULT(
              safe_fprintf(stderr, "  - %s:%s - %s\n", colored_string(LOG_COLOR_GREY, file_location),
                           colored_string(LOG_COLOR_FATAL, line_str), colored_string(size_color, pretty_size)));

          // Print backtrace if environment variable is set and we haven't hit the limit
          if (backtrace_count < backtrace_limit && curr->backtrace_count > 0) {
            backtrace_count++;
            // CRITICAL: Unlock mutex before calling platform_backtrace_symbols to avoid deadlock
            // when backtrace symbol resolution allocates memory
            mutex_unlock(&g_mem.mutex);

            // Print backtrace with symbol names
            char **symbols = platform_backtrace_symbols(curr->backtrace_ptrs, curr->backtrace_count);
            if (symbols) {
              SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "    Backtrace (%d frames):\n", curr->backtrace_count));
              for (int i = 0; i < curr->backtrace_count; i++) {
                SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "      [%d] %s\n", i, symbols[i]));
              }
              platform_backtrace_symbols_free(symbols);
            }

            // Re-acquire mutex for next iteration
            mutex_lock(&g_mem.mutex);
          }

          curr = curr->next;
        }

        mutex_unlock(&g_mem.mutex);
      } else {
        SAFE_IGNORE_PRINTF_RESULT(
            safe_fprintf(stderr, "\n%s\n",
                         colored_string(LOG_COLOR_ERROR,
                                        "Current allocations unavailable: failed to initialize debug memory mutex")));
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
