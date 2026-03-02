/**
 * @file platform/posix/mutex.c
 * @ingroup platform
 * @brief 🔒 POSIX pthread mutex implementation for cross-platform synchronization
 */

#ifndef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
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
  log_info("[MUTEX_INIT_DBG] pthread_mutex_init=%d, name=%s", err, name);
  if (err == 0) {
    log_info("[MUTEX_INIT_DBG] About to call NAMED_REGISTER_MUTEX with name=%s at %p", name, (void *)mutex);
    fflush(stdout);
    fflush(stderr);
    // Add a timeout mechanism to detect deadlocks
    mutex->name = NAMED_REGISTER_MUTEX(mutex, name);
    log_info("[MUTEX_INIT_DBG] NAMED_REGISTER_MUTEX returned: %s", mutex->name);
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
  // Track lock attempt on stack
  mutex_stack_push_pending((uintptr_t)mutex, mutex->name);

  int err = pthread_mutex_lock(&mutex->impl);
  if (err == 0) {
    // Mark as successfully locked
    mutex_stack_mark_locked((uintptr_t)mutex);
    mutex_on_lock(mutex);
  } else {
    // Lock failed, remove from stack
    mutex_stack_pop((uintptr_t)mutex);
  }
  return err;
}

/**
 * @brief Try to lock a mutex without blocking - implementation function
 * @param mutex Pointer to mutex to try locking
 * @return 0 on success, EBUSY if already locked, other error code on failure
 */
int mutex_trylock_impl(mutex_t *mutex) {
  // Track lock attempt on stack
  mutex_stack_push_pending((uintptr_t)mutex, mutex->name);

  int err = pthread_mutex_trylock(&mutex->impl);
  if (err == 0) {
    // Mark as successfully locked
    mutex_stack_mark_locked((uintptr_t)mutex);
  } else {
    // Lock failed, remove from stack
    mutex_stack_pop((uintptr_t)mutex);
  }
  return err;
}

/**
 * @brief Unlock a mutex - implementation function
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, error code on failure
 */
int mutex_unlock_impl(mutex_t *mutex) {
  mutex_on_unlock(mutex);
  int err = pthread_mutex_unlock(&mutex->impl);

  // Pop from lock stack after successful unlock
  if (err == 0) {
    mutex_stack_pop((uintptr_t)mutex);
  }

  return err;
}

#endif // !_WIN32
