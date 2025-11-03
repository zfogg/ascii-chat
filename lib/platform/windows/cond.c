/**
 * @file platform/windows/cond.c
 * @ingroup platform
 * @brief ⏰ Windows Condition Variable implementation for thread signaling and waiting
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../windows_compat.h"

/**
 * @brief Initialize a condition variable
 * @param cond Pointer to condition variable structure to initialize
 * @return 0 on success, error code on failure
 */
int cond_init(cond_t *cond) {
  InitializeConditionVariable(cond);
  return 0;
}

/**
 * @brief Destroy a condition variable and free its resources
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, error code on failure
 * @note Condition variables don't need explicit destruction on Windows
 */
int cond_destroy(cond_t *cond) {
  // Condition variables don't need explicit destruction on Windows
  (void)cond; // Suppress unused parameter warning
  return 0;
}

/**
 * @brief Wait on a condition variable indefinitely
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @return 0 on success, -1 on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 */
int cond_wait(cond_t *cond, mutex_t *mutex) {
  return SleepConditionVariableCS(cond, mutex, INFINITE) ? 0 : -1;
}

/**
 * @brief Wait on a condition variable with timeout
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout or failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 */
int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms) {
  return SleepConditionVariableCS(cond, mutex, timeout_ms) ? 0 : -1;
}

/**
 * @brief Signal one waiting thread on a condition variable
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, error code on failure
 */
int cond_signal(cond_t *cond) {
  WakeConditionVariable(cond);
  return 0;
}

/**
 * @brief Signal all waiting threads on a condition variable
 * @param cond Pointer to condition variable to broadcast on
 * @return 0 on success, error code on failure
 */
int cond_broadcast(cond_t *cond) {
  WakeAllConditionVariable(cond);
  return 0;
}

#endif // _WIN32
