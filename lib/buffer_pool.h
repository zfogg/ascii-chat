#pragma once

/**
 * @defgroup buffer_pool Buffer Pool
 * @ingroup buffer_pool
 *
 * @file buffer_pool.h
 * @ingroup buffer_pool
 * @brief Memory Buffer Pool System for High-Performance Allocation
 *
 * This header provides a pre-allocated memory buffer pool system optimized
 * for high-throughput scenarios in ascii-chat. The system uses multiple size
 * classes to reduce malloc/free overhead and improve performance.
 *
 * CORE FEATURES:
 * ==============
 * - Multiple size classes for different allocation patterns
 * - Pre-allocated buffers eliminate runtime allocation overhead
 * - Thread-safe operations for concurrent access
 * - Automatic fallback to malloc when pools are exhausted
 * - Comprehensive statistics tracking for performance analysis
 * - Global singleton pool for convenient application-wide use
 *
 * SIZE CLASSES:
 * ==============
 * The buffer pool manages four distinct size classes:
 * - Small (1KB): Audio packets and small data structures
 * - Medium (64KB): Small video frames and intermediate buffers
 * - Large (256KB): Large video frames and frame processing buffers
 * - Extra Large (2MB): HD video frames and large allocations
 *
 * Each size class maintains its own pool with configurable capacity.
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - Single large allocation per pool for efficiency
 * - Linked list of free buffers for fast allocation
 * - Zero-allocation pattern after initial pool creation
 * - Automatic cleanup on pool destruction
 *
 * THREAD SAFETY:
 * ==============
 * All pool operations are protected by mutexes to ensure thread-safe
 * concurrent access. The global singleton pool is also thread-safe.
 *
 * PERFORMANCE BENEFITS:
 * =====================
 * - Eliminates malloc/free calls in hot paths
 * - Reduces memory fragmentation
 * - Improves cache locality with pre-allocated blocks
 * - Enables zero-allocation operation after initialization
 *
 * STATISTICS AND MONITORING:
 * ===========================
 * The system tracks comprehensive statistics including:
 * - Allocation hits (served from pool)
 * - Allocation misses (fell back to malloc)
 * - Peak usage patterns
 * - Total bytes allocated
 *
 * These statistics enable performance analysis and pool sizing optimization.
 *
 * @note The buffer pool is primarily used by packet_queue for efficient
 *       packet data buffer management, but can be used by any module that
 *       needs high-performance memory allocation.
 *
 * @note When a pool is exhausted, allocations automatically fall back to
 *       regular malloc, ensuring the system never fails due to pool limits.
 *
 * @note Pool sizes are based on memory usage analysis of the application.
 *       Adjust sizes based on observed usage patterns for optimal performance.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/mutex.h"

/* ============================================================================
 * Buffer Size Constants
 * ============================================================================
 */

/**
 * @name Buffer Size Constants
 * @ingroup buffer_pool
 * @{
 */

/** @brief Small buffer size (1KB) for audio packets */
#define BUFFER_POOL_SMALL_SIZE 1024
/** @brief Medium buffer size (64KB) for small video frames */
#define BUFFER_POOL_MEDIUM_SIZE 65536
/** @brief Large buffer size (256KB) for large video frames */
#define BUFFER_POOL_LARGE_SIZE 262144
/** @brief Extra large buffer size (2MB) for HD video frames */
#define BUFFER_POOL_XLARGE_SIZE 2097152

/** @} */

/* ============================================================================
 * Buffer Count Constants
 * ============================================================================
 */

/**
 * @name Buffer Count Constants
 * @ingroup buffer_pool
 * @{
 */

/** @brief Number of small buffers to pre-allocate (1MB total) */
#define BUFFER_POOL_SMALL_COUNT 1024
/** @brief Number of medium buffers to pre-allocate (4MB total) */
#define BUFFER_POOL_MEDIUM_COUNT 64
/** @brief Number of large buffers to pre-allocate (8MB total) */
#define BUFFER_POOL_LARGE_COUNT 32
/** @brief Number of extra large buffers to pre-allocate (128MB total) */
#define BUFFER_POOL_XLARGE_COUNT 64

