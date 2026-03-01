#pragma once

/**
 * @file platform/cond.h
 * @brief Cross-platform condition variable interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides a unified condition variable interface that abstracts platform-specific
 * implementations (Windows Condition Variables vs POSIX pthread condition variables).
 *
 * The interface provides:
 * - Condition variable initialization and destruction
 * - Waiting on condition variables (with associated mutex)
 * - Timed waiting with timeout support
 * - Signaling and broadcasting to waiting threads
 *
 * @note On Windows, uses CONDITION_VARIABLE.
 *       On POSIX systems, uses pthread_cond_t.
 *
 * @note Condition variables must be used with a mutex (mutex_t) for proper synchronization.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <stdint.h>
#include "mutex.h"
#include <ascii-chat/atomic.h>

#ifdef _WIN32
#include "windows_compat.h"
/**
 * @brief Condition variable type (Windows: CONDITION_VARIABLE with name)
 * @ingroup platform
 */
typedef struct {
    CONDITION_VARIABLE impl; ///< Underlying Windows condition variable
    const char *name;        ///< Human-readable name for debugging
    uint64_t last_signal_time_ns;  ///< Timestamp of last signal (nanoseconds)
    uint64_t last_broadcast_time_ns; ///< Timestamp of last broadcast (nanoseconds)
    uint64_t last_wait_time_ns;    ///< Timestamp of last wait (nanoseconds)
    atomic_t waiting_count;        ///< Number of threads currently waiting
    uintptr_t last_waiting_key;     ///< Registry key of most recent waiter
    mutex_t *last_wait_mutex;       ///< Associated mutex at most recent wait (for deadlock detection)
    const char *last_wait_file;     ///< Callsite file of most recent cond_wait (for deadlock detection)
    int        last_wait_line;      ///< Callsite line of most recent cond_wait (for deadlock detection)
    const char *last_wait_func;     ///< Callsite function of most recent cond_wait (for deadlock detection)
} cond_t;
#else
#include <pthread.h>
/**
 * @brief Condition variable type (POSIX: pthread_cond_t with name)
 * @ingroup platform
 */
typedef struct {
    pthread_cond_t impl;     ///< Underlying POSIX condition variable
    const char *name;        ///< Human-readable name for debugging
    uint64_t last_signal_time_ns;  ///< Timestamp of last signal (nanoseconds)
    uint64_t last_broadcast_time_ns; ///< Timestamp of last broadcast (nanoseconds)
    uint64_t last_wait_time_ns;    ///< Timestamp of last wait (nanoseconds)
    atomic_t waiting_count;        ///< Number of threads currently waiting
    uintptr_t last_waiting_key;     ///< Registry key of most recent waiter
    mutex_t *last_wait_mutex;       ///< Associated mutex at most recent wait (for deadlock detection)
    const char *last_wait_file;     ///< Callsite file of most recent cond_wait (for deadlock detection)
    int        last_wait_line;      ///< Callsite line of most recent cond_wait (for deadlock detection)
    const char *last_wait_func;     ///< Callsite function of most recent cond_wait (for deadlock detection)
} cond_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Condition Variable Functions
// ============================================================================

/**
 * @brief Initialize a condition variable with a name
 * @param cond Pointer to condition variable to initialize
 * @param name Human-readable name for debugging (e.g., "audio_ready")
 * @return 0 on success, non-zero on error
 *
 * Initializes the condition variable for use. Must be called before any other
 * condition variable operations. The name is stored for debugging and automatically
 * suffixed with a unique counter.
 *
 * @ingroup platform
 */
int cond_init(cond_t *cond, const char *name);

/**
 * @brief Destroy a condition variable
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, non-zero on error
 *
 * Destroys the condition variable and frees any associated resources.
 * No threads should be waiting on the condition variable when this is called.
 *
 * @ingroup platform
 */
int cond_destroy(cond_t *cond);

/**
 * @brief Wait on a condition variable (blocking) - implementation function
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to mutex that must be locked by the calling thread
 * @return 0 on success, non-zero on error
 *
 * Atomically unlocks the mutex and waits on the condition variable.
 * The mutex must be locked by the calling thread before calling this function.
 * Upon return, the mutex will be locked again.
 *
 * @warning The mutex must be locked before calling this function.
 * @note Use cond_wait() macro instead of calling this directly
 *
 * @ingroup platform
 */
int cond_wait_impl(cond_t *cond, mutex_t *mutex);

