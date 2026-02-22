#pragma once

/**
 * @file platform/mutex.h
 * @brief Cross-platform mutex interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides a unified mutex interface that abstracts platform-specific
 * implementations (Windows Critical Sections vs POSIX pthread mutexes).
 *
 * The interface provides:
 * - Mutex initialization and destruction
 * - Locking and unlocking operations
 * - Try-lock for non-blocking acquisition
 * - Debug-enabled macros with lock tracking (in debug builds)
 *
 * @note On Windows, uses CRITICAL_SECTION for lightweight synchronization.
 *       On POSIX systems, uses pthread_mutex_t.
 *
 * @note In debug builds, mutex_lock() and mutex_unlock() macros use lock debugging
 *       if enabled. In release builds, they call the implementation directly for zero overhead.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

#ifdef _WIN32
#include "windows_compat.h"
/**
 * @brief Mutex type (Windows: CRITICAL_SECTION with name)
 * @ingroup platform
 */
typedef struct {
    CRITICAL_SECTION impl;   ///< Underlying Windows critical section
    const char *name;        ///< Human-readable name for debugging
    uint64_t last_lock_time_ns;   ///< Timestamp of last lock acquisition (nanoseconds)
    uint64_t last_unlock_time_ns; ///< Timestamp of last unlock (nanoseconds)
} mutex_t;
#else
#include <pthread.h>
/**
 * @brief Mutex type (POSIX: pthread_mutex_t with name)
 * @ingroup platform
 */
typedef struct {
    pthread_mutex_t impl;    ///< Underlying POSIX mutex
    const char *name;        ///< Human-readable name for debugging
    uint64_t last_lock_time_ns;   ///< Timestamp of last lock acquisition (nanoseconds)
    uint64_t last_unlock_time_ns; ///< Timestamp of last unlock (nanoseconds)
} mutex_t;
#endif

// Forward declare debug_mutex functions (full declarations in debug/lock.h)
#ifdef __cplusplus
extern "C" {
#endif

#ifndef NDEBUG
// Only declare these when not in release mode
// The actual implementations are in lib/debug/lock.c when DEBUG_LOCKS is enabled
int debug_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);
int debug_mutex_trylock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);
int debug_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);
bool lock_debug_is_initialized(void);
#endif

// ============================================================================
// Mutex Functions
// ============================================================================

/**
 * @brief Initialize a mutex with a name
 * @param mutex Pointer to mutex to initialize
 * @param name Human-readable name for debugging (e.g., "recv_mutex")
 * @return 0 on success, non-zero on error
 *
 * Initializes the mutex for use. Must be called before any other mutex operations.
 * The name is stored for debugging and automatically suffixed with a unique counter.
 *
 * @ingroup platform
 */
int mutex_init(mutex_t *mutex, const char *name);

/**
 * @brief Destroy a mutex
 * @param mutex Pointer to mutex to destroy
 * @return 0 on success, non-zero on error
 *
 * Destroys the mutex and frees any associated resources.
 * The mutex must not be locked when this is called.
 *
 * @ingroup platform
 */
int mutex_destroy(mutex_t *mutex);

/**
 * @brief Hook called when a mutex is successfully locked
 * @param mutex Pointer to the mutex that was locked
 *
 * Called by platform-specific implementations after lock acquisition.
 * Records timing and other diagnostic data.
 *
 * @ingroup platform
 */
void mutex_on_lock(mutex_t *mutex);

/**
 * @brief Hook called when a mutex is unlocked
 * @param mutex Pointer to the mutex that was unlocked
 *
 * Called by platform-specific implementations before lock release.
 * Records timing and other diagnostic data.
 *
 * @ingroup platform
 */
void mutex_on_unlock(mutex_t *mutex);

/**
 * @brief Lock a mutex (implementation function)
 * @param mutex Pointer to mutex to lock
 * @return 0 on success, non-zero on error
 *
 * @note This is the implementation function. Use mutex_lock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int mutex_lock_impl(mutex_t *mutex);

/**
 * @brief Try to lock a mutex without blocking (implementation function)
 * @param mutex Pointer to mutex to try to lock
 * @return 0 if lock was acquired, non-zero if mutex was already locked
 *
 * Attempts to acquire the mutex lock without blocking. Returns immediately
 * whether the lock was acquired or not.
 *
 * @note This is the implementation function. Use mutex_trylock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int mutex_trylock_impl(mutex_t *mutex);

/**
 * @brief Unlock a mutex (implementation function)
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, non-zero on error
 *
 * @note This is the implementation function. Use mutex_unlock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int mutex_unlock_impl(mutex_t *mutex);

/**
 * @name Mutex Locking Macros
 * @{
 */

/**
 * @brief Lock a mutex (with debug tracking in debug builds)
 * @param mutex Pointer to mutex to lock
 *
 * Locks the mutex, blocking if necessary until the lock is acquired.
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define mutex_lock(mutex) mutex_lock_impl(mutex)
#else
#define mutex_lock(mutex)                                                                                              \
  (lock_debug_is_initialized() ? debug_mutex_lock(mutex, __FILE__, __LINE__, __func__) : mutex_lock_impl(mutex))
#endif

/**
 * @brief Try to lock a mutex without blocking (with debug tracking in debug builds)
 * @param mutex Pointer to mutex to try to lock
 * @return 0 if lock was acquired, non-zero if mutex was already locked
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define mutex_trylock(mutex) mutex_trylock_impl(mutex)
#else
#define mutex_trylock(mutex)                                                                                           \
  (lock_debug_is_initialized() ? debug_mutex_trylock(mutex, __FILE__, __LINE__, __func__) : mutex_trylock_impl(mutex))
#endif

/**
 * @brief Unlock a mutex (with debug tracking in debug builds)
 * @param mutex Pointer to mutex to unlock
 *
 * Unlocks the mutex. The mutex must be locked by the current thread.
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define mutex_unlock(mutex) mutex_unlock_impl(mutex)
#else
#define mutex_unlock(mutex)                                                                                            \
  (lock_debug_is_initialized() ? debug_mutex_unlock(mutex, __FILE__, __LINE__, __func__) : mutex_unlock_impl(mutex))
#endif

/** @} */ /* Mutex Locking Macros */

#ifdef __cplusplus
}
#endif

/** @} */ /* platform */
