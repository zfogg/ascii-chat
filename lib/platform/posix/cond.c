/**
 * @file platform/posix/cond.c
 * @ingroup platform
 * @brief ⏰ POSIX pthread condition variable implementation for thread signaling and waiting
 */

#ifndef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
#include <pthread.h>
#include <time.h>
#include <ascii-chat/atomic.h> // For atomic_fetch_sub
#include <errno.h>     // For ETIMEDOUT

/**
 * @brief Initialize a condition variable with a name
 * @param cond Pointer to condition variable structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int cond_init(cond_t *cond, const char *name) {
  int err = pthread_cond_init(&cond->impl, NULL);
  if (err == 0) {
    cond->name = NAMED_REGISTER_COND(cond, name);
    cond->last_signal_time_ns = 0;
    cond->last_broadcast_time_ns = 0;
    cond->last_wait_time_ns = 0;
    cond->waiting_count = (atomic_t){0};
    cond->last_waiting_key = 0;
  }
  return err;
}

/**
 * @brief Destroy a condition variable and free its resources
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, error code on failure
 */
int cond_destroy(cond_t *cond) {
  NAMED_UNREGISTER(cond);
  return pthread_cond_destroy(&cond->impl);
}

/**
 * @brief Wait on a condition variable indefinitely - implementation function
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @return 0 on success, error code on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 * @note This is the raw implementation - use cond_wait macro for debug tracking
 */
int cond_wait_impl(cond_t *cond, mutex_t *mutex) {
  // pthread_cond_wait atomically releases mutex before waiting, then re-acquires it
  // DO NOT manually track unlock/lock - pthread_cond_wait is atomic and doesn't call our mutex functions
  // The mutex lock tracking continues across the wait - the mutex is still "held" from the library's perspective
  int result = pthread_cond_wait(&cond->impl, &mutex->impl);
  return result;
}

/**
 * @brief Wait on a condition variable with timeout - implementation function
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @param timeout_ns Timeout in nanoseconds
 * @return 0 on success, ETIMEDOUT on timeout, other error code on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 * @note This is the raw implementation - use cond_timedwait macro for debug tracking
 */
int cond_timedwait_impl(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns) {
  struct timespec ts;
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t deadline_ns = now_ns + timeout_ns;
  time_ns_to_timespec(deadline_ns, &ts);
  // pthread_cond_timedwait atomically releases mutex before waiting, then re-acquires it
  // DO NOT manually track unlock/lock - pthread_cond_timedwait is atomic and doesn't call our mutex functions
  // The mutex lock tracking continues across the wait - the mutex is still "held" from the library's perspective
  int result = pthread_cond_timedwait(&cond->impl, &mutex->impl, &ts);

  // If we timed out (not signaled), decrement waiting_count
  // cond_on_signal() is called by another thread if we were actually signaled
  if (result == ETIMEDOUT && cond && atomic_load_u64(&cond->waiting_count) > 0) {
    atomic_fetch_sub_u64(&cond->waiting_count, 1);
  }

  return result;
}

/**
 * @brief Signal one waiting thread on a condition variable
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, error code on failure
 */
int cond_signal(cond_t *cond) {
  cond_on_signal(cond);
  return pthread_cond_signal(&cond->impl);
}

/**
 * @brief Signal all waiting threads on a condition variable
 * @param cond Pointer to condition variable to broadcast on
 * @return 0 on success, error code on failure
 */
int cond_broadcast(cond_t *cond) {
  cond_on_broadcast(cond);
  return pthread_cond_broadcast(&cond->impl);
}

#endif // !_WIN32
