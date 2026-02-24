/**
 * @file platform/windows/cond.c
 * @ingroup platform
 * @brief ⏰ Windows Condition Variable implementation for thread signaling and waiting
 */

#ifdef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/platform/windows_compat.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
#include <errno.h> // For ETIMEDOUT
#include <stdatomic.h> // For atomic_fetch_sub

/**
 * @brief Initialize a condition variable with a name
 * @param cond Pointer to condition variable structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int cond_init(cond_t *cond, const char *name) {
  InitializeConditionVariable(&cond->impl);
  cond->name = NAMED_REGISTER(cond, name, "cond");
  cond->last_signal_time_ns = 0;
  cond->last_broadcast_time_ns = 0;
  cond->last_wait_time_ns = 0;
  cond->waiting_count = 0;
  cond->last_waiting_key = 0;
  return 0;
}

/**
 * @brief Destroy a condition variable and free its resources
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, error code on failure
 * @note Condition variables don't need explicit destruction on Windows
 */
int cond_destroy(cond_t *cond) {
  NAMED_UNREGISTER(cond);
  // Condition variables don't need explicit destruction on Windows
  (void)cond; // Suppress unused parameter warning
  return 0;
}

/**
 * @brief Wait on a condition variable indefinitely - implementation function
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @return 0 on success, -1 on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 * @note This is the raw implementation - use cond_wait macro for debug tracking
 */
int cond_wait_impl(cond_t *cond, mutex_t *mutex) {
  // SleepConditionVariableCS atomically releases mutex before waiting, then re-acquires it
  // Track the release that SleepConditionVariableCS performs
  mutex_on_unlock(mutex);
  BOOL result = SleepConditionVariableCS(&cond->impl, &mutex->impl, INFINITE);
  // Track the re-acquisition that SleepConditionVariableCS performs after signal
  mutex_on_lock(mutex);
  return result ? 0 : -1;
}

/**
 * @brief Wait on a condition variable with timeout - implementation function
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @param timeout_ns Timeout in nanoseconds
 * @return 0 on success, ETIMEDOUT on timeout, -1 on other failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 * @note Returns ETIMEDOUT on timeout for POSIX compatibility
 * @note This is the raw implementation - use cond_timedwait macro for debug tracking
 */
int cond_timedwait_impl(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns) {
  DWORD timeout_ms = (DWORD)time_ns_to_ms(timeout_ns);
  // SleepConditionVariableCS atomically releases mutex before waiting, then re-acquires it
  // Track the release that SleepConditionVariableCS performs
  mutex_on_unlock(mutex);
  BOOL result = SleepConditionVariableCS(&cond->impl, &mutex->impl, timeout_ms);
  // Track the re-acquisition that SleepConditionVariableCS performs (whether signaled or timed out)
  mutex_on_lock(mutex);

  if (!result) {
    DWORD err = GetLastError();
    if (err == ERROR_TIMEOUT) {
      // If we timed out (not signaled), decrement waiting_count
      if (cond && cond->waiting_count > 0) {
        atomic_fetch_sub((volatile _Atomic(uint64_t) *)&cond->waiting_count, 1);
      }
      return ETIMEDOUT; // Match POSIX pthread_cond_timedwait behavior
    }
    return -1; // Other error
  }
  return 0;
}

/**
 * @brief Signal one waiting thread on a condition variable
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, error code on failure
 */
int cond_signal(cond_t *cond) {
  cond_on_signal(cond);
  WakeConditionVariable(&cond->impl);
  return 0;
}

/**
 * @brief Signal all waiting threads on a condition variable
 * @param cond Pointer to condition variable to broadcast on
 * @return 0 on success, error code on failure
 */
int cond_broadcast(cond_t *cond) {
  cond_on_broadcast(cond);
  WakeAllConditionVariable(&cond->impl);
  return 0;
}

#endif // _WIN32
