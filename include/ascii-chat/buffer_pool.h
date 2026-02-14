#pragma once

/**
 * @file buffer_pool.h
 * @brief 🗃️ Lock-Free Unified Memory Buffer Pool with Lazy Allocation
 * @ingroup buffer_pool
 * @addtogroup buffer_pool
 * @{
 *
 * A mostly lock-free memory pool using atomic operations for the fast path.
 * Allocations and frees use CAS on a lock-free stack. Only shrinking needs a lock.
 *
 * DESIGN:
 * =======
 * - Lock-free stack for free list (LIFO allocation)
 * - Node header embedded before data pointer (no search on free)
 * - Atomic counters for all statistics
 * - Only shrinking requires a lock (infrequent)
 *
 * MEMORY LIMITS:
 * ==============
 * Default max: 337 MB (supports 32 clients at 144fps)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// C11 stdatomic.h conflicts with MSVC's C++ <atomic> header on Windows.
// Define ATOMIC_TYPE macro for cross-platform atomic support in struct definitions.
#if defined(__cplusplus) && defined(_WIN32)
#include <atomic>
using std::atomic_size_t;
// MSVC C++ mode: use std::atomic<T> instead of _Atomic(T)
#define ATOMIC_TYPE(T) std::atomic<T>
#else
#include <stdatomic.h>
// C mode: use standard _Atomic(T)
#define ATOMIC_TYPE(T) _Atomic(T)
#endif
#include "platform/mutex.h"
#include "util/magic.h"

/* ============================================================================
 * Configuration Constants
 * ============================================================================
 */

/** @brief Maximum total bytes the pool can hold (337 MB) */
#define BUFFER_POOL_MAX_BYTES (337 * 1024 * 1024)

/** @brief Time before unused buffers are freed (5 seconds in nanoseconds) */
#define BUFFER_POOL_SHRINK_DELAY_NS 5000000000ULL

/** @brief Minimum buffer size to pool (smaller allocations use malloc directly) */
#define BUFFER_POOL_MIN_SIZE 64

/** @brief Maximum single buffer size to pool (larger allocations use malloc directly) */
#define BUFFER_POOL_MAX_SINGLE_SIZE (4 * 1024 * 1024)

/** @brief Magic value to identify pooled buffers - Alias for backward compatibility */
#define BUFFER_POOL_MAGIC MAGIC_BUFFER_POOL_VALID
/** @brief Magic value for malloc fallback buffers (not in pool) - Alias for backward compatibility */
#define BUFFER_POOL_MAGIC_FALLBACK MAGIC_BUFFER_POOL_FALLBACK

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief Node header embedded before user data
 *
 * Layout in memory:
 *   [buffer_node_t header][user data...]
 *                         ^-- pointer returned to caller
 *
 * @ingroup buffer_pool
 */
typedef struct buffer_node {
  uint32_t magic;                     ///< Magic to identify pooled buffers
  uint32_t _pad;                      ///< Padding for alignment
  size_t size;                        ///< Size of user data portion
  ATOMIC_TYPE(struct buffer_node *) next; ///< Next in free list (atomic for lock-free)
  ATOMIC_TYPE(uint64_t) returned_at_ns;   ///< Timestamp when returned to free list (nanoseconds)
  struct buffer_pool *pool;           ///< Owning pool (for free)
} buffer_node_t;

/**
 * @brief Unified buffer pool with lock-free fast path
 *
 * @ingroup buffer_pool
 */
typedef struct buffer_pool {
  ATOMIC_TYPE(buffer_node_t *) free_list; ///< Lock-free stack of available buffers
  mutex_t shrink_mutex;               ///< Only used for shrinking

  size_t max_bytes;         ///< Maximum total bytes allowed
  uint64_t shrink_delay_ns; ///< Time before unused buffers freed (nanoseconds)

  /** @name Atomic counters @{ */
  ATOMIC_TYPE(size_t) current_bytes;   ///< Total bytes in pool
  ATOMIC_TYPE(size_t) used_bytes;      ///< Bytes currently in use
  ATOMIC_TYPE(size_t) peak_bytes;      ///< Peak bytes in use
  ATOMIC_TYPE(size_t) peak_pool_bytes; ///< Peak bytes in pool

  ATOMIC_TYPE(uint64_t) hits;             ///< Allocations from free list
  ATOMIC_TYPE(uint64_t) allocs;           ///< New buffer allocations
  ATOMIC_TYPE(uint64_t) returns;          ///< Buffers returned to free list
  ATOMIC_TYPE(uint64_t) shrink_freed;     ///< Buffers freed by shrink
  ATOMIC_TYPE(uint64_t) malloc_fallbacks; ///< Allocations that bypassed pool
  /** @} */
} buffer_pool_t;

/* ============================================================================
 * Buffer Pool API
 * ============================================================================
 */

/**
 * @brief Create a new buffer pool
 * @param max_bytes Maximum bytes the pool can hold (0 = use default)
 * @param shrink_delay_ns Time before unused buffers freed in nanoseconds (0 = use default)
 * @return New buffer pool, or NULL on failure
 */
buffer_pool_t *buffer_pool_create(size_t max_bytes, uint64_t shrink_delay_ns);

/**
 * @brief Destroy a buffer pool and free all memory
 * @param pool Pool to destroy
 */
void buffer_pool_destroy(buffer_pool_t *pool);

/**
 * @brief Allocate a buffer from the pool (lock-free fast path)
 * @param pool Buffer pool (NULL = use global pool)
 * @param size Size of buffer to allocate
 * @return Allocated buffer, or NULL on failure
 */
void *buffer_pool_alloc(buffer_pool_t *pool, size_t size);

/**
 * @brief Free a buffer back to the pool (lock-free)
 * @param pool Buffer pool (NULL = auto-detect from buffer)
 * @param data Buffer to free
 * @param size Size of buffer (used for fallback, can be 0 if pooled)
 */
void buffer_pool_free(buffer_pool_t *pool, void *data, size_t size);

/**
 * @brief Force shrink the pool (free old unused buffers)
 * @param pool Buffer pool
 * @note This is the only operation that takes a lock
 */
void buffer_pool_shrink(buffer_pool_t *pool);

/**
 * @brief Get pool statistics (atomic reads)
 */
void buffer_pool_get_stats(buffer_pool_t *pool, size_t *current_bytes, size_t *used_bytes, size_t *free_bytes);

/**
 * @brief Log pool statistics
 */
void buffer_pool_log_stats(buffer_pool_t *pool, const char *name);

/* ============================================================================
 * Global Buffer Pool
 * ============================================================================
 */

void buffer_pool_init_global(void);
void buffer_pool_cleanup_global(void);
buffer_pool_t *buffer_pool_get_global(void);

/* ============================================================================
 * Convenience Macros
 * ============================================================================
 */

#define POOL_ALLOC(size) buffer_pool_alloc(NULL, (size))
#define POOL_FREE(data, size) buffer_pool_free(NULL, (data), (size))

/** @} */
