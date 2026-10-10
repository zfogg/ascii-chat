/**
 * @file platform/windows/mutex.c
 * @ingroup platform
 * @brief 🔒 Windows Critical Section implementation for cross-platform synchronization
 */

#ifdef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/platform/windows_compat.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/asciichat_errno.h>

/**
 * @brief Initialize a mutex with a name
 * @param mutex Pointer to mutex structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int mutex_init(mutex_t *mutex, const char *name) {
  InitializeCriticalSectionAndSpinCount(&mutex->impl, 4000);
#ifndef NDEBUG
  mutex->last_lock_time_ns = 0;
  mutex->last_unlock_time_ns = 0;
  mutex->currently_held_by_key = 0;
  mutex->lock_count = 0;
  mutex->unlock_count = 0;
  mutex->trylock_count = 0;
  mutex->trylock_success_count = 0;
#endif
  mutex->name = NAMED_REGISTER_MUTEX(mutex, name, NULL);
  return 0;
}

/**
 * @brief Destroy a mutex and free its resources
 * @param mutex Pointer to mutex to destroy
 * @return 0 on success, error code on failure
 */
int mutex_destroy(mutex_t *mutex) {
  NAMED_UNREGISTER(mutex);
  DeleteCriticalSection(&mutex->impl);
  return 0;
}

/**
 * @brief Lock a mutex (blocking) - implementation function
 * @param mutex Pointer to mutex to lock
 * @return 0 on success, error code on failure
 */
int mutex_lock_impl(mutex_t *mutex) {
  // Auto-initialize if the CRITICAL_SECTION hasn't been initialized yet.
  // On Windows, zero-initialized CRITICAL_SECTION is invalid (unlike POSIX pthread_mutex).
  // Check DebugInfo field which is NULL only when uninitialized.
  if (mutex->impl.DebugInfo == NULL) {
    InitializeCriticalSection(&mutex->impl);
  }
  // Recursive acquisitions succeed immediately; never report them as waits.
  if (TryEnterCriticalSection(&mutex->impl)) {
    mutex_stack_push_locked((uintptr_t)mutex, mutex->name);
  } else {
    mutex_stack_push_pending((uintptr_t)mutex, mutex->name);
    EnterCriticalSection(&mutex->impl);
    mutex_stack_mark_locked((uintptr_t)mutex);
  }
  mutex_on_lock(mutex);
  return 0;
}

/**
 * @brief Try to lock a mutex without blocking - implementation function
 * @param mutex Pointer to mutex to try locking
 * @return 0 on success, EBUSY if already locked, other error code on failure
 */
int mutex_trylock_impl(mutex_t *mutex) {
  BOOL success = TryEnterCriticalSection(&mutex->impl);
  if (success)
    mutex_stack_push_locked((uintptr_t)mutex, mutex->name);
  mutex_on_trylock(mutex, success ? true : false);
  return success ? 0 : 16; // EBUSY = 16
}

/**
 * @brief Unlock a mutex - implementation function
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, error code on failure
 */
int mutex_unlock_impl(mutex_t *mutex) {
#ifndef NDEBUG
  uintptr_t owner = mutex->currently_held_by_key;
  bool recursive = mutex->impl.RecursionCount > 1;
#endif
  mutex_on_unlock(mutex);
#ifndef NDEBUG
  if (recursive)
    mutex->currently_held_by_key = owner;
#endif
  mutex_stack_pop((uintptr_t)mutex);
  LeaveCriticalSection(&mutex->impl);
  return 0;
}

#endif // _WIN32
