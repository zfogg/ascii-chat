/**
 * @file platform/posix/cond.c
 * @ingroup platform
 * @brief ⏰ POSIX pthread condition variable implementation for thread signaling and waiting
 */

#ifndef _WIN32

#include "../abstraction.h"
#include "../../util/time.h"
#include <pthread.h>
#include <time.h>

/**
 * @brief Initialize a condition variable
 * @param cond Pointer to condition variable structure to initialize
 * @return 0 on success, error code on failure
 */
int cond_init(cond_t *cond) {
  return pthread_cond_init(cond, NULL);
}

/**
 * @brief Destroy a condition variable and free its resources
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, error code on failure
 */
int cond_destroy(cond_t *cond) {
  return pthread_cond_destroy(cond);
}

/**
 * @brief Wait on a condition variable indefinitely
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @return 0 on success, error code on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 */
int cond_wait(cond_t *cond, mutex_t *mutex) {
  return pthread_cond_wait(cond, mutex);
}

/**
 * @brief Wait on a condition variable with timeout
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, ETIMEDOUT on timeout, other error code on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 */
int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms) {
  struct timespec ts;
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t timeout_ns = time_ms_to_ns((uint64_t)timeout_ms);
  uint64_t deadline_ns = now_ns + timeout_ns;
  time_ns_to_timespec(deadline_ns, &ts);
  return pthread_cond_timedwait(cond, mutex, &ts);
}

/**
 * @brief Signal one waiting thread on a condition variable
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, error code on failure
 */
int cond_signal(cond_t *cond) {
  return pthread_cond_signal(cond);
}

/**
 * @brief Signal all waiting threads on a condition variable
 * @param cond Pointer to condition variable to broadcast on
 * @return 0 on success, error code on failure
 */
int cond_broadcast(cond_t *cond) {
  return pthread_cond_broadcast(cond);
}

#endif // !_WIN32
