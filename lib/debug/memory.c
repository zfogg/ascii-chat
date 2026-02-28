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
#include <errno.h>

#include <ascii-chat/debug/memory.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/sync.h>
#include <ascii-chat/log/named.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/common/error_codes.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/uthash.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/ansi.h>

typedef struct mem_block {
  void *ptr;
  size_t size;
  char file[BUFFER_SIZE_SMALL];
  int line;
  uint64_t tid;          // Allocating thread ID (for site lookup)
  char thread_name[256]; // Thread name from named registry
  bool is_aligned;
  struct mem_block *next;
} mem_block_t;

// Allocation site cache - tracks per-site backtraces
typedef struct alloc_site {
  char key[BUFFER_SIZE_SMALL + 32]; // "file:line:tid" - uthash key
  size_t live_count;                // Live (unfreed) allocations from this site
  size_t live_bytes;                // Live bytes from this site
  size_t total_count;               // Total ever allocated from this site
  backtrace_t backtrace;            // Captured+symbolized once on first alloc from site
  char thread_name[256];            // Thread name for this site (cached at first alloc)
  UT_hash_handle hh;
} alloc_site_t;

#define MEM_SITE_CACHE_MAX_KEYS 1024
#define MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY 256

// Non-static for shared library compatibility (still thread-local)
__thread bool g_in_debug_memory = false;

// Allocation site cache (protected by g_mem.mutex)
static alloc_site_t *g_site_cache = NULL;
static size_t g_site_count = 0;

/**
 * @brief Allocation ignore list for memory report
 *
 * These file:line:count entries represent expected system allocations that should
 * not be reported as leaks. The count field specifies EXACTLY how many allocations
 * from that location are expected. If more allocations exist, they WILL be reported
 * as leaks, allowing us to detect actual memory leaks even in ignored locations.
 *
 * Example: {"lib/util/pcre2.c", 52, 2} means exactly 2 PCRE2 singleton allocations
 * are expected. If 3 exist, the 3rd one will be reported as a leak.
 */
/**
 * @brief Debug memory suppression entry for both configuration and runtime tracking
 *
 * Static fields (file, line, expected_count, expected_bytes, reason) define
 * what we expect. Runtime fields (key, count, bytes) track what we've seen.
 */
typedef struct {
  // Configuration (static, in g_suppression_config[])
  const char *file;
  int line;
  int expected_count;    // Expected allocations per thread
  size_t expected_bytes; // Expected total bytes per thread
  const char *reason;    // Why these allocations are expected/harmless

  // Runtime counter (dynamic, in g_suppression_counters[])
  // Key format: "file:line:tid"
  char key[BUFFER_SIZE_SMALL + 32];
  int count;
  size_t bytes;
} debug_memory_suppression_t;

// Static configuration of expected suppressions
static const debug_memory_suppression_t g_suppression_config[] = {
    {"lib/options/colorscheme.c", 585, 8, 47, "8 16-color ANSI code strings"},
    {"lib/options/colorscheme.c", 602, 8, 88, "8 256-color ANSI code strings"},
    {"lib/options/colorscheme.c", 619, 8, 144, "8 truecolor ANSI code strings"},
    {"lib/util/path.c", 1211, 1, 43, "normalized path allocation (caller frees)"},
    {"lib/platform/posix/util.c", 35, 18, 1280, "platform_strdup() string allocations (mirror mode)"},
    {"lib/util/pcre2.c", 54, 1, 56, "PCRE2 JIT singleton for code compilation (intentional, cleaned up at shutdown)"},
    {"lib/util/pcre2.c", 62, 1, 7, "PCRE2 mcontext singleton for matching (intentional, cleaned up at shutdown)"},
    // Debug system allocations (intentional, tied to thread lifetimes)
    {"lib/debug/mutex.c", 327, 500, 15000, "Debug mutex stack tracking allocations (freed on thread/program exit)"},
    {"lib/thread_pool.c", 114, 10, 3840, "Thread pool metadata allocations"},
    {"lib/thread_pool.c", 160, 40, 416, "Thread pool work queue entries"},
    {"lib/platform/posix/thread.c", 87, 100, 1600, "Thread creation tracking allocations"},
    {NULL, 0, 0, 0, NULL} // Sentinel
};

// Runtime counters tracking per-thread allocations (cleared at report boundaries)
#define MAX_SUPPRESSION_COUNTERS 64
static debug_memory_suppression_t g_suppression_counters[MAX_SUPPRESSION_COUNTERS];
static size_t g_suppression_counter_count = 0;

/**
 * @brief Reset suppression counters at report boundaries
 */
static void reset_suppression_counters(void) {
  memset(g_suppression_counters, 0, sizeof(g_suppression_counters));
  g_suppression_counter_count = 0;
}

/**
 * @brief Check if an allocation should be ignored in the memory report
 *
 * Tracks how many allocations from each ignore location have been seen per thread.
 * Only ignores up to the expected_count for each location/thread combination.
 */