/** @} */

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief Single buffer node in the pool
 *
 * Represents a single buffer in the pool. Buffers are linked together
 * in a free list for efficient allocation.
 *
 * @ingroup buffer_pool
 */
typedef struct buffer_node {
  void *data;               ///< The actual buffer memory
  size_t size;              ///< Size of this buffer
  struct buffer_node *next; ///< Next free buffer in list (for free list)
  bool in_use;              ///< Debug flag: track if buffer is in use
} buffer_node_t;

/**
 * @brief Buffer pool for a specific size class
 *
 * Manages a pool of buffers of a single size. Maintains a free list for
 * O(1) allocation and tracks statistics for performance monitoring.
 *
 * @ingroup buffer_pool
 */
typedef struct buffer_pool {
  buffer_node_t *free_list; ///< Stack of free buffers (LIFO)
  buffer_node_t *nodes;     ///< Pre-allocated array of buffer nodes
  void *memory_block;       ///< Single allocation for all buffers
  size_t buffer_size;       ///< Size of each buffer in this pool
  size_t pool_size;         ///< Number of buffers in pool
  size_t used_count;        ///< Number of buffers currently in use

  /** @name Statistics
   * @{
   */
  uint64_t hits;                  ///< Successful allocations from pool
  uint64_t misses;                ///< Allocations that fell back to malloc
  uint64_t returns;               ///< Successful returns to pool
  uint64_t peak_used;             ///< Peak number of buffers used simultaneously
  uint64_t total_bytes_allocated; ///< Total bytes served (for analysis)
  /** @} */
} buffer_pool_t;

/**
 * @brief Multi-size buffer pool manager
 *
 * Manages multiple buffer pools for different size classes. Automatically
 * dispatches allocation requests to the appropriate pool based on size.
 * Falls back to malloc if no matching pool exists or pool is exhausted.
 *
 * @ingroup buffer_pool
 */
typedef struct data_buffer_pool {
  buffer_pool_t *small_pool;  ///< Pool for small buffers (1KB, audio packets)
  buffer_pool_t *medium_pool; ///< Pool for medium buffers (64KB, small frames)
  buffer_pool_t *large_pool;  ///< Pool for large buffers (256KB, large frames)
  buffer_pool_t *xlarge_pool; ///< Pool for extra large buffers (2MB, HD frames)
  mutex_t pool_mutex;         ///< Mutex protecting all pools

  /** @name Global Statistics
   * @{
   */
  uint64_t total_allocs;     ///< Total allocation requests
  uint64_t pool_hits;        ///< Allocations satisfied from pools
  uint64_t malloc_fallbacks; ///< Allocations that used malloc fallback
  /** @} */
} data_buffer_pool_t;

/* ============================================================================
 * Buffer Pool Management
 * @{
 */

/**
 * @brief Create a new multi-size buffer pool
 * @return Pointer to newly created buffer pool, or NULL on failure
 *
 * Creates a new buffer pool manager with all size classes initialized.
 * Each size class is pre-allocated with the configured number of buffers.
 *
 * @note Allocates significant memory up front (128KB + 4MB + 8MB + 128MB).
 *       Use this carefully in memory-constrained environments.
 *
 * @note Thread-safe: Can be called from multiple threads.
 *
 * @ingroup buffer_pool
 */
data_buffer_pool_t *data_buffer_pool_create(void);

/**
 * @brief Destroy a buffer pool and free all resources
 * @param pool Buffer pool to destroy (must not be NULL)
 *
 * Frees all pre-allocated buffers and internal structures. After calling
 * this function, the pool pointer is invalid and must not be used.
 *
 * @warning All buffers allocated from this pool must be freed before
 *          destroying the pool. Attempting to free buffers after pool
 *          destruction will cause undefined behavior.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_destroy(data_buffer_pool_t *pool);

/**
 * @brief Allocate a buffer from the appropriate pool
 * @param pool Buffer pool manager
 * @param size Size of buffer to allocate
 * @return Pointer to allocated buffer, or NULL on failure
 *
 * Allocates a buffer of the specified size by dispatching to the appropriate
 * size class pool. If no matching pool exists or pool is exhausted, falls
 * back to standard malloc.
 *
 * @note Size matching: Buffers are allocated from the smallest pool that
 *       fits the requested size. For example, 2KB request uses medium pool.
 *
 * @note Fallback: If no pool matches or pool is exhausted, uses malloc.
 *       Statistics track both pool hits and malloc fallbacks.
 *
 * @note Thread-safe: Multiple threads can allocate simultaneously.
 *
 * @ingroup buffer_pool
 */
