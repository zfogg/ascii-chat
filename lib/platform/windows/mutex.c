/**
 * @file platform/windows/mutex.c
 * @ingroup platform
 * @brief 🔒 Windows Critical Section implementation for cross-platform synchronization
 */

#ifdef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/platform/windows_compat.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/asciichat_errno.h>

/**
 * @brief Initialize a mutex with a name
 * @param mutex Pointer to mutex structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int mutex_init(mutex_t *mutex, const char *name) {
  InitializeCriticalSectionAndSpinCount(&mutex->impl, 4000);
  mutex->name = NAMED_REGISTER(mutex, name ? name : "unnamed");
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
  EnterCriticalSection(&mutex->impl);
  return 0;
}

/**
 * @brief Try to lock a mutex without blocking - implementation function
 * @param mutex Pointer to mutex to try locking
 * @return 0 on success, EBUSY if already locked, other error code on failure
 */
int mutex_trylock_impl(mutex_t *mutex) {
  return TryEnterCriticalSection(&mutex->impl) ? 0 : 16; // EBUSY = 16
}

/**
 * @brief Unlock a mutex - implementation function
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, error code on failure
 */
int mutex_unlock_impl(mutex_t *mutex) {
  LeaveCriticalSection(&mutex->impl);
  return 0;
}

#endif // _WIN32
