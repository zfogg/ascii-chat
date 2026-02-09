/**
 * @file buffer_pool.c
 * @ingroup buffer_pool
 * @brief 💾 Lock-free memory pool with atomic operations
 */

#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/magic.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================
 */

/** @brief Get node header from user data pointer */
static inline buffer_node_t *node_from_data(void *data) {
  return (buffer_node_t *)((char *)data - sizeof(buffer_node_t));
}

/** @brief Get user data pointer from node header */
static inline void *data_from_node(buffer_node_t *node) {
  return (void *)((char *)node + sizeof(buffer_node_t));
}

/** @brief Atomically update peak if new value is higher */
static inline void update_peak(atomic_size_t *peak, size_t value) {
  size_t old = atomic_load_explicit(peak, memory_order_relaxed);
  while (value > old) {
    if (atomic_compare_exchange_weak_explicit(peak, &old, value, memory_order_relaxed, memory_order_relaxed)) {
      break;
    }
  }
}

/* ============================================================================
 * Buffer Pool Implementation
 * ============================================================================
 */

buffer_pool_t *buffer_pool_create(size_t max_bytes, uint64_t shrink_delay_ns) {
  buffer_pool_t *pool = SAFE_MALLOC(sizeof(buffer_pool_t), buffer_pool_t *);
  if (!pool) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer pool");
    return NULL;
  }

  if (mutex_init(&pool->shrink_mutex) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize shrink mutex");
    SAFE_FREE(pool);
    return NULL;
  }

  atomic_init(&pool->free_list, NULL);
  pool->max_bytes = max_bytes > 0 ? max_bytes : BUFFER_POOL_MAX_BYTES;
  pool->shrink_delay_ns = shrink_delay_ns > 0 ? shrink_delay_ns : BUFFER_POOL_SHRINK_DELAY_NS;

  atomic_init(&pool->current_bytes, 0);
  atomic_init(&pool->used_bytes, 0);
  atomic_init(&pool->peak_bytes, 0);
  atomic_init(&pool->peak_pool_bytes, 0);
  atomic_init(&pool->hits, 0);
  atomic_init(&pool->allocs, 0);
  atomic_init(&pool->returns, 0);
  atomic_init(&pool->shrink_freed, 0);
  atomic_init(&pool->malloc_fallbacks, 0);

  char pretty_max[64];
  format_bytes_pretty(pool->max_bytes, pretty_max, sizeof(pretty_max));
  log_dev("Created buffer pool (max: %s, shrink: %llu ns, lock-free)", pretty_max,
          (unsigned long long)pool->shrink_delay_ns);

  return pool;
}

void buffer_pool_destroy(buffer_pool_t *pool) {
  if (!pool)
    return;

  // Drain the free list
  buffer_node_t *node = atomic_load(&pool->free_list);
  while (node) {
    buffer_node_t *next = atomic_load(&node->next);
    SAFE_FREE(node); // Node and data are one allocation
    node = next;
  }

  mutex_destroy(&pool->shrink_mutex);
  SAFE_FREE(pool);
}

