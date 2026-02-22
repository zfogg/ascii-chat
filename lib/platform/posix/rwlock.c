/**
 * @file platform/posix/rwlock.c
 * @ingroup platform
 * @brief 🔓 POSIX pthread read-write lock implementation for multi-reader/single-writer synchronization
 */

#ifndef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/debug/named.h>
#include <pthread.h>

/**
 * @brief Initialize a read-write lock with a name
 * @param lock Pointer to read-write lock structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int rwlock_init_impl(rwlock_t *lock) {
  return pthread_rwlock_init(&lock->impl, NULL);
}

int rwlock_init(rwlock_t *lock, const char *name) {
  int err = pthread_rwlock_init(&lock->impl, NULL);
  if (err == 0) {
    lock->name = NAMED_REGISTER(lock, name ? name : "unnamed");
  }
  return err;
}

/**
 * @brief Destroy a read-write lock and free its resources
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, error code on failure
 */
int rwlock_destroy_impl(rwlock_t *lock) {
  return pthread_rwlock_destroy(&lock->impl);
}

int rwlock_destroy(rwlock_t *lock) {
  NAMED_UNREGISTER(lock);
  return rwlock_destroy_impl(lock);
}

/**
 * @brief Acquire a read lock (shared access) - implementation function
 * @param lock Pointer to read-write lock to acquire for reading
 * @return 0 on success, error code on failure
 */
int rwlock_rdlock_impl(rwlock_t *lock) {
  return pthread_rwlock_rdlock(&lock->impl);
}

/**
 * @brief Acquire a write lock (exclusive access) - implementation function
 * @param lock Pointer to read-write lock to acquire for writing
 * @return 0 on success, error code on failure
 */
int rwlock_wrlock_impl(rwlock_t *lock) {
  return pthread_rwlock_wrlock(&lock->impl);
}

/**
 * @brief Release a read lock (explicit read unlock) - implementation function
 * @param lock Pointer to read-write lock to release from read mode
 * @return 0 on success, error code on failure
 */
int rwlock_rdunlock_impl(rwlock_t *lock) {
  return pthread_rwlock_unlock(&lock->impl);
}

/**
 * @brief Release a write lock (explicit write unlock) - implementation function
 * @param lock Pointer to read-write lock to release from write mode
 * @return 0 on success, error code on failure
 */
int rwlock_wrunlock_impl(rwlock_t *lock) {
  return pthread_rwlock_unlock(&lock->impl);
}

#endif // !_WIN32
