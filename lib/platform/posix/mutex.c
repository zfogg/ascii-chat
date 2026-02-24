/**
 * @file platform/posix/mutex.c
 * @ingroup platform
 * @brief 🔒 POSIX pthread mutex implementation for cross-platform synchronization
 */

#ifndef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/debug/named.h>
#include <pthread.h>
#include <ascii-chat/asciichat_errno.h>

/**
 * @brief Initialize a mutex with a name
 * @param mutex Pointer to mutex structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int mutex_init(mutex_t *mutex, const char *name) {
  int err = pthread_mutex_init(&mutex->impl, NULL);
  if (err == 0) {
    mutex->name = NAMED_REGISTER(mutex, name, "mutex");
    mutex->last_lock_time_ns = 0;
    mutex->last_unlock_time_ns = 0;
    mutex->currently_held_by_key = 0;
  }
  return err;
}

/**
 * @brief Destroy a mutex and free its resources
 * @param mutex Pointer to mutex to destroy
 * @return 0 on success, error code on failure
 */
int mutex_destroy(mutex_t *mutex) {
  NAMED_UNREGISTER(mutex);
  return pthread_mutex_destroy(&mutex->impl);
}

/**
 * @brief Lock a mutex (blocking) - implementation function
 * @param mutex Pointer to mutex to lock
 * @return 0 on success, error code on failure
 */
int mutex_lock_impl(mutex_t *mutex) {
  int err = pthread_mutex_lock(&mutex->impl);
  if (err == 0) {
    mutex_on_lock(mutex);
  }
  return err;
}

/**
 * @brief Try to lock a mutex without blocking - implementation function
 * @param mutex Pointer to mutex to try locking
 * @return 0 on success, EBUSY if already locked, other error code on failure
 */
int mutex_trylock_impl(mutex_t *mutex) {
  return pthread_mutex_trylock(&mutex->impl);
}

/**
 * @brief Unlock a mutex - implementation function
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, error code on failure
 */
int mutex_unlock_impl(mutex_t *mutex) {
  mutex_on_unlock(mutex);
  return pthread_mutex_unlock(&mutex->impl);
}

#endif // !_WIN32