// Helper function to acquire mutex with polling instead of blocking.
// mutex_lock() can be unsafe during shutdown when signals may arrive.
// Use trylock polling instead to avoid hanging indefinitely.
static bool acquire_mutex_with_polling(mutex_t *mutex, int timeout_ms) {
  int max_retries = (timeout_ms + 9) / 10; // 10ms per retry
  for (int retry = 0; retry < max_retries; retry++) {
    int lock_result = mutex_trylock(mutex);
    if (lock_result == 0) {
      return true;
    }
    if (lock_result != EBUSY) {
      return false; // Unexpected error
    }
    platform_sleep_ms(10); // Sleep 10ms before retry
  }
  return false; // Timeout
}

// Helper: Capture thread name from named registry
static void capture_thread_name(uint64_t tid, char *thread_name_buf, size_t buf_size) {
  // Check if this is the main thread (saved during initialization)
  uint64_t main_tid = debug_sync_get_main_thread_id();
  if (main_tid != 0 && tid == main_tid) {
    safe_snprintf(thread_name_buf, buf_size, "thread/main (0x%lx)", tid);
    return;
  }

  // Try to look up thread name from registry using the tid directly
  const char *name = named_get((uintptr_t)tid);
  if (name) {
    // Found a registered name, format it with the type
    safe_snprintf(thread_name_buf, buf_size, "thread/%s (0x%lx)", name, tid);
  } else {
    // Not registered - just show hex tid
    safe_snprintf(thread_name_buf, buf_size, "0x%lx", tid);
  }
}

static bool should_ignore_allocation(const char *file, int line, uint64_t tid, size_t size) {
  // file is already the normalized relative path from curr->file
  // (set during debug_malloc/debug_calloc using extract_project_relative_path)
  // Do NOT call extract_project_relative_path again - it won't work on already-relative paths

  for (size_t i = 0; g_suppression_config[i].file != NULL; i++) {
    if (line == g_suppression_config[i].line && strcmp(file, g_suppression_config[i].file) == 0) {
      // Found matching suppression entry - check if we've exceeded expected count for this thread
      char key[BUFFER_SIZE_SMALL + 32];
      safe_snprintf(key, sizeof(key), "%s:%d:%lu", file, line, tid);

      // Look up existing counter for this site/thread combination
      debug_memory_suppression_t *counter = NULL;
      for (size_t j = 0; j < g_suppression_counter_count; j++) {
        if (strcmp(g_suppression_counters[j].key, key) == 0) {
          counter = &g_suppression_counters[j];
          break;
        }
      }

      // Create new counter if needed
      if (!counter) {
        if (g_suppression_counter_count < MAX_SUPPRESSION_COUNTERS) {
          counter = &g_suppression_counters[g_suppression_counter_count];
          SAFE_STRNCPY(counter->key, key, sizeof(counter->key) - 1);
          counter->count = 0;
          counter->bytes = 0;
          g_suppression_counter_count++;
        } else {
          // Counter array full - report as leak to avoid silent failures
          return false;
        }
      }

      // Check if we've exceeded expected count for this thread
      if (counter->count < g_suppression_config[i].expected_count) {
        counter->count++;
        counter->bytes += size;
        return true; // Ignore this allocation
      }
      // Exceeded expected count - report as leak!
      return false;
    }
  }
  return false;
}

/**
 * Get or create an allocation site cache entry (called while holding g_mem.mutex)
 */
static alloc_site_t *get_or_create_site(const char *file, int line) {
  uint64_t tid = asciichat_thread_current_id();
  const char *relative_file = extract_project_relative_path(file);

  char key[BUFFER_SIZE_SMALL + 32];
  safe_snprintf(key, sizeof(key), "%s:%d:%lu", relative_file, line, tid);

  // Try to find existing site
  alloc_site_t *site = NULL;
  HASH_FIND_STR(g_site_cache, key, site);
  if (site) {
    return site;
  }

  // Check if we've exceeded cache capacity
  if (g_site_count >= MEM_SITE_CACHE_MAX_KEYS) {
    log_warn_every(LOG_RATE_FAST, "Allocation site cache full (%zu keys), not tracking %s:%d (tid: 0x%lx)",
                   MEM_SITE_CACHE_MAX_KEYS, relative_file, line, tid);
    return NULL;
  }

  // Create new site entry
  site = (alloc_site_t *)malloc(sizeof(alloc_site_t));
  if (!site) {
    return NULL;
  }

  memset(site, 0, sizeof(alloc_site_t));
  SAFE_STRNCPY(site->key, key, sizeof(site->key) - 1);

  // Capture backtrace addresses only (fast)
  // Symbolization is deferred to exit time (memory report) to avoid 50ms+ per site during startup
  backtrace_capture(&site->backtrace);

  // Capture thread name at site creation time
  capture_thread_name(tid, site->thread_name, sizeof(site->thread_name));

  // Add to hash table
  HASH_ADD_STR(g_site_cache, key, site);
  g_site_count++;

  return site;
}

