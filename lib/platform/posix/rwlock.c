/**
 * @file rwlock.c
 * @brief POSIX read-write lock implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides POSIX pthread read-write lock wrappers for the platform abstraction layer,
 * enabling cross-platform reader-writer synchronization using a unified API.
 */

#ifndef _WIN32

#include "../abstraction.h"
#include <pthread.h>

/**
 * @brief Initialize a read-write lock
 * @param lock Pointer to read-write lock structure to initialize
 * @return 0 on success, error code on failure
 */
int rwlock_init(rwlock_t *lock) {
  return pthread_rwlock_init(lock, NULL);
}

/**
 * @brief Destroy a read-write lock and free its resources
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, error code on failure
 */
int rwlock_destroy(rwlock_t *lock) {
  return pthread_rwlock_destroy(lock);
}

/**
 * @brief Acquire a read lock (shared access)
 * @param lock Pointer to read-write lock to acquire for reading
 * @return 0 on success, error code on failure
 */
int rwlock_rdlock(rwlock_t *lock) {
  return pthread_rwlock_rdlock(lock);
}

/**
 * @brief Acquire a write lock (exclusive access)
 * @param lock Pointer to read-write lock to acquire for writing
 * @return 0 on success, error code on failure
 */
int rwlock_wrlock(rwlock_t *lock) {
  return pthread_rwlock_wrlock(lock);
}

/**
 * @brief Release a read-write lock (generic unlock)
 * @param lock Pointer to read-write lock to release
 * @return 0 on success, error code on failure
 * @note This works for both read and write locks on POSIX systems
 */
int rwlock_unlock(rwlock_t *lock) {
  return pthread_rwlock_unlock(lock);
}

/**
 * @brief Release a read lock (explicit read unlock)
 * @param lock Pointer to read-write lock to release from read mode
 * @return 0 on success, error code on failure
 */
int rwlock_rdunlock(rwlock_t *lock) {
  return pthread_rwlock_unlock(lock);
}

/**
 * @brief Release a write lock (explicit write unlock)
 * @param lock Pointer to read-write lock to release from write mode
 * @return 0 on success, error code on failure
 */
int rwlock_wrunlock(rwlock_t *lock) {
  return pthread_rwlock_unlock(lock);
}

#endif // !_WIN32