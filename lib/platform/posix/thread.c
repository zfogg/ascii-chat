/**
 * @file platform/posix/thread.c
 * @ingroup platform
 * @brief 🧵 POSIX pthread implementation for cross-platform thread management
 */

#ifndef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/time.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sched.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

/**
 * @brief Create a new thread
 * @param thread Pointer to thread structure to initialize
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, error code on failure
 */
int asciichat_thread_create(asciichat_thread_t *thread, void *(*func)(void *), void *arg) {
  return pthread_create(thread, NULL, func, arg);
}

/**
 * @brief Wait for a thread to complete and retrieve its return value
 * @param thread Pointer to thread to join
 * @param retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, error code on failure
 */
int asciichat_thread_join(asciichat_thread_t *thread, void **retval) {
  int result = pthread_join(*thread, retval);
  if (result == 0) {
    // Clear the handle after successful join to match Windows behavior
    // This allows asciichat_thread_is_initialized() to correctly return false
    memset(thread, 0, sizeof(asciichat_thread_t));
  }
  return result;
}

/**
 * @brief Join a thread with timeout
 * @param thread Thread handle to join
 * @param retval Optional return value from thread
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -2 on timeout, -1 on error
 */
int asciichat_thread_join_timeout(asciichat_thread_t *thread, void **retval, uint64_t timeout_ns) {
  (void)timeout_ns; // Unused on macOS - suppress warning
// POSIX doesn't have pthread_timedjoin_np on all systems
// Use pthread_tryjoin_np with polling as fallback
#ifdef __linux__
  struct timespec timeout;
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t deadline_ns = now_ns + timeout_ns;

  // Convert nanoseconds to timespec for pthread_timedjoin_np
  time_ns_to_timespec(deadline_ns, &timeout);

  int result = pthread_timedjoin_np(*thread, retval, &timeout);
  if (result == ETIMEDOUT) {
    return -2;
  }
  if (result == 0) {
    // Clear the handle after successful join to match Windows behavior
    memset(thread, 0, sizeof(asciichat_thread_t));
  }
  return result == 0 ? 0 : -1;
#else
  // For macOS and other systems without pthread_timedjoin_np or pthread_tryjoin_np
  // We'll use a simple approach: just do a blocking pthread_join
  // This means the timeout isn't perfectly respected, but it works for compatibility

  // Note: This is a limitation on macOS - we don't have non-blocking thread join
  // In practice, threads should exit quickly in our use case
  int result = pthread_join(*thread, retval);
  if (result == 0) {
    // Clear the handle after successful join to match Windows behavior
    memset(thread, 0, sizeof(asciichat_thread_t));
  }
  return result == 0 ? 0 : -1;
#endif
}

/**
 * @brief Exit the current thread with a return value
 * @param retval Return value for the thread
 *
 * Automatically cleans up thread-local error context before exiting.
 * This prevents memory leaks from error messages allocated in thread-local storage.
 */
void asciichat_thread_exit(void *retval) {
  // Clean up thread-local error context to prevent leaks
  // Declared in asciichat_errno.h but we need to avoid circular dependency
  // so we use weak symbol or forward declaration
  extern void asciichat_clear_errno(void);
  asciichat_clear_errno();

  pthread_exit(retval);
}

/**
 * @brief Detach a thread, allowing it to run independently
 * @param thread Pointer to thread to detach
 * @return 0 on success, error code on failure
 */
int asciichat_thread_detach(asciichat_thread_t *thread) {
  return pthread_detach(*thread);
}

/**
 * @brief Get the current thread's ID
 * @return Thread ID structure for current thread
 */
thread_id_t asciichat_thread_self(void) {
  return pthread_self();
}

/**
 * @brief Compare two thread IDs for equality
 * @param t1 First thread ID
 * @param t2 Second thread ID
 * @return Non-zero if equal, 0 if different
 */
int asciichat_thread_equal(thread_id_t t1, thread_id_t t2) {
  return pthread_equal(t1, t2);
}

/**
 * @brief Get current thread ID as a 64-bit integer
 * @return Current thread ID as uint64_t
 */
uint64_t asciichat_thread_current_id(void) {
  return (uint64_t)pthread_self();
}

bool asciichat_thread_is_initialized(asciichat_thread_t *thread) {
  if (!thread)
    return false;
  // On POSIX, check if thread handle is non-zero
  return (*thread != 0);
}

void asciichat_thread_init(asciichat_thread_t *thread) {
  if (thread) {
    memset(thread, 0, sizeof(asciichat_thread_t)); // On POSIX, zero-init the pthread_t
  }
}

// ============================================================================
// Thread-Local Storage (TLS) Functions
// ============================================================================

/**
 * @brief Create a thread-local storage key
 * @param key Pointer to TLS key (output parameter)
 * @param destructor Optional destructor function called when thread exits
 * @return 0 on success, non-zero on error
 */
int ascii_tls_key_create(tls_key_t *key, void (*destructor)(void *)) {
  return pthread_key_create(key, destructor);
}

/**
 * @brief Delete a thread-local storage key
 * @param key TLS key to delete
 * @return 0 on success, non-zero on error
 */
int ascii_tls_key_delete(tls_key_t key) {
  return pthread_key_delete(key);
}

/**
 * @brief Get thread-local value for a key
 * @param key TLS key
 * @return Thread-local value, or NULL if not set
 */
void *ascii_tls_get(tls_key_t key) {
  return pthread_getspecific(key);
}

/**
 * @brief Set thread-local value for a key
 * @param key TLS key
 * @param value Value to store
 * @return 0 on success, non-zero on error
 */
int ascii_tls_set(tls_key_t key, void *value) {
  return pthread_setspecific(key, value);
}

/**
 * @brief Set the current thread to real-time priority (POSIX implementation)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Uses SCHED_FIFO with priority 80 for Linux/POSIX systems.
 * Requires CAP_SYS_NICE capability or appropriate rtprio resource limit.
 */
asciichat_error_t asciichat_thread_set_realtime_priority(void) {
#ifdef __APPLE__
  // macOS: Use thread_policy_set for real-time scheduling
  thread_time_constraint_policy_data_t policy;
  policy.period = 0;
  policy.computation = 5000; // 5ms computation time
  policy.constraint = 10000; // 10ms constraint
  policy.preemptible = 0;    // Not preemptible

  kern_return_t result = thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
                                           THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  if (result != KERN_SUCCESS) {
    return SET_ERRNO(ERROR_THREAD, "Failed to set real-time thread priority on macOS");
  }
  return ASCIICHAT_OK;

#else
  // Linux: Use SCHED_FIFO with high priority
  struct sched_param param;
  int policy = SCHED_FIFO;
  param.sched_priority = 80; // High priority (1-99 range for SCHED_FIFO)

  if (pthread_setschedparam(pthread_self(), policy, &param) != 0) {
    return SET_ERRNO_SYS(
        ERROR_THREAD,
        "Failed to set real-time thread priority (try running with elevated privileges or configuring rtprio limits)");
  }
  return ASCIICHAT_OK;
#endif
}

#endif // !_WIN32
