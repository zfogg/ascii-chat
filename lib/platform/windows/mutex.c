/**
 * @file platform/windows/mutex.c
 * @ingroup platform
 * @brief 🔒 Windows Critical Section implementation for cross-platform synchronization
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../windows_compat.h"
#include "asciichat_errno.h"

/**
 * @brief Initialize a mutex
 * @param mutex Pointer to mutex structure to initialize
 * @return 0 on success, error code on failure
 */
int mutex_init(mutex_t *mutex) {
  InitializeCriticalSectionAndSpinCount(mutex, 4000);
  return 0;
}

/**
 * @brief Destroy a mutex and free its resources
 * @param mutex Pointer to mutex to destroy
 * @return 0 on success, error code on failure
 */
int mutex_destroy(mutex_t *mutex) {
  DeleteCriticalSection(mutex);
  return 0;
}

/**
 * @brief Lock a mutex (blocking) - implementation function
 * @param mutex Pointer to mutex to lock
 * @return 0 on success, error code on failure
 */
int mutex_lock_impl(mutex_t *mutex) {
  EnterCriticalSection(mutex);
  return 0;
}

/**
 * @brief Try to lock a mutex without blocking
 * @param mutex Pointer to mutex to try locking
 * @return 0 on success, EBUSY if already locked, other error code on failure
 */
int mutex_trylock(mutex_t *mutex) {
  return TryEnterCriticalSection(mutex) ? 0 : 16; // EBUSY = 16
}

/**
 * @brief Unlock a mutex - implementation function
 * @param mutex Pointer to mutex to unlock
 * @return 0 on success, error code on failure
 */
int mutex_unlock_impl(mutex_t *mutex) {
  LeaveCriticalSection(mutex);
  return 0;
}

#endif // _WIN32