/**
 * @brief Wait on a condition variable with timeout - implementation function
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to mutex that must be locked by the calling thread
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if condition was signaled, non-zero on timeout or error
 *
 * Atomically unlocks the mutex and waits on the condition variable with a timeout.
 * Returns non-zero if the timeout expires before the condition is signaled.
 *
 * @warning The mutex must be locked before calling this function.
 * @note Use cond_timedwait() macro instead of calling this directly
 *
 * @ingroup platform
 */
int cond_timedwait_impl(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns);

/**
 * @brief Signal a condition variable (wake one waiting thread)
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, non-zero on error
 *
 * Wakes up one thread that is waiting on the condition variable.
 * If no threads are waiting, the signal is lost.
 *
 * @ingroup platform
 */
int cond_signal(cond_t *cond);

/**
 * @brief Broadcast to a condition variable (wake all waiting threads)
 * @param cond Pointer to condition variable to broadcast
 * @return 0 on success, non-zero on error
 *
 * Wakes up all threads that are waiting on the condition variable.
 * If no threads are waiting, the broadcast has no effect.
 *
 * @ingroup platform
 */
int cond_broadcast(cond_t *cond);

/**
 * @brief Hook called when a thread waits on a condition variable
 * @param cond Pointer to the condition variable being waited on
 * @param mutex Pointer to the associated mutex
 * @param file Source file of the wait callsite (for deadlock detection)
 * @param line Source line of the wait callsite (for deadlock detection)
 * @param func Source function of the wait callsite (for deadlock detection)
 *
 * Called by platform-specific implementations before blocking on wait.
 * Records timing, callsite information, and associated mutex for deadlock detection.
 *
 * @ingroup platform
 */
void cond_on_wait(cond_t *cond, mutex_t *mutex, const char *file, int line, const char *func);

// Forward declare debug_sync functions (only in debug builds)
#ifndef NDEBUG
int debug_sync_cond_wait(cond_t *cond, mutex_t *mutex, const char *file, int line, const char *func);
int debug_sync_cond_timedwait(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns, const char *file, int line, const char *func);
bool debug_sync_is_initialized(void);
#endif

/**
 * @brief Hook called when a condition variable is signaled
 * @param cond Pointer to the condition variable being signaled
 *
 * Called by platform-specific implementations after waking one thread.
 * Records timing and other diagnostic data.
 *
 * @ingroup platform
 */
void cond_on_signal(cond_t *cond);

/**
 * @brief Hook called when a condition variable is broadcast
 * @param cond Pointer to the condition variable being broadcast
 *
 * Called by platform-specific implementations after waking all threads.
 * Records timing and other diagnostic data.
 *
 * @ingroup platform
 */
void cond_on_broadcast(cond_t *cond);

/**
 * @name Condition Variable Wait Macros
 * @{
 */

/**
 * @brief Wait on a condition variable (with debug tracking in debug builds)
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to mutex that must be locked by the calling thread
 * @return 0 on success, non-zero on error
 *
 * Atomically unlocks the mutex and waits on the condition variable.
 * The mutex must be locked by the calling thread before calling this function.
 * Upon return, the mutex will be locked again.
 *
 * @note In debug builds, this macro includes deadlock detection if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @warning The mutex must be locked before calling this function.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define cond_wait(cond, mutex) cond_wait_impl(cond, mutex)
#else
#define cond_wait(cond, mutex)                                                                                         \
  (debug_sync_is_initialized() ? debug_sync_cond_wait(cond, mutex, __FILE__, __LINE__, __func__)                       \
                               : cond_wait_impl(cond, mutex))
#endif

/**
 * @brief Wait on a condition variable with timeout (with debug tracking in debug builds)
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to mutex that must be locked by the calling thread
 * @param timeout_ns Timeout in nanoseconds
 * @return 0 if condition was signaled, non-zero on timeout or error
 *
 * Atomically unlocks the mutex and waits on the condition variable with a timeout.
 * Returns non-zero if the timeout expires before the condition is signaled.
 *
 * @note In debug builds, this macro includes deadlock detection if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @warning The mutex must be locked before calling this function.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define cond_timedwait(cond, mutex, timeout_ns) cond_timedwait_impl(cond, mutex, timeout_ns)
#else
#define cond_timedwait(cond, mutex, timeout_ns)                                                                        \
  (debug_sync_is_initialized()                                                                                         \
     ? debug_sync_cond_timedwait(cond, mutex, timeout_ns, __FILE__, __LINE__, __func__)                               \
     : cond_timedwait_impl(cond, mutex, timeout_ns))
#endif

/** @} */ /* Condition Variable Wait Macros */

#ifdef __cplusplus
}
#endif

/** @} */