/**
 * Look up an allocation site by file, line, and thread ID (called while holding g_mem.mutex)
 */
static alloc_site_t *lookup_site(const char *file, int line, uint64_t tid) {
  const char *relative_file = extract_project_relative_path(file);
  char key[BUFFER_SIZE_SMALL + 32];
  safe_snprintf(key, sizeof(key), "%s:%d:%lu", relative_file, line, tid);

  alloc_site_t *site = NULL;
  HASH_FIND_STR(g_site_cache, key, site);
  return site;
}

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
  lifecycle_t lifecycle;
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
           .lifecycle = LIFECYCLE_INIT,
           .quiet_mode = false};

#undef malloc
#undef free
#undef calloc
#undef realloc

static bool ensure_mutex_initialized(void) {
  if (lifecycle_is_initialized(&g_mem.lifecycle)) {
    return true;
  }

  if (!lifecycle_init(&g_mem.lifecycle, "debug_memory")) {
    log_error("Failed to initialize debug memory mutex; memory tracking will run without locking");
    return false;
  }

  return true;
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
      block->tid = asciichat_thread_current_id();
      capture_thread_name(block->tid, block->thread_name, sizeof(block->thread_name));
      block->next = g_mem.head;
      g_mem.head = block;

      // Update allocation site cache
      alloc_site_t *site = get_or_create_site(normalized_file, line);
      if (site) {
        site->live_count++;
        site->live_bytes += size;
        site->total_count++;
        if (site->live_count == MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY) {
          // Only warn if this allocation site is not suppressed (expected/benign allocations)
          if (!should_ignore_allocation(normalized_file, line, asciichat_thread_current_id(), size)) {
            log_warn("%s:%d:%lu — %d live allocations, possible memory accumulation", normalized_file, line,
                     asciichat_thread_current_id(), MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY);
          }
        }
      }
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
      block->tid = asciichat_thread_current_id();
      capture_thread_name(block->tid, block->thread_name, sizeof(block->thread_name));
      block->next = g_mem.head;
      g_mem.head = block;

      // Update allocation site cache
      alloc_site_t *site = get_or_create_site(normalized_file, line);
      if (site) {
        site->live_count++;
        site->live_bytes += size;
        site->total_count++;
        if (site->live_count == MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY) {
          // Only warn if this allocation site is not suppressed (expected/benign allocations)
          if (!should_ignore_allocation(normalized_file, line, asciichat_thread_current_id(), size)) {
            log_warn("%s:%d:%lu — %d live allocations, possible memory accumulation", normalized_file, line,
                     asciichat_thread_current_id(), MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY);
          }
        }
      }
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

        // Update allocation site cache counters
        alloc_site_t *site = lookup_site(curr->file, curr->line, curr->tid);
        if (site && site->live_count > 0) {
          site->live_count--;
          if (site->live_bytes >= freed_size) {
            site->live_bytes -= freed_size;
          } else {
            site->live_bytes = 0; // Prevent underflow
          }
        }

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
      const char *normalized_file = extract_project_relative_path(file);
      SAFE_STRNCPY(block->file, normalized_file, sizeof(block->file) - 1);
      block->line = line;
      block->tid = asciichat_thread_current_id();
      capture_thread_name(block->tid, block->thread_name, sizeof(block->thread_name));
      block->next = g_mem.head;
      g_mem.head = block;

      // Update allocation site cache
      alloc_site_t *site = get_or_create_site(normalized_file, line);
      if (site) {
        site->live_count++;
        site->live_bytes += total;
        site->total_count++;
        if (site->live_count == MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY) {
          // Only warn if this allocation site is not suppressed (expected/benign allocations)
          if (!should_ignore_allocation(normalized_file, line, asciichat_thread_current_id(), size)) {
            log_warn("%s:%d:%lu — %d live allocations, possible memory accumulation", normalized_file, line,
                     asciichat_thread_current_id(), MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY);
          }
        }
      }
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
      // Update site cache: decrement old site, increment new site
      alloc_site_t *old_site = lookup_site(curr->file, curr->line, curr->tid);
      if (old_site && old_site->live_count > 0) {
        old_site->live_count--;
        old_site->live_bytes -= curr->size;
      }

      // Update block with new metadata
      curr->ptr = new_ptr;
      const char *normalized_file = extract_project_relative_path(file);
      SAFE_STRNCPY(curr->file, normalized_file, sizeof(curr->file) - 1);
      curr->file[sizeof(curr->file) - 1] = '\0';
      curr->line = line;
      curr->tid = asciichat_thread_current_id();
      curr->size = size;
      curr->is_aligned = false;

      // Add to new site
      alloc_site_t *new_site = get_or_create_site(normalized_file, line);
      if (new_site) {
        new_site->live_count++;
        new_site->live_bytes += size;
        new_site->total_count++;
        if (new_site->live_count == MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY) {
          log_warn("%s:%d:%lu — %d live allocations, possible memory accumulation", normalized_file, line,
                   asciichat_thread_current_id(), MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY);
        }
      }
    } else {
      // Block not found - create new tracking entry
      mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
      if (block) {
        block->ptr = new_ptr;
        block->size = size;
        block->is_aligned = false;
        const char *normalized_file = extract_project_relative_path(file);
        SAFE_STRNCPY(block->file, normalized_file, sizeof(block->file) - 1);
        block->line = line;
        block->tid = asciichat_thread_current_id();
        block->next = g_mem.head;
        g_mem.head = block;

        // Add to site cache
        alloc_site_t *site = get_or_create_site(normalized_file, line);
        if (site) {
          site->live_count++;
          site->live_bytes += size;
          site->total_count++;
          if (site->live_count == MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY) {
            log_warn("%s:%d:%lu — %d live allocations, possible memory accumulation", normalized_file, line,
                     asciichat_thread_current_id(), MEM_SITE_CACHE_MAX_ALLOCS_PER_KEY);
          }
        }
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

void debug_memory_report(void) {
  // Check for usage error BEFORE cleanup clears it
  asciichat_error_t error = GET_ERRNO();

  // Always clean up errno, even if we're not printing
  asciichat_errno_destroy();

  // Skip memory report if an action flag was passed (for clean action output)
  // unless explicitly forced via ASCII_CHAT_MEMORY_DEBUG environment variable
  if (has_action_flag() && !SAFE_GETENV("ASCII_CHAT_MEMORY_DEBUG")) {
    return;
  }

  // Skip memory report on command-line usage errors for clean error output
  if (error == ERROR_USAGE) {
    return;
  }

  bool quiet = g_mem.quiet_mode;

  // Reset suppression counters at start of report
  reset_suppression_counters();

  if (!quiet) {
// Build report in buffer, then output once to stderr and log file
#define REPORT_BUFFER_SIZE (256 * 1024) // 256KB for full memory report
    char *report_buffer = malloc(REPORT_BUFFER_SIZE);
    if (!report_buffer) {
      SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "Failed to allocate memory for report buffer\n"));
      return;
    }
    size_t report_len = 0;

#define APPEND_REPORT(fmt, ...)                                                                                        \
  do {                                                                                                                 \
    size_t remaining = REPORT_BUFFER_SIZE - report_len;                                                                \
    if (remaining > 1) {                                                                                               \
      int written = safe_snprintf(report_buffer + report_len, remaining, fmt, ##__VA_ARGS__);                          \
      if (written > 0)                                                                                                 \
        report_len += written;                                                                                         \
    }                                                                                                                  \
  } while (0)

    APPEND_REPORT("=== Memory Report ===\n");

    size_t total_allocated = atomic_load(&g_mem.total_allocated);
    size_t total_freed = atomic_load(&g_mem.total_freed);
    size_t current_usage = atomic_load(&g_mem.current_usage);
    size_t peak_usage = atomic_load(&g_mem.peak_usage);
    size_t malloc_calls = atomic_load(&g_mem.malloc_calls);
    size_t calloc_calls = atomic_load(&g_mem.calloc_calls);
    size_t free_calls = atomic_load(&g_mem.free_calls);

    // Calculate total size and count of suppressed allocations
    size_t suppressed_bytes = 0;
    size_t suppressed_count = 0;
    if (g_mem.head) {
      if (ensure_mutex_initialized()) {
        // Use polling instead of blocking mutex_lock to avoid hangs during shutdown.
        if (!acquire_mutex_with_polling(&g_mem.mutex, 100)) {
          goto skip_memory_iter;
        }

        // Now we have the lock - iterate memory blocks
        mem_block_t *curr = g_mem.head;
        while (curr) {
          if (should_ignore_allocation(curr->file, curr->line, curr->tid, curr->size)) {
            suppressed_bytes += curr->size;
            suppressed_count++;
          }
          curr = curr->next;
        }
        mutex_unlock(&g_mem.mutex);
      }
    }

  skip_memory_iter:

    // Adjust current usage to exclude suppressed allocations
    size_t adjusted_current_usage = (current_usage >= suppressed_bytes) ? (current_usage - suppressed_bytes) : 0;

    // Adjust total allocated for display to exclude suppressions (so it matches total freed)
    size_t adjusted_total_allocated =
        (total_allocated >= suppressed_bytes) ? (total_allocated - suppressed_bytes) : total_allocated;

    // Reset suppression counters after counting suppressed bytes
    reset_suppression_counters();

    // Count actual unfreed allocations early so we can use it to filter suppression warnings
    size_t unfreed_count = 0;
    if (g_mem.head) {
      if (ensure_mutex_initialized()) {
        if (acquire_mutex_with_polling(&g_mem.mutex, 100)) {
          mem_block_t *curr = g_mem.head;
          while (curr) {
            if (!should_ignore_allocation(curr->file, curr->line, curr->tid, curr->size)) {
              unfreed_count++;
            }
            curr = curr->next;
          }
          mutex_unlock(&g_mem.mutex);
        }
      }
    }

    // Only warn about suppression mismatches if there are outstanding allocations
    if (unfreed_count > 0) {
      for (size_t i = 0; g_suppression_config[i].file != NULL; i++) {
        // Sum counts and bytes from all threads for this file:line combination
        int total_count = 0;
        size_t total_bytes = 0;
        for (size_t j = 0; j < g_suppression_counter_count; j++) {
          // Parse counter key "file:line:tid" to extract file and line
          char *last_colon = strrchr(g_suppression_counters[j].key, ':');
          if (last_colon) {
            char *second_colon = last_colon - 1;
            while (second_colon > g_suppression_counters[j].key && *second_colon != ':') {
              second_colon--;
            }
            if (second_colon > g_suppression_counters[j].key) {
              int line_from_key = 0;
              sscanf(second_colon + 1, "%d", &line_from_key);
              size_t file_len = second_colon - g_suppression_counters[j].key;
              if (file_len == strlen(g_suppression_config[i].file) && line_from_key == g_suppression_config[i].line &&
                  strncmp(g_suppression_counters[j].key, g_suppression_config[i].file, file_len) == 0) {
                total_count += g_suppression_counters[j].count;
                total_bytes += g_suppression_counters[j].bytes;
              }
            }
          }
        }

        if (total_count != g_suppression_config[i].expected_count ||
            total_bytes != g_suppression_config[i].expected_bytes) {
          APPEND_REPORT("%s\n", colored_string(LOG_COLOR_ERROR, "WARNING: Suppression mismatch detected"));
          APPEND_REPORT("  %s:%d\n", g_suppression_config[i].file, g_suppression_config[i].line);
          if (total_count != g_suppression_config[i].expected_count) {
            APPEND_REPORT("    Count mismatch: expected %d, found %d\n", g_suppression_config[i].expected_count,
                          total_count);
          }
          if (total_bytes != g_suppression_config[i].expected_bytes) {
            APPEND_REPORT("    Bytes mismatch: expected %zu, found %zu\n", g_suppression_config[i].expected_bytes,
                          total_bytes);
          }
        }
      }
    }

    char pretty_total[64];
    char pretty_freed[64];
    char pretty_current[64];
    char pretty_peak[64];
    format_bytes_pretty(adjusted_total_allocated, pretty_total, sizeof(pretty_total));
    format_bytes_pretty(total_freed, pretty_freed, sizeof(pretty_freed));
    format_bytes_pretty(adjusted_current_usage, pretty_current, sizeof(pretty_current));
    format_bytes_pretty(peak_usage, pretty_peak, sizeof(pretty_peak));

    // Calculate max label width for column alignment
    const char *label_total = "Total allocated:";
    const char *label_freed = "Total freed:";
    const char *label_current = "Current usage:";
    const char *label_peak = "Peak usage:";
    const char *label_malloc = "malloc calls:";
    const char *label_calloc = "calloc calls:";
    const char *label_free = "free calls:";
    const char *label_suppressions = "suppressions:";
    const char *label_diff = "unfreed allocations:";

    size_t max_label_width = 0;
    max_label_width = MAX(max_label_width, strlen(label_total));
    max_label_width = MAX(max_label_width, strlen(label_freed));
    max_label_width = MAX(max_label_width, strlen(label_current));
    max_label_width = MAX(max_label_width, strlen(label_peak));
    max_label_width = MAX(max_label_width, strlen(label_malloc));
    max_label_width = MAX(max_label_width, strlen(label_calloc));
    max_label_width = MAX(max_label_width, strlen(label_free));
    max_label_width = MAX(max_label_width, strlen(label_suppressions));
    max_label_width = MAX(max_label_width, strlen(label_diff));

#define APPEND_MEM_LINE(label, value_str)                                                                              \
  do {                                                                                                                 \
    APPEND_REPORT("%s", colored_string(LOG_COLOR_GREY, label));                                                        \
    for (size_t i = strlen(label); i < max_label_width; i++) {                                                         \
      APPEND_REPORT(" ");                                                                                              \
    }                                                                                                                  \
    APPEND_REPORT(" %s\n", value_str);                                                                                 \
  } while (0)

#define APPEND_MEM_LINE_COLORED(label, value_str, color)                                                               \
  do {                                                                                                                 \
    APPEND_REPORT("%s", colored_string(LOG_COLOR_GREY, label));                                                        \
    for (size_t i = strlen(label); i < max_label_width; i++) {                                                         \
      APPEND_REPORT(" ");                                                                                              \
    }                                                                                                                  \
    APPEND_REPORT(" %s\n", colored_string(color, value_str));                                                          \
  } while (0)

    // Colorize total allocated/freed: green if they match, red if they don't (memory leak)
    // We've already adjusted allocated to exclude suppressions, so now compare directly
    log_color_t alloc_freed_color = (adjusted_total_allocated == total_freed) ? LOG_COLOR_INFO : LOG_COLOR_ERROR;
    APPEND_MEM_LINE_COLORED(label_total, pretty_total, alloc_freed_color);
    APPEND_MEM_LINE_COLORED(label_freed, pretty_freed, alloc_freed_color);

    // Colorize current usage: green if 0 (no leaks), red if any unfreed memory (leak detected)
    log_color_t current_color = (adjusted_current_usage == 0) ? LOG_COLOR_INFO : LOG_COLOR_ERROR;
    APPEND_MEM_LINE_COLORED(label_current, pretty_current, current_color);

    // Colorize peak usage: green if 0-50 MB, yellow if 50-80 MB, red if above 80 MB
    log_color_t peak_color = LOG_COLOR_INFO; // Default green
    if (peak_usage >= (80 * 1024 * 1024)) {
      peak_color = LOG_COLOR_ERROR; // Red if >= 80 MB
    } else if (peak_usage >= (50 * 1024 * 1024)) {
      peak_color = LOG_COLOR_WARN; // Yellow if >= 50 MB
    }
    APPEND_MEM_LINE_COLORED(label_peak, pretty_peak, peak_color);

    // unfreed_count already calculated earlier for suppression warning filtering

    // Colorize malloc/calloc/free calls: green if unfreed == 0, red if unfreed != 0
    log_color_t calls_color = (unfreed_count == 0) ? LOG_COLOR_INFO : LOG_COLOR_ERROR;
    char malloc_str[32];
    safe_snprintf(malloc_str, sizeof(malloc_str), "%zu", malloc_calls);
    APPEND_MEM_LINE_COLORED(label_malloc, malloc_str, calls_color);

    char calloc_str[32];
    safe_snprintf(calloc_str, sizeof(calloc_str), "%zu", calloc_calls);
    APPEND_MEM_LINE_COLORED(label_calloc, calloc_str, calls_color);

    char free_str[32];
    safe_snprintf(free_str, sizeof(free_str), "%zu", free_calls);
    APPEND_MEM_LINE_COLORED(label_free, free_str, calls_color);

    // Colorize suppressions: green if working properly, red if >= 1MB or >= 100 allocations
    if (suppressed_count > 0) {
      char pretty_suppressed[64];
      format_bytes_pretty(suppressed_bytes, pretty_suppressed, sizeof(pretty_suppressed));
      char suppressions_str[128];
      safe_snprintf(suppressions_str, sizeof(suppressions_str), "%zu (%s)", suppressed_count, pretty_suppressed);

      log_color_t suppressions_color = LOG_COLOR_INFO; // Green by default (working properly)
      if (suppressed_bytes >= (1024 * 1024) || suppressed_count >= 100) {
        suppressions_color = LOG_COLOR_ERROR; // Red if >= 1MB or >= 100 allocations
      }
      APPEND_MEM_LINE_COLORED(label_suppressions, suppressions_str, suppressions_color);
    }

    char diff_str[32];
    safe_snprintf(diff_str, sizeof(diff_str), "%zu", unfreed_count);
    APPEND_REPORT("%s", colored_string(LOG_COLOR_GREY, label_diff));
    for (size_t i = strlen(label_diff); i < max_label_width; i++) {
      APPEND_REPORT(" ");
    }
    APPEND_REPORT(" %s\n", colored_string(unfreed_count == 0 ? LOG_COLOR_INFO : LOG_COLOR_ERROR, diff_str));

#undef APPEND_MEM_LINE
#undef APPEND_MEM_LINE_COLORED

    // Only show "Current allocations:" section if there are actual leaks
    if (unfreed_count > 0) {
      // Reset counters before printing pass (after counting pass used them)
      reset_suppression_counters();

      // Print "Current allocations:" header (count already shown in "unfreed allocations" above)
      APPEND_REPORT("\n%s\n", colored_string(LOG_COLOR_DEV, "Current allocations:"));
    }

    if (g_site_cache && unfreed_count > 0) {
      if (ensure_mutex_initialized()) {
        if (!acquire_mutex_with_polling(&g_mem.mutex, 100)) {
          goto skip_allocations_list;
        }

        // Iterate site cache with timeout (max 500ms to avoid hanging during shutdown)
        uint64_t iteration_start_ns = time_get_ns();
        const uint64_t max_iteration_ns = 500000000; // 500ms timeout
        alloc_site_t *site, *tmp;
        HASH_ITER(hh, g_site_cache, site, tmp) {
          // Check timeout every iteration
          if (time_elapsed_ns(iteration_start_ns, time_get_ns()) > max_iteration_ns) {
            APPEND_REPORT("  (printing truncated - timeout)\n");
            break;
          }
          // Skip sites with no live allocations
          if (site->live_count == 0) {
            continue;
          }

          // Parse site key to extract file and line for ignore checking
          // Key format: "file:line:tid"  (where file is the relative path)
          char file[BUFFER_SIZE_SMALL];
          memset(file, 0, sizeof(file));
          int line = 0;
          uint64_t tid = 0;
          bool parse_success = false;

          // Strategy: Find both colons by scanning for the rightmost two ':'
          // 1. Find the LAST ':' (between line and tid)
          // 2. Find the SECOND-LAST ':' (between file and line)
          // 3. Extract each component

          // Count colons to verify we have exactly 2
          int colon_count = 0;
          for (const char *p = site->key; *p; p++) {
            if (*p == ':')
              colon_count++;
          }

          if (colon_count == 2) {
            // Find the two colons from right to left
            char *last_colon = strrchr(site->key, ':'); // Last colon
            char *first_colon = strchr(site->key, ':'); // First colon

            if (last_colon && first_colon && first_colon < last_colon) {
              // Extract components: file is from start to first_colon
              size_t file_len = first_colon - site->key;

              if (file_len > 0 && file_len < sizeof(file)) {
                strncpy(file, site->key, file_len);
                file[file_len] = '\0';

                // Extract line (between first and last colon)
                if (sscanf(first_colon + 1, "%d", &line) == 1 && sscanf(last_colon + 1, "%lu", &tid) == 1) {
                  parse_success = true;
                }
              } else if (file_len >= sizeof(file)) {
                // Filename too long, truncate with ellipsis
                size_t max_len = sizeof(file) - 4;
                strncpy(file, site->key, max_len);
                file[max_len] = '\0';
                strcat(file, "...");

                if (sscanf(first_colon + 1, "%d", &line) == 1 && sscanf(last_colon + 1, "%lu", &tid) == 1) {
                  parse_success = true;
                }
              }
            }
          }

          // If parsing failed, use a descriptive fallback
          if (!parse_success) {
            safe_snprintf(file, sizeof(file), "<key:%s>", site->key);
          }

          // Skip ignored allocations
          if (should_ignore_allocation(file, line, tid, site->live_bytes)) {
            continue;
          }

          char pretty_bytes[64];
          format_bytes_pretty(site->live_bytes, pretty_bytes, sizeof(pretty_bytes));

          char line_str[32];
          safe_snprintf(line_str, sizeof(line_str), "%d", line);
          char count_str[32];
          safe_snprintf(count_str, sizeof(count_str), "%zu", site->live_count);

          // Determine color based on byte size (1 MB and over=red, KB=yellow, B=light blue)
          log_color_t size_color = LOG_COLOR_DEBUG;
          if (strstr(pretty_bytes, "MB") || strstr(pretty_bytes, "GB") || strstr(pretty_bytes, "TB") ||
              strstr(pretty_bytes, "PB") || strstr(pretty_bytes, "EB")) {
            size_color = LOG_COLOR_ERROR;
          } else if (strstr(pretty_bytes, "KB")) {
            size_color = LOG_COLOR_WARN;
          }

          // Colorize thread name separately to avoid rotating buffer conflicts
          const char *colored_thread_name = colorize_named_string(site->thread_name);

          // Print site summary with thread name (captured at site creation, colorized)
          APPEND_REPORT("  - %s:%s [%s] %s live %s total\n", colored_string(LOG_COLOR_GREY, file),
                        colored_string(LOG_COLOR_FATAL, line_str), colored_thread_name,
                        colored_string(size_color, count_str), colored_string(size_color, pretty_bytes));

          // Don't synchronously symbolize backtraces during memory report - symbolization is slow
          // and can cause hangs during shutdown. Instead, backtraces will be symbolized asynchronously
          // by the debug sync thread. Just print whatever symbols are available.
          if (site->backtrace.symbols != NULL && site->backtrace.count > 0) {
            APPEND_REPORT("    Backtrace (%d frames):\n", site->backtrace.count);
            for (int i = 0; i < site->backtrace.count; i++) {
              APPEND_REPORT("      [%d] %s\n", i, site->backtrace.symbols[i]);
            }
          }
        }

        mutex_unlock(&g_mem.mutex);
      } else {
        APPEND_REPORT("\n%s\n",
                      colored_string(LOG_COLOR_ERROR,
                                     "Current allocations unavailable: failed to initialize debug memory mutex"));
      }
    }

  skip_allocations_list:

    // SKIP cleanup site cache at shutdown
    // We used to clean up the hash table here, but HASH_DEL inside HASH_ITER causes
    // undefined behavior (modifying hash table during iteration = infinite loop/hang).
    // Since we're exiting anyway, skip this cleanup - the OS will reclaim memory.

    // Write to stderr (colored output)
    SAFE_IGNORE_PRINTF_RESULT(safe_fprintf(stderr, "%s", report_buffer));

    // Write to log file (for SIGUSR2 debugging persistence) - strip ANSI codes
    char *stripped = ansi_strip_escapes(report_buffer, report_len);
    if (stripped) {
      log_file_msg("%s", stripped);
      free(stripped);
    }

    free(report_buffer);
#undef REPORT_BUFFER_SIZE
#undef APPEND_REPORT
  }
}

// ============================================================================
// Memory Report Debug Thread (triggered via SIGUSR2)
// ============================================================================

#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/thread.h>

typedef struct {
  volatile bool should_run;
  volatile bool should_exit;
  volatile bool signal_triggered; // Flag set by SIGUSR2 handler
  mutex_t mutex;                  // Protects access to flags
  cond_t cond;                    // Wakes thread when signal arrives
  bool initialized;               // Tracks if mutex/cond are initialized
} debug_memory_request_t;

static debug_memory_request_t g_debug_memory_request = {false, false, false, {0}, {0}, false};
static asciichat_thread_t g_debug_memory_thread;

/**
 * @brief Thread function for memory report printing
 *
 * Waits for SIGUSR2 signal or explicit trigger. When triggered,
 * prints the memory report on this thread rather than in signal context.
 */
static void *debug_memory_thread_fn(void *arg) {
  (void)arg;

  while (!g_debug_memory_request.should_exit) {
    mutex_lock(&g_debug_memory_request.mutex);

    if ((g_debug_memory_request.should_run || g_debug_memory_request.signal_triggered) &&
        !g_debug_memory_request.should_exit) {
      mutex_unlock(&g_debug_memory_request.mutex);
      debug_memory_report();
      mutex_lock(&g_debug_memory_request.mutex);
      g_debug_memory_request.should_run = false;
      g_debug_memory_request.signal_triggered = false;
    }

    // Wait for work or signal, with 100ms timeout to check should_exit
    if (!g_debug_memory_request.should_exit) {
      cond_timedwait(&g_debug_memory_request.cond, &g_debug_memory_request.mutex, 100000000); // 100ms
    }
    mutex_unlock(&g_debug_memory_request.mutex);
  }
  return NULL;
}

/**
 * @brief Initialize memory debug thread resources
 */
int debug_memory_thread_init(void) {
  return 0;
}

/**
 * @brief Start the memory debug thread
 */
int debug_memory_thread_start(void) {
  if (!g_debug_memory_request.initialized) {
    mutex_init(&g_debug_memory_request.mutex, "debug_memory_state");
    cond_init(&g_debug_memory_request.cond, "debug_memory_signal");
    g_debug_memory_request.initialized = true;
  }

  g_debug_memory_request.should_exit = false;
  int err = asciichat_thread_create(&g_debug_memory_thread, "debug_memory", debug_memory_thread_fn, NULL);
  return err;
}

/**
 * @brief Trigger memory report from SIGUSR2 handler
 */
void debug_memory_trigger_report(void) {
  g_debug_memory_request.signal_triggered = true;
  cond_signal(&g_debug_memory_request.cond);
}

/**
 * @brief Stop the memory debug thread
 */
void debug_memory_thread_cleanup(void) {
  // Only join if thread was actually created
  if (!g_debug_memory_request.initialized) {
    return;
  }

  g_debug_memory_request.initialized = false; // Prevent double-join

  // Set exit flag and signal thread - don't acquire mutex to avoid deadlock
  // The thread reads should_exit without locking, and cond_signal is safe without mutex
  g_debug_memory_request.should_exit = true;
  cond_signal(&g_debug_memory_request.cond);

  // Use timeout join to prevent indefinite hang if debug thread is stuck
  // Matches pattern used in debug_sync_cleanup_thread() which has 1 second timeout
  int join_result = asciichat_thread_join_timeout(&g_debug_memory_thread, NULL, 1000000000ULL); // 1 second timeout
  if (join_result != 0) {
    // Thread didn't exit cleanly, but we still need to proceed with shutdown
    // Log a warning if logging is still available
  }
}

#elif defined(DEBUG_MEMORY)

void debug_memory_set_quiet_mode(bool quiet) {
  (void)quiet;
}

void debug_memory_report(void) {}

int debug_memory_thread_init(void) {
  return 0;
}

int debug_memory_thread_start(void) {
  return 0;
}

void debug_memory_trigger_report(void) {}

void debug_memory_thread_cleanup(void) {}

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

/**
 * @brief Asynchronously symbolize unsymbolized backtraces from memory allocations
 * Called by debug sync thread to avoid blocking the memory report during shutdown
 */
void debug_sync_symbolize_allocations(void) {
  if (!g_site_cache) {
    return; // No allocations to symbolize
  }

  if (!ensure_mutex_initialized()) {
    return; // Can't acquire mutex
  }

  // Try to acquire mutex with short timeout (don't block the debug thread)
  if (!acquire_mutex_with_polling(&g_mem.mutex, 50)) {
    return; // Mutex busy, skip this iteration
  }

  // Iterate site cache and symbolize backtraces that haven't been tried yet
  alloc_site_t *site, *tmp;
  int symbolized_count = 0;
  const int max_per_iteration = 5; // Symbolize max 5 backtraces per iteration

  HASH_ITER(hh, g_site_cache, site, tmp) {
    if (symbolized_count >= max_per_iteration) {
      break; // Don't symbolize too many in one iteration
    }

    // Only symbolize backtraces that haven't been tried yet
    if (site->backtrace.count > 0 && !site->backtrace.tried_symbolize) {
      backtrace_symbolize(&site->backtrace);
      symbolized_count++;
    }
  }

  mutex_unlock(&g_mem.mutex);
}

#endif