void *data_buffer_pool_alloc(data_buffer_pool_t *pool, size_t size);

/**
 * @brief Free a buffer back to its pool
 * @param pool Buffer pool manager
 * @param data Buffer pointer to free (must not be NULL)
 * @param size Size of buffer being freed
 *
 * Frees a buffer back to the appropriate pool. The size parameter is used
 * to determine which pool the buffer belongs to.
 *
 * @note Size matching: Must match the size used when allocating. Freeing with
 *       incorrect size may cause buffer corruption.
 *
 * @note Fallback handling: If buffer was allocated via malloc fallback,
 *       this function calls free() directly.
 *
 * @note Thread-safe: Multiple threads can free simultaneously.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_free(data_buffer_pool_t *pool, void *data, size_t size);

/**
 * @brief Get basic statistics from buffer pool
 * @param pool Buffer pool manager (must not be NULL)
 * @param hits_out Pointer to receive total pool hits (can be NULL)
 * @param misses_out Pointer to receive total malloc fallbacks (can be NULL)
 *
 * Retrieves basic allocation statistics from the buffer pool. Useful for
 * quick performance checks.
 *
 * @note For detailed per-pool statistics, use `data_buffer_pool_get_detailed_stats()`.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_get_stats(data_buffer_pool_t *pool, uint64_t *hits, uint64_t *misses);

/** @} */

/* ============================================================================
 * Global Buffer Pool (Singleton)
 * @{
 */

/**
 * @brief Initialize the global shared buffer pool
 *
 * Initializes the global singleton buffer pool. Must be called once at
 * application startup before using buffer pool allocation functions.
 *
 * @note Idempotent: Safe to call multiple times (no-op after first call).
 *
 * @note Thread-safe: Can be called from any thread during initialization.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_init_global(void);

/**
 * @brief Clean up the global shared buffer pool
 *
 * Destroys the global singleton buffer pool and frees all resources.
 * Should be called once at application shutdown after all buffers are freed.
 *
 * @warning All buffers allocated from the global pool must be freed before
 *          calling this function.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_cleanup_global(void);

/**
 * @brief Get pointer to the global shared buffer pool
 * @return Pointer to global buffer pool, or NULL if not initialized
 *
 * Returns a pointer to the global singleton buffer pool. Useful for passing
 * to functions that require an explicit pool parameter.
 *
 * @note The global pool must be initialized with `data_buffer_pool_init_global()`
 *       before calling this function.
 *
 * @ingroup buffer_pool
 */
data_buffer_pool_t *data_buffer_pool_get_global(void);

/** @} */

/* ============================================================================
 * Convenience Functions (Global Pool)
 * @{
 */

/**
 * @brief Allocate a buffer from the global pool
 * @param size Size of buffer to allocate
 * @return Pointer to allocated buffer, or NULL on failure
 *
 * Convenience function that allocates from the global buffer pool.
 * Equivalent to `data_buffer_pool_alloc(data_buffer_pool_get_global(), size)`.
 *
 * @note The global pool must be initialized before using this function.
 *
 * @note Thread-safe: Multiple threads can allocate simultaneously.
 *
 * @ingroup buffer_pool
 */
void *buffer_pool_alloc(size_t size);

