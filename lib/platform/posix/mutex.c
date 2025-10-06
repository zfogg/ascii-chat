/**
 * @file mutex.c
 * @brief POSIX mutex implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides POSIX pthread mutex wrappers for the platform abstraction layer,
 * enabling cross-platform mutex synchronization using a unified API.
 */

#ifndef _WIN32

#include "../abstraction.h"
#include <pthread.h>

/**
 * @brief Initialize a mutex
 * @param mutex Pointer to mutex structure to initialize
 * @return 0 on success, error code on failure
 */
int mutex_init(mutex_t *mutex) {
  return pthread_mutex_init(mutex, NULL);
}

/**
 * @brief Destroy a mutex and free its resources
 * @param mutex Pointer to mutex to destroy
 * @return 0 on success, error code on failure
 */
int mutex_destroy(mutex_t *mutex) {
  return pthread_mutex_destroy(mutex);
}

/**
 * @brief Lock a mutex (blocking) - implementation function
 * @param mutex Pointer to mutex to lock
 * @return 0 on success, error code on failure
 */
int mutex_lock_impl(mutex_t *mutex) {
  return pthread_mutex_lock(mutex);
}

/**
 * @brief Try to lock a mutex without blocking
 * @param mutex Pointer to mutex to try locking
 * @return 0 on success, EBUSY if already locked, other error code on failure
 */
int mutex_trylock(mutex_t *mutex) {
  return pthread_mutex_trylock(mutex);
}

/**
 * @brief Unlock a mutex - implementation function
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, error code on failure
 */
int mutex_unlock_impl(mutex_t *mutex) {
  return pthread_mutex_unlock(mutex);
}

#endif // !_WIN32
