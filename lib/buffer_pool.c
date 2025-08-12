#include "buffer_pool.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>
#include <inttypes.h> // For PRIxPTR

/* ============================================================================
 * Internal Buffer Pool Functions
 * ============================================================================
 */

static buffer_pool_t *buffer_pool_create_single(size_t buffer_size, size_t pool_size) {
  if (buffer_size == 0 || pool_size == 0) {
    return NULL;
  }

  buffer_pool_t *pool;
  SAFE_MALLOC(pool, sizeof(buffer_pool_t), buffer_pool_t *);

  // Allocate array of buffer nodes
  SAFE_MALLOC(pool->nodes, sizeof(buffer_node_t) * pool_size, buffer_node_t *);

  // Allocate single memory block for all buffers
  SAFE_MALLOC(pool->memory_block, buffer_size * pool_size, void *);

  // Initialize each buffer node
  uint8_t *current_buffer = (uint8_t *)pool->memory_block;
  for (size_t i = 0; i < pool_size; i++) {
    pool->nodes[i].data = current_buffer;
    pool->nodes[i].size = buffer_size;
    pool->nodes[i].in_use = false;
    pool->nodes[i].next = (i < pool_size - 1) ? &pool->nodes[i + 1] : NULL;
    current_buffer += buffer_size;
  }

  pool->free_list = &pool->nodes[0];
  pool->buffer_size = buffer_size;
  pool->pool_size = pool_size;
  pool->used_count = 0;
  pool->hits = 0;
  pool->misses = 0;
  pool->returns = 0;

  return pool;
}

static void buffer_pool_destroy_single(buffer_pool_t *pool) {
  if (!pool) {
    return;
  }

  SAFE_FREE(pool->memory_block);
  SAFE_FREE(pool->nodes);
  SAFE_FREE(pool);
}

static void *buffer_pool_alloc_single(buffer_pool_t *pool, size_t size) {
  if (!pool || size > pool->buffer_size) {
    return NULL;
  }

  buffer_node_t *node = pool->free_list;
  if (node) {
    pool->free_list = node->next;
    node->next = NULL;
    node->in_use = true;
    pool->used_count++;
    pool->hits++;
    pool->total_bytes_allocated += size;
    if (pool->used_count > pool->peak_used) {
      pool->peak_used = pool->used_count;
    }
    return node->data;
  }

  pool->misses++;
  return NULL;
}

static bool buffer_pool_free_single(buffer_pool_t *pool, void *data) {
  if (!pool || !data) {
    return false;
  }

  // Check if this buffer belongs to our pool
  uint8_t *pool_start = (uint8_t *)pool->memory_block;
  uint8_t *pool_end = pool_start + (pool->buffer_size * pool->pool_size);
  uint8_t *buffer = (uint8_t *)data;

  if (buffer < pool_start || buffer >= pool_end) {
    return false; // Not from this pool
  }

  // Find the corresponding node
  size_t index = (buffer - pool_start) / pool->buffer_size;
  if (index >= pool->pool_size) {
    return false;
  }

  buffer_node_t *node = &pool->nodes[index];
  if (!node->in_use) {
    log_error("Double free detected in buffer pool!");
    return false;
  }

  // Return to free list
  node->in_use = false;
  node->next = pool->free_list;
  pool->free_list = node;
  pool->used_count--;
  pool->returns++;

  return true;
}

/* ============================================================================
 * Public Buffer Pool API
 * ============================================================================
 */

data_buffer_pool_t *data_buffer_pool_create(void) {
  data_buffer_pool_t *pool;
  SAFE_MALLOC(pool, sizeof(data_buffer_pool_t), data_buffer_pool_t *);

  // Create pools for different size classes
  pool->small_pool = buffer_pool_create_single(BUFFER_POOL_SMALL_SIZE, BUFFER_POOL_SMALL_COUNT);
  pool->medium_pool = buffer_pool_create_single(BUFFER_POOL_MEDIUM_SIZE, BUFFER_POOL_MEDIUM_COUNT);
  pool->large_pool = buffer_pool_create_single(BUFFER_POOL_LARGE_SIZE, BUFFER_POOL_LARGE_COUNT);
  pool->xlarge_pool = buffer_pool_create_single(BUFFER_POOL_XLARGE_SIZE, BUFFER_POOL_XLARGE_COUNT);

  pthread_mutex_init(&pool->pool_mutex, NULL);

  pool->total_allocs = 0;
  pool->pool_hits = 0;
  pool->malloc_fallbacks = 0;

  log_info("Created data buffer pool: %zu KB small, %zu KB medium, %zu KB large, %zu KB xlarge",
           (BUFFER_POOL_SMALL_SIZE * BUFFER_POOL_SMALL_COUNT) / 1024,
           (BUFFER_POOL_MEDIUM_SIZE * BUFFER_POOL_MEDIUM_COUNT) / 1024,
           (BUFFER_POOL_LARGE_SIZE * BUFFER_POOL_LARGE_COUNT) / 1024,
           (BUFFER_POOL_XLARGE_SIZE * BUFFER_POOL_XLARGE_COUNT) / 1024);

  return pool;
}

