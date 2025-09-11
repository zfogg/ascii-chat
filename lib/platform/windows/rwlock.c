/**
 * @file rwlock.c
 * @brief Windows read-write lock implementation for ASCII-Chat platform abstraction layer
 * 
 * This file provides Windows SRW Lock wrappers for the platform abstraction layer,
 * enabling cross-platform reader-writer synchronization using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include <windows.h>

/**
 * @brief Initialize a read-write lock
 * @param lock Pointer to read-write lock structure to initialize
 * @return 0 on success, error code on failure
 */
int rwlock_init(rwlock_t *lock) {
    InitializeSRWLock(&lock->lock);
    return 0;
}

/**
 * @brief Destroy a read-write lock and free its resources
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, error code on failure
 * @note SRWLocks don't need explicit destruction on Windows
 */
int rwlock_destroy(rwlock_t *lock) {
    // SRWLocks don't need explicit destruction
    (void)lock; // Suppress unused parameter warning
    return 0;
}

/**
 * @brief Acquire a read lock (shared access)
 * @param lock Pointer to read-write lock to acquire for reading
 * @return 0 on success, error code on failure
 */
int rwlock_rdlock(rwlock_t *lock) {
    AcquireSRWLockShared(&lock->lock);
    return 0;
}

/**
 * @brief Acquire a write lock (exclusive access)
 * @param lock Pointer to read-write lock to acquire for writing
 * @return 0 on success, error code on failure
 */
int rwlock_wrlock(rwlock_t *lock) {
    AcquireSRWLockExclusive(&lock->lock);
    return 0;
}

/**
 * @brief Release a read-write lock (generic unlock)
 * @param lock Pointer to read-write lock to release
 * @return 0 on success, error code on failure
 * @note This is a limitation of SRWLock - we don't track lock type
 *       so we try to release as writer first, then as reader
 */
int rwlock_unlock(rwlock_t *lock) {
    // Try to release as writer first, then as reader
    // This is a limitation of SRWLock - we don't track lock type
    ReleaseSRWLockExclusive(&lock->lock);
    return 0;
}

/**
 * @brief Release a read lock (explicit read unlock)
 * @param lock Pointer to read-write lock to release from read mode
 * @return 0 on success, error code on failure
 */
int rwlock_rdunlock(rwlock_t *lock) {
    ReleaseSRWLockShared(&lock->lock);
    return 0;
}

/**
 * @brief Release a write lock (explicit write unlock)
 * @param lock Pointer to read-write lock to release from write mode
 * @return 0 on success, error code on failure
 */
int rwlock_wrunlock(rwlock_t *lock) {
    ReleaseSRWLockExclusive(&lock->lock);
    return 0;
}

#endif // _WIN32