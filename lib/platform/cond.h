#pragma once

/**
 * @file platform/cond.h
 * @ingroup module_platform
 * @brief Cross-platform condition variable interface for ASCII-Chat
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
#include "mutex.h"

#ifdef _WIN32
#include "windows_compat.h"
/** @brief Condition variable type (Windows: CONDITION_VARIABLE) */
typedef CONDITION_VARIABLE cond_t;
#else
#include <pthread.h>
/** @brief Condition variable type (POSIX: pthread_cond_t) */
typedef pthread_cond_t cond_t;
#endif

// ============================================================================
// Condition Variable Functions
// ============================================================================

/**
 * @brief Initialize a condition variable
 * @param cond Pointer to condition variable to initialize
 * @return 0 on success, non-zero on error
 *
 * Initializes the condition variable for use. Must be called before any other
 * condition variable operations.
 *
 * @ingroup module_platform
 */
int cond_init(cond_t *cond);

/**
 * @brief Destroy a condition variable
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, non-zero on error
 *
 * Destroys the condition variable and frees any associated resources.
 * No threads should be waiting on the condition variable when this is called.
 *
 * @ingroup module_platform
 */
int cond_destroy(cond_t *cond);

/**
 * @brief Wait on a condition variable (blocking)
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to mutex that must be locked by the calling thread
 * @return 0 on success, non-zero on error
 *
 * Atomically unlocks the mutex and waits on the condition variable.
 * The mutex must be locked by the calling thread before calling this function.
 * Upon return, the mutex will be locked again.
 *
 * @warning The mutex must be locked before calling this function.
 *
 * @ingroup module_platform
 */
int cond_wait(cond_t *cond, mutex_t *mutex);

/**
 * @brief Wait on a condition variable with timeout
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to mutex that must be locked by the calling thread
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if condition was signaled, non-zero on timeout or error
 *
 * Atomically unlocks the mutex and waits on the condition variable with a timeout.
 * Returns non-zero if the timeout expires before the condition is signaled.
 *
 * @warning The mutex must be locked before calling this function.
 *
 * @ingroup module_platform
 */
int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms);

/**
 * @brief Signal a condition variable (wake one waiting thread)
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, non-zero on error
 *
 * Wakes up one thread that is waiting on the condition variable.
 * If no threads are waiting, the signal is lost.
 *
 * @ingroup module_platform
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
 * @ingroup module_platform
 */
int cond_broadcast(cond_t *cond);