void data_buffer_pool_destroy(data_buffer_pool_t *pool) {
  if (!pool) {
    return;
  }

  buffer_pool_destroy_single(pool->small_pool);
  buffer_pool_destroy_single(pool->medium_pool);
  buffer_pool_destroy_single(pool->large_pool);
  buffer_pool_destroy_single(pool->xlarge_pool);

  pthread_mutex_destroy(&pool->pool_mutex);
  SAFE_FREE(pool);
}

void *data_buffer_pool_alloc(data_buffer_pool_t *pool, size_t size) {
  if (!pool) {
    // No pool, fallback to malloc
    log_warn("MALLOC FALLBACK (no pool): size=%zu at %s:%d", size, __FILE__, __LINE__);
    void *data;
    SAFE_MALLOC(data, size, void *);
    return data;
  }

  pthread_mutex_lock(&pool->pool_mutex);
  pool->total_allocs++;

  void *buffer = NULL;

  // Try to allocate from appropriate pool based on size
  if (size <= BUFFER_POOL_SMALL_SIZE) {
    buffer = buffer_pool_alloc_single(pool->small_pool, size);
    if (!buffer)
      log_warn("SMALL POOL EXHAUSTED for size=%zu", size);
  } else if (size <= BUFFER_POOL_MEDIUM_SIZE) {
    buffer = buffer_pool_alloc_single(pool->medium_pool, size);
    if (!buffer)
      log_warn("MEDIUM POOL EXHAUSTED for size=%zu", size);
  } else if (size <= BUFFER_POOL_LARGE_SIZE) {
    buffer = buffer_pool_alloc_single(pool->large_pool, size);
    if (!buffer)
      log_warn("LARGE POOL EXHAUSTED for size=%zu", size);
  } else if (size <= BUFFER_POOL_XLARGE_SIZE) {
    buffer = buffer_pool_alloc_single(pool->xlarge_pool, size);
    if (!buffer)
      log_warn("XLARGE POOL EXHAUSTED for size=%zu", size);
  } else {
    log_warn("ALLOCATION TOO LARGE: size=%zu exceeds max pool size", size);
  }

  if (buffer) {
    pool->pool_hits++;
  } else {
    // Fallback to malloc for sizes not in pool or pool exhausted
    pool->malloc_fallbacks++;
  }

  pthread_mutex_unlock(&pool->pool_mutex);

  // If no buffer from pool, use malloc
  if (!buffer) {
    void *callstack[3];
    int frames = backtrace(callstack, 3);
    char **symbols = backtrace_symbols(callstack, frames);

    fprintf(stderr, "MALLOC FALLBACK ALLOC: size=%zu at %s:%d thread=%p\n", size, __FILE__, __LINE__,
            (void *)pthread_self());
    if (symbols && frames >= 2) {
      fprintf(stderr, "  Called from: %s\n", symbols[1]);
      if (frames >= 3)
        fprintf(stderr, "  Called from: %s\n", symbols[2]);
    }
    free(symbols);

    SAFE_MALLOC(buffer, size, void *);
    fprintf(stderr, "MALLOC FALLBACK ALLOC COMPLETE: size=%zu -> ptr=%p thread=%p\n", size, buffer,
            (void *)pthread_self());
  }

  return buffer;
}

void data_buffer_pool_free(data_buffer_pool_t *pool, void *data, size_t size) {
  if (!data) {
    return;
  }

  if (!pool) {
    // No pool, must have been malloc'd directly
    log_warn("MALLOC FALLBACK FREE (no pool): size=%zu at %s:%d", size, __FILE__, __LINE__);
    SAFE_FREE(data);
    return;
  }

  pthread_mutex_lock(&pool->pool_mutex);

  bool freed = false;

  // Try to return to appropriate pool
  if (size <= BUFFER_POOL_SMALL_SIZE) {
    freed = buffer_pool_free_single(pool->small_pool, data);
  } else if (size <= BUFFER_POOL_MEDIUM_SIZE) {
    freed = buffer_pool_free_single(pool->medium_pool, data);
  } else if (size <= BUFFER_POOL_LARGE_SIZE) {
    freed = buffer_pool_free_single(pool->large_pool, data);
  } else if (size <= BUFFER_POOL_XLARGE_SIZE) {
    freed = buffer_pool_free_single(pool->xlarge_pool, data);
  }

  pthread_mutex_unlock(&pool->pool_mutex);

  // If not from any pool, it was malloc'd
  if (!freed) {
    // Save the pointer address as an integer to avoid static analyzer warnings about use-after-free
    uintptr_t original_addr = (uintptr_t)data;
    fprintf(stderr, "MALLOC FALLBACK FREE: size=%zu ptr=%p at %s:%d thread=%p\n", size, data, __FILE__,
            __LINE__, (void *)pthread_self());
    SAFE_FREE(data);
    fprintf(stderr, "MALLOC FALLBACK FREE COMPLETE: size=%zu ptr=0x%" PRIxPTR " thread=%p\n", size, original_addr,
            (void *)pthread_self());
  }
}