void *buffer_pool_alloc(buffer_pool_t *pool, size_t size) {
  // Use global pool if none specified
  if (!pool) {
    pool = buffer_pool_get_global();
  }

  // Size out of range - use malloc with node header for consistent cleanup
  if (!pool || size < BUFFER_POOL_MIN_SIZE || size > BUFFER_POOL_MAX_SINGLE_SIZE) {
    size_t total_size = sizeof(buffer_node_t) + size;
    buffer_node_t *node = SAFE_MALLOC(total_size, buffer_node_t *);
    node->magic = MAGIC_BUFFER_POOL_FALLBACK; // Different magic for fallbacks
    node->_pad = 0;
    node->size = size;
    atomic_init(&node->next, NULL);
    atomic_init(&node->returned_at_ns, 0);
    node->pool = NULL; // No pool for fallbacks

    if (pool) {
      atomic_fetch_add_explicit(&pool->malloc_fallbacks, 1, memory_order_relaxed);
    }
    return data_from_node(node);
  }

  // Try to pop from lock-free stack (LIFO)
  buffer_node_t *node = atomic_load_explicit(&pool->free_list, memory_order_acquire);
  while (node) {
    buffer_node_t *next = atomic_load_explicit(&node->next, memory_order_relaxed);
    if (atomic_compare_exchange_weak_explicit(&pool->free_list, &node, next, memory_order_release,
                                              memory_order_acquire)) {
      // Successfully popped - check if it's big enough
      if (node->size >= size) {
        // Reuse this buffer
        atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
        size_t node_size = node->size;
        atomic_fetch_add_explicit(&pool->used_bytes, node_size, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->hits, 1, memory_order_relaxed);
        update_peak(&pool->peak_bytes, atomic_load(&pool->used_bytes));
        return data_from_node(node);
      } else {
        // Too small - push it back and allocate new
        // (This is rare with LIFO - usually we get similar sizes)
        buffer_node_t *head = atomic_load_explicit(&pool->free_list, memory_order_relaxed);
        do {
          atomic_store_explicit(&node->next, head, memory_order_relaxed);
        } while (!atomic_compare_exchange_weak_explicit(&pool->free_list, &head, node, memory_order_release,
                                                        memory_order_relaxed));
        break; // Fall through to allocate new
      }
    }
    // CAS failed - reload and retry
    node = atomic_load_explicit(&pool->free_list, memory_order_acquire);
  }

  // Check if we can allocate more
  size_t total_size = sizeof(buffer_node_t) + size;
  size_t current = atomic_load_explicit(&pool->current_bytes, memory_order_relaxed);

  // Atomically try to reserve space
  while (current + total_size <= pool->max_bytes) {
    if (atomic_compare_exchange_weak_explicit(&pool->current_bytes, &current, current + total_size,
                                              memory_order_relaxed, memory_order_relaxed)) {
      // Reserved space - now allocate
      // Allocate node + data in one chunk for cache efficiency
      node = SAFE_MALLOC_ALIGNED(total_size, 64, buffer_node_t *);
      if (!node) {
        // Undo reservation
        atomic_fetch_sub_explicit(&pool->current_bytes, total_size, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->malloc_fallbacks, 1, memory_order_relaxed);
        return SAFE_MALLOC(size, void *);
      }

      node->magic = MAGIC_BUFFER_POOL_VALID;
      node->_pad = 0;
      node->size = size;
      atomic_init(&node->next, NULL);
      atomic_init(&node->returned_at_ns, 0);
      node->pool = pool;

      atomic_fetch_add_explicit(&pool->used_bytes, size, memory_order_relaxed);
      atomic_fetch_add_explicit(&pool->allocs, 1, memory_order_relaxed);
      update_peak(&pool->peak_bytes, atomic_load(&pool->used_bytes));
      update_peak(&pool->peak_pool_bytes, atomic_load(&pool->current_bytes));

      return data_from_node(node);
    }
    // CAS failed - someone else allocated, reload and check again
  }

  // Pool at capacity - fall back to malloc with node header for consistent cleanup
  total_size = sizeof(buffer_node_t) + size;
  node = SAFE_MALLOC(total_size, buffer_node_t *);
  if (!node) {
    return NULL;
  }
  node->magic = MAGIC_BUFFER_POOL_FALLBACK;
  node->_pad = 0;
  node->size = size;
  atomic_init(&node->next, NULL);
  atomic_init(&node->returned_at_ns, 0);
  node->pool = pool;

  atomic_fetch_add_explicit(&pool->malloc_fallbacks, 1, memory_order_relaxed);
  return data_from_node(node);
}

