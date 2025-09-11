/**
 * @file cond.c
 * @brief POSIX condition variable implementation for ASCII-Chat platform abstraction layer
 * 
 * This file provides POSIX pthread condition variable wrappers for the platform abstraction layer,
 * enabling cross-platform thread synchronization using a unified API.
 */

#ifndef _WIN32

#include "../abstraction.h"
#include <pthread.h>
#include <time.h>

/**
 * @brief Initialize a condition variable
 * @param cond Pointer to condition variable structure to initialize
 * @return 0 on success, error code on failure
 */
int cond_init(cond_t *cond) {
    return pthread_cond_init(&cond->cond, NULL);
}

/**
 * @brief Destroy a condition variable and free its resources
 * @param cond Pointer to condition variable to destroy
 * @return 0 on success, error code on failure
 */
int cond_destroy(cond_t *cond) {
    return pthread_cond_destroy(&cond->cond);
}

/**
 * @brief Wait on a condition variable indefinitely
 * @param cond Pointer to condition variable to wait on
 * @param mutex Pointer to associated mutex (must be locked by caller)
 * @return 0 on success, error code on failure
 * @note The mutex is automatically released while waiting and reacquired before returning
 */
int cond_wait(cond_t *cond, mutex_t *mutex) {
    return pthread_cond_wait(&cond->cond, &mutex->mutex);
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
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
}

/**
 * @brief Signal one waiting thread on a condition variable
 * @param cond Pointer to condition variable to signal
 * @return 0 on success, error code on failure
 */
int cond_signal(cond_t *cond) {
    return pthread_cond_signal(&cond->cond);
}

/**
 * @brief Signal all waiting threads on a condition variable
 * @param cond Pointer to condition variable to broadcast on
 * @return 0 on success, error code on failure
 */
int cond_broadcast(cond_t *cond) {
    return pthread_cond_broadcast(&cond->cond);
}

#endif // !_WIN32