void data_buffer_pool_get_stats(data_buffer_pool_t *pool, uint64_t *hits, uint64_t *misses) {
  if (!pool) {
    if (hits)
      *hits = 0;
    if (misses)
      *misses = 0;
    return;
  }

  pthread_mutex_lock(&pool->pool_mutex);
  if (hits)
    *hits = pool->pool_hits;
  if (misses)
    *misses = pool->malloc_fallbacks;
  pthread_mutex_unlock(&pool->pool_mutex);
}

/* ============================================================================
 * Global Shared Buffer Pool
 * ============================================================================
 */

static data_buffer_pool_t *g_global_buffer_pool = NULL;
static pthread_mutex_t g_global_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

void data_buffer_pool_init_global(void) {
  pthread_mutex_lock(&g_global_pool_mutex);
  if (!g_global_buffer_pool) {
    g_global_buffer_pool = data_buffer_pool_create();
    if (g_global_buffer_pool) {
      log_info("Initialized global shared buffer pool");
    }
  }
  pthread_mutex_unlock(&g_global_pool_mutex);
}

void data_buffer_pool_cleanup_global(void) {
  pthread_mutex_lock(&g_global_pool_mutex);
  if (g_global_buffer_pool) {
    // Log final statistics
    uint64_t hits, misses;
    data_buffer_pool_get_stats(g_global_buffer_pool, &hits, &misses);
    if (hits + misses > 0) {
      log_info("Global buffer pool final stats: %llu hits (%.1f%%), %llu misses", (unsigned long long)hits,
               (double)hits * 100.0 / (hits + misses), (unsigned long long)misses);
    }

    data_buffer_pool_destroy(g_global_buffer_pool);
    g_global_buffer_pool = NULL;
    log_info("Cleaned up global shared buffer pool");
  } else {
    log_debug("Global buffer pool already cleaned up - skipping");
  }
  pthread_mutex_unlock(&g_global_pool_mutex);
}

data_buffer_pool_t *data_buffer_pool_get_global(void) {
  return g_global_buffer_pool;
}

// Convenience functions that use the global pool
void *buffer_pool_alloc(size_t size) {
  if (!g_global_buffer_pool) {
    // If global pool not initialized, fall back to regular malloc
    log_warn("MALLOC FALLBACK (global pool not init): size=%zu at %s:%d", size, __FILE__, __LINE__);
    void *data;
    SAFE_MALLOC(data, size, void *);
    return data;
  }
  return data_buffer_pool_alloc(g_global_buffer_pool, size);
}

void buffer_pool_free(void *data, size_t size) {
  if (!data) {
    return;
  }

  // If we have a global pool, try to free through it
  if (g_global_buffer_pool) {
    data_buffer_pool_free(g_global_buffer_pool, data, size);
    return;
  }

  // No global pool - this memory must have been malloc'd
  // (unless it's from a pool that was already destroyed, in which case we leak)
  fprintf(stderr, "MALLOC FALLBACK FREE (global pool destroyed): size=%zu ptr=%p at %s:%d\n", size, data, __FILE__,
          __LINE__);
  SAFE_FREE(data);
  fprintf(stderr, "MALLOC FALLBACK FREE (global pool destroyed) COMPLETE: size=%zu ptr=%p\n", size, data);
}

/* ============================================================================
 * Enhanced Statistics Functions
 * ============================================================================ */