void buffer_pool_free(buffer_pool_t *pool, void *data, size_t size) {
  (void)size; // Size parameter not needed with header-based detection

  if (!data)
    return;

  // All buffer_pool allocations have headers, safe to check magic
  buffer_node_t *node = node_from_data(data);

  // If it's a malloc fallback (has fallback magic), free the node directly
  if (IS_MAGIC_VALID(node->magic, MAGIC_BUFFER_POOL_FALLBACK)) {
    SAFE_FREE(node); // Free the node (includes header + data)
    return;
  }

  // If it's not a pooled buffer (no valid magic), it's external - use platform free
  if (!IS_MAGIC_VALID(node->magic, MAGIC_BUFFER_POOL_VALID)) {
    free(data); // Unknown allocation, just free the data pointer
    return;
  }

  // It's a pooled buffer - return to pool
  // Use the pool stored in the node if none provided
  if (!pool) {
    pool = node->pool;
  }

  if (!pool) {
    // Shouldn't happen, but safety
    log_error("Pooled buffer has no pool reference!");
    return;
  }

  // Update stats
  atomic_fetch_sub_explicit(&pool->used_bytes, node->size, memory_order_relaxed);
  atomic_fetch_add_explicit(&pool->returns, 1, memory_order_relaxed);

  // Set return timestamp
  atomic_store_explicit(&node->returned_at_ns, time_get_ns(), memory_order_relaxed);

  // Push to lock-free stack
  buffer_node_t *head = atomic_load_explicit(&pool->free_list, memory_order_relaxed);
  do {
    atomic_store_explicit(&node->next, head, memory_order_relaxed);
  } while (!atomic_compare_exchange_weak_explicit(&pool->free_list, &head, node, memory_order_release,
                                                  memory_order_relaxed));

  // Periodically trigger shrink (every 100 returns)
  uint64_t returns = atomic_load_explicit(&pool->returns, memory_order_relaxed);
  if (returns % 100 == 0) {
    buffer_pool_shrink(pool);
  }
}

void buffer_pool_shrink(buffer_pool_t *pool) {
  if (!pool || pool->shrink_delay_ns == 0)
    return;

  // Only one thread can shrink at a time
  if (mutex_trylock(&pool->shrink_mutex) != 0) {
    return; // Another thread is shrinking
  }

  uint64_t now = time_get_ns();
  uint64_t cutoff = (now > pool->shrink_delay_ns) ? (now - pool->shrink_delay_ns) : 0;

  // Atomically swap out the entire free list
  buffer_node_t *list = atomic_exchange_explicit(&pool->free_list, NULL, memory_order_acquire);

  // Partition into keep and free lists
  buffer_node_t *keep_list = NULL;
  buffer_node_t *free_list = NULL;

  while (list) {
    buffer_node_t *next = atomic_load_explicit(&list->next, memory_order_relaxed);
    uint64_t returned_at = atomic_load_explicit(&list->returned_at_ns, memory_order_relaxed);

    if (returned_at < cutoff) {
      // Old buffer - add to free list
      atomic_store_explicit(&list->next, free_list, memory_order_relaxed);
      free_list = list;
    } else {
      // Recent buffer - keep it
      atomic_store_explicit(&list->next, keep_list, memory_order_relaxed);
      keep_list = list;
    }
    list = next;
  }

  // Push kept buffers back to free list
  if (keep_list) {
    // Find tail
    buffer_node_t *tail = keep_list;
    while (atomic_load(&tail->next)) {
      tail = atomic_load(&tail->next);
    }

    // Atomically prepend to current free list
    buffer_node_t *head = atomic_load_explicit(&pool->free_list, memory_order_relaxed);
    do {
      atomic_store_explicit(&tail->next, head, memory_order_relaxed);
    } while (!atomic_compare_exchange_weak_explicit(&pool->free_list, &head, keep_list, memory_order_release,
                                                    memory_order_relaxed));
  }

  // Free old buffers
  while (free_list) {
    buffer_node_t *next = atomic_load_explicit(&free_list->next, memory_order_relaxed);
    size_t total_size = sizeof(buffer_node_t) + free_list->size;
    atomic_fetch_sub_explicit(&pool->current_bytes, total_size, memory_order_relaxed);
    atomic_fetch_add_explicit(&pool->shrink_freed, 1, memory_order_relaxed);
    SAFE_FREE(free_list);
    free_list = next;
  }

  mutex_unlock(&pool->shrink_mutex);
}

