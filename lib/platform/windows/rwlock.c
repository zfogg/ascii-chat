/**
 * @file rwlock.c
 * @brief Windows read-write lock implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows SRW Lock wrappers for the platform abstraction layer,
 * enabling cross-platform reader-writer synchronization using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "common.h"
#include <windows.h>

/**
 * @brief Initialize a read-write lock
 * @param lock Pointer to read-write lock structure to initialize
 * @return 0 on success, error code on failure
 */
int rwlock_init_impl(rwlock_t *lock) {
  InitializeSRWLock(lock);
  return 0;
}

// Public wrappers to match non-impl API used across the codebase
int rwlock_init(rwlock_t *lock) {
  return rwlock_init_impl(lock);
}

/**
 * @brief Destroy a read-write lock and free its resources
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, error code on failure
 * @note SRWLocks don't need explicit destruction on Windows
 */
int rwlock_destroy_impl(rwlock_t *lock) {
  // SRWLocks don't need explicit destruction
  (void)lock; // Suppress unused parameter warning
  return 0;
}

int rwlock_destroy(rwlock_t *lock) {
  return rwlock_destroy_impl(lock);
}

/**
 * @brief Acquire a read lock (shared access) - implementation function
 * @param lock Pointer to read-write lock to acquire for reading
 * @return 0 on success, error code on failure
 */
int rwlock_rdlock_impl(rwlock_t *lock) {
  AcquireSRWLockShared(lock);
  return 0;
}

/**
 * @brief Acquire a write lock (exclusive access) - implementation function
 * @param lock Pointer to read-write lock to acquire for writing
 * @return 0 on success, error code on failure
 */
int rwlock_wrlock_impl(rwlock_t *lock) {
  AcquireSRWLockExclusive(lock);
  return 0;
}

/**
 * @brief Release a read lock (explicit read unlock) - implementation function
 * @param lock Pointer to read-write lock to release from read mode
 * @return 0 on success, error code on failure
 */
int rwlock_rdunlock_impl(rwlock_t *lock) {
  ReleaseSRWLockShared(lock);
  return 0;
}

/**
 * @brief Release a write lock (explicit write unlock) - implementation function
 * @param lock Pointer to read-write lock to release from write mode
 * @return 0 on success, error code on failure
 */
int rwlock_wrunlock_impl(rwlock_t *lock) {
  ReleaseSRWLockExclusive(lock);
  return 0;
}

#endif // _WIN32
