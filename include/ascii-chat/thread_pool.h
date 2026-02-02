#pragma once

/**
 * @file thread_pool.h
 * @brief 🧵 Generic thread pool abstraction for managing worker threads
 * @ingroup threading
 *
 * Provides a reusable thread pool implementation for managing multiple worker
 * threads with ordered cleanup support. This abstraction is used by:
 * - tcp_server: Per-client thread pools for receive/send/render workers
 * - Server: Background threads (stats logger, etc.)
 * - ACDS: Background threads (rate limit cleanup, etc.)
 * - Client: Capture and display threads
 *
 * ## Features
 *
 * - **Thread lifecycle management**: Create, spawn, stop, and join threads
 * - **Ordered cleanup**: Threads with lower stop_id values are stopped first
 * - **Thread naming**: Assign names to threads for debugging
 * - **Thread-safe**: All operations are protected with mutexes
 * - **Flexible**: Supports both entity-bound pools and global singleton pools
 *
 * ## Usage Pattern
 *
 * ```c
 * // Create thread pool
 * thread_pool_t *pool = thread_pool_create("my_pool");
 *
 * // Spawn workers with ordered cleanup
 * thread_pool_spawn(pool, worker_func1, arg1, 1, "worker1"); // Stop first
 * thread_pool_spawn(pool, worker_func2, arg2, 2, "worker2"); // Stop second
 * thread_pool_spawn(pool, worker_func3, arg3, 3, "worker3"); // Stop last
 *
 * // Stop all threads in stop_id order (1 → 2 → 3)
 * thread_pool_stop_all(pool);
 *
 * // Destroy pool (frees all resources)
 * thread_pool_destroy(pool);
 * ```
 *
 * ## Ordered Cleanup Example (Server Per-Client Threads)
 *
 * ```c
 * // Client connection established
 * thread_pool_spawn(pool, receive_thread, client, 1, "receive_1");
 * thread_pool_spawn(pool, video_render, client, 2, "video_2");
 * thread_pool_spawn(pool, audio_render, client, 2, "audio_2");
 * thread_pool_spawn(pool, send_thread, client, 3, "send_3");
 *
 * // Client disconnect - threads stopped in order:
 * // 1. Receive thread (stop_id=1) - no new data arrives
 * // 2. Render threads (stop_id=2) - no new frames generated
 * // 3. Send thread (stop_id=3) - all queued data sent
 * thread_pool_stop_all(pool);
 * ```
 *
 * ## Background Thread Example (Stats Logger)
 *
 * ```c
 * // Server startup
 * thread_pool_t *server_pool = thread_pool_create("server");
 * thread_pool_spawn(server_pool, stats_logger, NULL, 0, "stats");
 *
 * // Server shutdown
 * thread_pool_stop_all(server_pool);
 * thread_pool_destroy(server_pool);
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/abstraction.h>

// Forward declaration
typedef struct thread_pool thread_pool_t;
typedef struct thread_pool_entry thread_pool_entry_t;

/**
 * @brief Thread pool entry (internal linked list node)
 *
 * Tracks individual threads in the pool. Threads are maintained in a
 * sorted linked list by stop_id for ordered cleanup.
 */
struct thread_pool_entry {
  asciichat_thread_t thread;      ///< Thread handle
  int stop_id;                    ///< Cleanup order (lower = stop first, -1 = unordered)
  void *(*thread_func)(void *);   ///< Thread function
  void *thread_arg;               ///< Thread argument
  char name[64];                  ///< Thread name for debugging
  struct thread_pool_entry *next; ///< Linked list next pointer
};

/**
 * @brief Thread pool structure
 *
 * Manages a collection of worker threads with ordered cleanup support.
 * Thread-safe for concurrent spawn/stop operations.
 */
struct thread_pool {
  char name[64];                ///< Pool name for debugging
  thread_pool_entry_t *threads; ///< Linked list of threads (sorted by stop_id)
  mutex_t threads_mutex;        ///< Mutex protecting thread list
  size_t thread_count;          ///< Number of threads in pool
};

/**
 * @brief Create a new thread pool
 *
 * Allocates and initializes a thread pool structure. The pool starts empty
 * with no threads. Use thread_pool_spawn() to add threads to the pool.
 *
 * @param pool_name Name for the pool (max 63 chars, used for debugging)
 * @return Pointer to new thread pool, or NULL on allocation failure
 */
thread_pool_t *thread_pool_create(const char *pool_name);

/**
 * @brief Destroy a thread pool
 *
 * Stops all threads (if not already stopped), waits for them to exit,
 * and frees all pool resources. It's safe to call this even if threads
 * are still running - they will be stopped first.
 *
 * @param pool Thread pool to destroy (NULL is safe, does nothing)
 */
void thread_pool_destroy(thread_pool_t *pool);

/**
 * @brief Spawn a worker thread in the pool
 *
 * Creates and tracks a new worker thread with optional ordered cleanup.
 * Threads are inserted into the pool's linked list in sorted order by
 * stop_id (ascending). When the pool is stopped, threads with lower
 * stop_id values are stopped first.
 *
 * **Stop ID Ordering Guidelines:**
 * - `stop_id < 0`: Unordered (stopped last, order undefined)
 * - `stop_id = 0`: General background workers
 * - `stop_id = 1`: Data source threads (e.g., receive, capture)
 * - `stop_id = 2`: Processing threads (e.g., render, encode)
 * - `stop_id = 3`: Data sink threads (e.g., send, write)
 *
 * **Example (Server Per-Client Threads):**
 * - stop_id=1: Receive thread (stop first to prevent new data)
 * - stop_id=2: Render threads (stop after receive)
 * - stop_id=3: Send thread (stop last after all processing)
 *
 * @param pool Thread pool to spawn into
 * @param thread_func Thread function to execute
 * @param thread_arg Argument passed to thread function
 * @param stop_id Cleanup order (-1=unordered, 0+=ordered)
 * @param thread_name Thread name for debugging (max 63 chars, NULL=auto-generate)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t thread_pool_spawn(thread_pool_t *pool, void *(*thread_func)(void *), void *thread_arg, int stop_id,
                                    const char *thread_name);

/**
 * @brief Stop all threads in the pool in stop_id order
 *
 * Joins all threads in ascending stop_id order (lower values first).
 * Threads with negative stop_id are stopped last in undefined order.
 * This function blocks until all threads have exited.
 *
 * **Shutdown Sequence:**
 * 1. Threads with stop_id >= 0 are joined in ascending order
 * 2. Threads with stop_id < 0 are joined in any order
 * 3. All thread entries are freed
 *
 * After this call, the pool is empty but still valid - you can spawn
 * new threads or destroy the pool.
 *
 * @param pool Thread pool to stop
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t thread_pool_stop_all(thread_pool_t *pool);

/**
 * @brief Get thread count in the pool
 *
 * Thread-safe count of active threads in the pool.
 *
 * @param pool Thread pool to query
 * @return Number of threads in pool, or 0 if pool is NULL
 */
size_t thread_pool_get_count(const thread_pool_t *pool);

/**
 * @brief Check if pool has any threads
 *
 * Convenience function equivalent to `thread_pool_get_count(pool) > 0`.
 *
 * @param pool Thread pool to check
 * @return true if pool has threads, false if empty or NULL
 */
bool thread_pool_has_threads(const thread_pool_t *pool);