void buffer_pool_get_stats(buffer_pool_t *pool, size_t *current_bytes, size_t *used_bytes, size_t *free_bytes) {
  if (!pool) {
    if (current_bytes)
      *current_bytes = 0;
    if (used_bytes)
      *used_bytes = 0;
    if (free_bytes)
      *free_bytes = 0;
    return;
  }

  size_t current = atomic_load_explicit(&pool->current_bytes, memory_order_relaxed);
  size_t used = atomic_load_explicit(&pool->used_bytes, memory_order_relaxed);

  if (current_bytes)
    *current_bytes = current;
  if (used_bytes)
    *used_bytes = used;
  if (free_bytes)
    *free_bytes = (current > used) ? (current - used) : 0;
}

void buffer_pool_log_stats(buffer_pool_t *pool, const char *name) {
  if (!pool)
    return;

  size_t current = atomic_load(&pool->current_bytes);
  size_t used = atomic_load(&pool->used_bytes);
  size_t peak = atomic_load(&pool->peak_bytes);
  size_t peak_pool = atomic_load(&pool->peak_pool_bytes);
  uint64_t hits = atomic_load(&pool->hits);
  uint64_t allocs = atomic_load(&pool->allocs);
  uint64_t returns = atomic_load(&pool->returns);
  uint64_t shrink_freed = atomic_load(&pool->shrink_freed);
  uint64_t fallbacks = atomic_load(&pool->malloc_fallbacks);

  char pretty_current[64], pretty_used[64], pretty_free[64];
  char pretty_peak[64], pretty_peak_pool[64], pretty_max[64];

  format_bytes_pretty(current, pretty_current, sizeof(pretty_current));
  format_bytes_pretty(used, pretty_used, sizeof(pretty_used));
  format_bytes_pretty(current > used ? current - used : 0, pretty_free, sizeof(pretty_free));
  format_bytes_pretty(peak, pretty_peak, sizeof(pretty_peak));
  format_bytes_pretty(peak_pool, pretty_peak_pool, sizeof(pretty_peak_pool));
  format_bytes_pretty(pool->max_bytes, pretty_max, sizeof(pretty_max));

  uint64_t total_requests = hits + allocs + fallbacks;
  double hit_rate = total_requests > 0 ? (double)hits * 100.0 / (double)total_requests : 0;

  log_debug("=== Buffer Pool: %s ===", name ? name : "unnamed");
  log_debug("  Current: %s / %s capacity (peak used: %s)", pretty_current, pretty_max, pretty_peak_pool);
  log_debug("  Buffers: %s used, %s free (peak: %s)", pretty_used, pretty_free, pretty_peak);
  log_debug("  Hits: %llu (%.1f%%), Allocs: %llu, Fallbacks: %llu", (unsigned long long)hits, hit_rate,
            (unsigned long long)allocs, (unsigned long long)fallbacks);
  log_debug("  Returns: %llu, Shrink freed: %llu", (unsigned long long)returns, (unsigned long long)shrink_freed);
}

/* ============================================================================
 * Global Buffer Pool
 * ============================================================================
 */

static buffer_pool_t *g_global_pool = NULL;
static static_mutex_t g_global_pool_mutex = STATIC_MUTEX_INIT;

void buffer_pool_init_global(void) {
  static_mutex_lock(&g_global_pool_mutex);
  if (!g_global_pool) {
    g_global_pool = buffer_pool_create(0, 0);
    if (g_global_pool) {
      log_dev("Initialized global buffer pool");
    }
  }
  static_mutex_unlock(&g_global_pool_mutex);
}

void buffer_pool_cleanup_global(void) {
  static_mutex_lock(&g_global_pool_mutex);
  if (g_global_pool) {
    buffer_pool_log_stats(g_global_pool, "Global (final)");
    buffer_pool_destroy(g_global_pool);
    g_global_pool = NULL;
  }
  static_mutex_unlock(&g_global_pool_mutex);
}

buffer_pool_t *buffer_pool_get_global(void) {
  return g_global_pool;
}