/**
 * @brief Free a buffer back to the global pool
 * @param data Buffer pointer to free (must not be NULL)
 * @param size Size of buffer being freed
 *
 * Convenience function that frees to the global buffer pool.
 * Equivalent to `data_buffer_pool_free(data_buffer_pool_get_global(), data, size)`.
 *
 * @note The global pool must be initialized before using this function.
 *
 * @note Thread-safe: Multiple threads can free simultaneously.
 *
 * @ingroup buffer_pool
 */
void buffer_pool_free(void *data, size_t size);

/** @} */

/* ============================================================================
 * Enhanced Statistics
 * ============================================================================
 */

/**
 * @brief Detailed statistics structure for buffer pool analysis
 *
 * Contains per-pool statistics broken down by size class, allowing
 * detailed analysis of buffer pool performance.
 *
 * @ingroup buffer_pool
 */
typedef struct {
  /** @name Small Pool Statistics
   * @{
   */
  uint64_t small_hits;      ///< Small pool successful allocations
  uint64_t small_misses;    ///< Small pool malloc fallbacks
  uint64_t small_returns;   ///< Small pool successful returns
  uint64_t small_peak_used; ///< Small pool peak usage
  uint64_t small_bytes;     ///< Small pool total bytes allocated
  /** @} */

  /** @name Medium Pool Statistics
   * @{
   */
  uint64_t medium_hits;      ///< Medium pool successful allocations
  uint64_t medium_misses;    ///< Medium pool malloc fallbacks
  uint64_t medium_returns;   ///< Medium pool successful returns
  uint64_t medium_peak_used; ///< Medium pool peak usage
  uint64_t medium_bytes;     ///< Medium pool total bytes allocated
  /** @} */

  /** @name Large Pool Statistics
   * @{
   */
  uint64_t large_hits;      ///< Large pool successful allocations
  uint64_t large_misses;    ///< Large pool malloc fallbacks
  uint64_t large_returns;   ///< Large pool successful returns
  uint64_t large_peak_used; ///< Large pool peak usage
  uint64_t large_bytes;     ///< Large pool total bytes allocated
  /** @} */

  /** @name Extra Large Pool Statistics
   * @{
   */
  uint64_t xlarge_hits;      ///< Extra large pool successful allocations
  uint64_t xlarge_misses;    ///< Extra large pool malloc fallbacks
  uint64_t xlarge_returns;   ///< Extra large pool successful returns
  uint64_t xlarge_peak_used; ///< Extra large pool peak usage
  uint64_t xlarge_bytes;     ///< Extra large pool total bytes allocated
  /** @} */

  /** @name Aggregate Statistics
   * @{
   */
  uint64_t total_allocations;        ///< Total allocation requests across all pools
  uint64_t total_bytes;              ///< Total bytes allocated across all pools
  uint64_t total_pool_usage_percent; ///< Percentage of allocations satisfied from pools
  /** @} */
} buffer_pool_detailed_stats_t;

/**
 * @brief Get detailed statistics from buffer pool
 * @param pool Buffer pool manager (must not be NULL)
 * @param stats Output structure to receive statistics (must not be NULL)
 *
 * Retrieves detailed per-pool statistics broken down by size class.
 * Useful for performance analysis and optimization.
 *
 * @note All statistics counters are cumulative since pool creation.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_get_detailed_stats(data_buffer_pool_t *pool, buffer_pool_detailed_stats_t *stats);

/**
 * @brief Log buffer pool statistics to logging system
 * @param pool Buffer pool manager (must not be NULL)
 * @param pool_name Name identifier for the pool in log output (can be NULL)
 *
 * Logs detailed statistics from the buffer pool to the logging system.
 * Useful for periodic performance monitoring and debugging.
 *
 * @note Requires logging system to be initialized.
 *
 * @ingroup buffer_pool
 */
void data_buffer_pool_log_stats(data_buffer_pool_t *pool, const char *pool_name);

/**
 * @brief Log global buffer pool statistics
 *
 * Convenience function that logs statistics from the global singleton pool.
 * Equivalent to `data_buffer_pool_log_stats(data_buffer_pool_get_global(), "global")`.
 *
 * @note The global pool must be initialized before calling this function.
 *
 * @ingroup buffer_pool
 */
void buffer_pool_log_global_stats(void);
