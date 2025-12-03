#pragma once

/**
 * @file platform/rwlock.h
 * @brief Cross-platform read-write lock interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides a unified read-write lock interface that abstracts platform-specific
 * implementations (Windows SRW Locks vs POSIX pthread read-write locks).
 *
 * The interface provides:
 * - Read-write lock initialization and destruction
 * - Shared read lock acquisition (multiple readers allowed)
 * - Exclusive write lock acquisition (exclusive access)
 * - Lock release operations
 * - Debug-enabled macros with lock tracking (in debug builds)
 *
 * @note On Windows, uses SRWLOCK for lightweight synchronization.
 *       On POSIX systems, uses pthread_rwlock_t.
 *
 * @note In debug builds, rwlock_rdlock(), rwlock_wrlock(), rwlock_rdunlock(), and
 *       rwlock_wrunlock() macros use lock debugging if enabled. In release builds,
 *       they call the implementation directly for zero overhead.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

#ifdef _WIN32
#include "windows_compat.h"
/** @brief Read-write lock type (Windows: SRWLOCK) */
typedef SRWLOCK rwlock_t;
#else
#include <pthread.h>
/** @brief Read-write lock type (POSIX: pthread_rwlock_t) */
typedef pthread_rwlock_t rwlock_t;
#endif

// Forward declarations for lock debugging
// Note: Full lock_debug.h cannot be included here due to circular dependencies
// Files that use both rwlock and lock_debug must include lock_debug.h separately
#ifndef NDEBUG
bool lock_debug_is_initialized(void);
int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
int debug_rwlock_wrunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
#endif

// ============================================================================
// Read-Write Lock Functions
// ============================================================================

/**
 * @brief Initialize a read-write lock
 * @param lock Pointer to read-write lock to initialize
 * @return 0 on success, non-zero on error
 *
 * Initializes the read-write lock for use. Must be called before any other
 * lock operations.
 *
 * @ingroup platform
 */
int rwlock_init(rwlock_t *lock);

/**
 * @brief Destroy a read-write lock
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, non-zero on error
 *
 * Destroys the read-write lock and frees any associated resources.
 * The lock must not be held by any thread when this is called.
 *
 * @ingroup platform
 */
int rwlock_destroy(rwlock_t *lock);

/**
 * @brief Initialize a read-write lock (implementation function)
 * @param lock Pointer to read-write lock to initialize
 * @return 0 on success, non-zero on error
 *
 * @note This is the implementation function. Use rwlock_init() instead.
 *
 * @ingroup platform
 */
int rwlock_init_impl(rwlock_t *lock);

/**
 * @brief Destroy a read-write lock (implementation function)
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, non-zero on error
 *
 * @note This is the implementation function. Use rwlock_destroy() instead.
 *
 * @ingroup platform
 */
int rwlock_destroy_impl(rwlock_t *lock);

/**
 * @brief Acquire a read lock (implementation function)
 * @param lock Pointer to read-write lock
 * @return 0 on success, non-zero on error
 *
 * Acquires a shared read lock. Multiple threads can hold read locks simultaneously.
 * Blocks if a write lock is held.
 *
 * @note This is the implementation function. Use rwlock_rdlock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int rwlock_rdlock_impl(rwlock_t *lock);

/**
 * @brief Acquire a write lock (implementation function)
 * @param lock Pointer to read-write lock
 * @return 0 on success, non-zero on error
 *
 * Acquires an exclusive write lock. Only one thread can hold a write lock,
 * and it excludes all read locks. Blocks if any locks are held.
 *
 * @note This is the implementation function. Use rwlock_wrlock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int rwlock_wrlock_impl(rwlock_t *lock);

/**
 * @brief Release a read lock (implementation function)
 * @param lock Pointer to read-write lock
 * @return 0 on success, non-zero on error
 *
 * Releases a shared read lock held by the calling thread.
 *
 * @note This is the implementation function. Use rwlock_rdunlock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int rwlock_rdunlock_impl(rwlock_t *lock);

/**
 * @brief Release a write lock (implementation function)
 * @param lock Pointer to read-write lock
 * @return 0 on success, non-zero on error
 *
 * Releases an exclusive write lock held by the calling thread.
 *
 * @note This is the implementation function. Use rwlock_wrunlock() macro instead,
 *       which includes debug tracking in debug builds.
 *
 * @ingroup platform
 */
int rwlock_wrunlock_impl(rwlock_t *lock);

/**
 * @name Read-Write Lock Macros
 * @{
 */

/**
 * @brief Acquire a read lock (with debug tracking in debug builds)
 * @param lock Pointer to read-write lock
 *
 * Acquires a shared read lock. Multiple threads can hold read locks simultaneously.
 * Blocks if a write lock is held.
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define rwlock_rdlock(lock) rwlock_rdlock_impl(lock)
#else
#define rwlock_rdlock(lock)                                                                                            \
  (lock_debug_is_initialized() ? debug_rwlock_rdlock(lock, __FILE__, __LINE__, __func__) : rwlock_rdlock_impl(lock))
#endif

/**
 * @brief Acquire a write lock (with debug tracking in debug builds)
 * @param lock Pointer to read-write lock
 *
 * Acquires an exclusive write lock. Only one thread can hold a write lock,
 * and it excludes all read locks. Blocks if any locks are held.
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define rwlock_wrlock(lock) rwlock_wrlock_impl(lock)
#else
#define rwlock_wrlock(lock)                                                                                            \
  (lock_debug_is_initialized() ? debug_rwlock_wrlock(lock, __FILE__, __LINE__, __func__) : rwlock_wrlock_impl(lock))
#endif

/**
 * @brief Release a read lock (with debug tracking in debug builds)
 * @param lock Pointer to read-write lock
 *
 * Releases a shared read lock held by the calling thread.
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define rwlock_rdunlock(lock) rwlock_rdunlock_impl(lock)
#else
#define rwlock_rdunlock(lock)                                                                                          \
  (lock_debug_is_initialized() ? debug_rwlock_rdunlock(lock, __FILE__, __LINE__, __func__) : rwlock_rdunlock_impl(lock))
#endif

/**
 * @brief Release a write lock (with debug tracking in debug builds)
 * @param lock Pointer to read-write lock
 *
 * Releases an exclusive write lock held by the calling thread.
 *
 * @note In debug builds, this macro includes lock debugging if initialized.
 *       In release builds, calls the implementation directly for zero overhead.
 *
 * @ingroup platform
 */
#ifdef NDEBUG
#define rwlock_wrunlock(lock) rwlock_wrunlock_impl(lock)
#else
#define rwlock_wrunlock(lock)                                                                                          \
  (lock_debug_is_initialized() ? debug_rwlock_wrunlock(lock, __FILE__, __LINE__, __func__) : rwlock_wrunlock_impl(lock))
#endif

/** @} */
