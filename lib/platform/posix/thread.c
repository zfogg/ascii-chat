/**
 * @file thread.c
 * @brief POSIX thread implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides POSIX pthread wrappers for the platform abstraction layer,
 * enabling cross-platform thread management using a unified API.
 */

#ifndef _WIN32

#include "../abstraction.h"
#include <pthread.h>
#include <stdint.h>

/**
 * @brief Create a new thread
 * @param thread Pointer to thread structure to initialize
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, error code on failure
 */
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg) {
  return pthread_create(thread, NULL, func, arg);
}

/**
 * @brief Wait for a thread to complete and retrieve its return value
 * @param thread Pointer to thread to join
 * @param retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, error code on failure
 */
int ascii_thread_join(asciithread_t *thread, void **retval) {
  return pthread_join(*thread, retval);
}

/**
 * @brief Exit the current thread with a return value
 * @param retval Return value for the thread
 */
void ascii_thread_exit(void *retval) {
  pthread_exit(retval);
}

/**
 * @brief Detach a thread, allowing it to run independently
 * @param thread Pointer to thread to detach
 * @return 0 on success, error code on failure
 */
int ascii_thread_detach(asciithread_t *thread) {
  return pthread_detach(*thread);
}

/**
 * @brief Get the current thread's ID
 * @return Thread ID structure for current thread
 */
thread_id_t ascii_thread_self(void) {
  return pthread_self();
}

/**
 * @brief Compare two thread IDs for equality
 * @param t1 First thread ID
 * @param t2 Second thread ID
 * @return Non-zero if equal, 0 if different
 */
int ascii_thread_equal(thread_id_t t1, thread_id_t t2) {
  return pthread_equal(t1, t2);
}

/**
 * @brief Get current thread ID as a 64-bit integer
 * @return Current thread ID as uint64_t
 */
uint64_t ascii_thread_current_id(void) {
  return (uint64_t)pthread_self();
}

bool ascii_thread_is_initialized(asciithread_t *thread) {
  if (!thread)
    return false;
  // On POSIX, check if thread handle is non-zero
  return (*thread != 0);
}

#endif // !_WIN32