void data_buffer_pool_get_detailed_stats(data_buffer_pool_t *pool, buffer_pool_detailed_stats_t *stats) {
  if (!pool || !stats) {
    return;
  }

  memset(stats, 0, sizeof(*stats));

  pthread_mutex_lock(&pool->pool_mutex);

  // Small pool stats
  if (pool->small_pool) {
    stats->small_hits = pool->small_pool->hits;
    stats->small_misses = pool->small_pool->misses;
    stats->small_returns = pool->small_pool->returns;
    stats->small_peak_used = pool->small_pool->peak_used;
    stats->small_bytes = pool->small_pool->total_bytes_allocated;
  }

  // Medium pool stats
  if (pool->medium_pool) {
    stats->medium_hits = pool->medium_pool->hits;
    stats->medium_misses = pool->medium_pool->misses;
    stats->medium_returns = pool->medium_pool->returns;
    stats->medium_peak_used = pool->medium_pool->peak_used;
    stats->medium_bytes = pool->medium_pool->total_bytes_allocated;
  }

  // Large pool stats
  if (pool->large_pool) {
    stats->large_hits = pool->large_pool->hits;
    stats->large_misses = pool->large_pool->misses;
    stats->large_returns = pool->large_pool->returns;
    stats->large_peak_used = pool->large_pool->peak_used;
    stats->large_bytes = pool->large_pool->total_bytes_allocated;
  }

  // Extra large pool stats
  if (pool->xlarge_pool) {
    stats->xlarge_hits = pool->xlarge_pool->hits;
    stats->xlarge_misses = pool->xlarge_pool->misses;
    stats->xlarge_returns = pool->xlarge_pool->returns;
    stats->xlarge_peak_used = pool->xlarge_pool->peak_used;
    stats->xlarge_bytes = pool->xlarge_pool->total_bytes_allocated;
  }

  // Calculate totals
  stats->total_allocations = stats->small_hits + stats->medium_hits + stats->large_hits + stats->xlarge_hits +
                             stats->small_misses + stats->medium_misses + stats->large_misses + stats->xlarge_misses;
  stats->total_bytes = stats->small_bytes + stats->medium_bytes + stats->large_bytes + stats->xlarge_bytes;

  // Calculate pool usage efficiency
  uint64_t total_hits = stats->small_hits + stats->medium_hits + stats->large_hits + stats->xlarge_hits;
  if (stats->total_allocations > 0) {
    stats->total_pool_usage_percent = (total_hits * 100) / stats->total_allocations;
  }

  pthread_mutex_unlock(&pool->pool_mutex);
}

void data_buffer_pool_log_stats(data_buffer_pool_t *pool, const char *pool_name) {
  if (!pool) {
    return;
  }

  buffer_pool_detailed_stats_t stats;
  data_buffer_pool_get_detailed_stats(pool, &stats);

  log_info("=== Buffer Pool Stats: %s ===", pool_name ? pool_name : "Unknown");
  log_info("Total allocations: %llu, Pool hit rate: %llu%%, Total bytes: %.2f MB",
           (unsigned long long)stats.total_allocations, (unsigned long long)stats.total_pool_usage_percent,
           (double)stats.total_bytes / (1024.0 * 1024.0));

  if (stats.small_hits + stats.small_misses > 0) {
    log_info("  Small (1KB): %llu hits, %llu misses (%.1f%%), peak: %llu/%d, %.2f MB",
             (unsigned long long)stats.small_hits, (unsigned long long)stats.small_misses,
             (double)stats.small_hits * 100.0 / (stats.small_hits + stats.small_misses),
             (unsigned long long)stats.small_peak_used, BUFFER_POOL_SMALL_COUNT,
             (double)stats.small_bytes / (1024.0 * 1024.0));
  }

  if (stats.medium_hits + stats.medium_misses > 0) {
    log_info("  Medium (64KB): %llu hits, %llu misses (%.1f%%), peak: %llu/%d, %.2f MB",
             (unsigned long long)stats.medium_hits, (unsigned long long)stats.medium_misses,
             (double)stats.medium_hits * 100.0 / (stats.medium_hits + stats.medium_misses),
             (unsigned long long)stats.medium_peak_used, BUFFER_POOL_MEDIUM_COUNT,
             (double)stats.medium_bytes / (1024.0 * 1024.0));
  }

  if (stats.large_hits + stats.large_misses > 0) {
    log_info("  Large (256KB): %llu hits, %llu misses (%.1f%%), peak: %llu/%d, %.2f MB",
             (unsigned long long)stats.large_hits, (unsigned long long)stats.large_misses,
             (double)stats.large_hits * 100.0 / (stats.large_hits + stats.large_misses),
             (unsigned long long)stats.large_peak_used, BUFFER_POOL_LARGE_COUNT,
             (double)stats.large_bytes / (1024.0 * 1024.0));
  }

  if (stats.xlarge_hits + stats.xlarge_misses > 0) {
    log_info("  XLarge (1.25MB): %llu hits, %llu misses (%.1f%%), peak: %llu/%d, %.2f MB",
             (unsigned long long)stats.xlarge_hits, (unsigned long long)stats.xlarge_misses,
             (double)stats.xlarge_hits * 100.0 / (stats.xlarge_hits + stats.xlarge_misses),
             (unsigned long long)stats.xlarge_peak_used, BUFFER_POOL_XLARGE_COUNT,
             (double)stats.xlarge_bytes / (1024.0 * 1024.0));
  }
}

void buffer_pool_log_global_stats(void) {
  if (g_global_buffer_pool) {
    data_buffer_pool_log_stats(g_global_buffer_pool, "Global");
  } else {
    log_info("Global buffer pool not initialized");
  }
}