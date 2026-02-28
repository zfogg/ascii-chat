/**
 * @file platform/posix/thread.c
 * @ingroup platform
 * @brief 🧵 POSIX pthread implementation for cross-platform thread management
 */

#ifndef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

/**
 * @name Real-time Thread Timing Constants
 * @brief macOS thread scheduling timing (in mach time units, approximately nanoseconds)
 * @{
 */
#define THREAD_COMPUTATION_TIME 5000 // ~5ms computation time
#define THREAD_CONSTRAINT_TIME 10000 // ~10ms constraint time
/** @} */

/**
 * @brief Internal thread wrapper that adds automatic cleanup before thread exit
 *
 * This function wraps user thread functions to ensure proper cleanup
 * (like mutex stack cleanup) happens before the thread terminates.
 *
 * @param arg Pointer to asciichat_thread_wrapper_t allocated by asciichat_thread_create
 * @return Return value from user thread function
 *
 * @internal
 */
void *asciichat_thread_wrapper_impl(void *arg) {
  asciichat_thread_wrapper_t *wrapper = (asciichat_thread_wrapper_t *)arg;
  if (!wrapper) {
    log_debug("[THREAD] Wrapper is NULL, exiting immediately");
    return NULL;
  }

  log_debug("[THREAD] Starting wrapped thread, wrapper at %p", (void *)wrapper);

  // Call user's thread function with user's argument
  void *result = NULL;

  if (wrapper->user_func) {
    result = wrapper->user_func(wrapper->user_arg);
  }

  log_debug("[THREAD] User function returned, cleaning up mutex stacks");

  // Perform cleanup before thread exit
  mutex_stack_cleanup_current_thread();

  // Free the wrapper struct
  log_debug("[THREAD] Freeing wrapper at %p before exit", (void *)wrapper);
  SAFE_FREE(wrapper);

  log_debug("[THREAD] Wrapper freed, thread exiting");
  return result;
}

/**
 * @brief Create a new thread with name
 * @param thread Pointer to thread structure to initialize
 * @param name Human-readable name for debugging
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, error code on failure
 */
int asciichat_thread_create(asciichat_thread_t *thread, const char *name, void *(*func)(void *), void *arg) {
  // Allocate wrapper structure to hold user's function and argument
  asciichat_thread_wrapper_t *wrapper = SAFE_MALLOC(sizeof(asciichat_thread_wrapper_t), asciichat_thread_wrapper_t *);
  if (!wrapper) {
    return -1; // Memory allocation failure
  }

  wrapper->user_func = func;
  wrapper->user_arg = arg;

  // Create thread with wrapper function, passing wrapper as argument
  int err = pthread_create(thread, NULL, asciichat_thread_wrapper_impl, (void *)wrapper);
  if (err == 0 && name && thread) {
    NAMED_REGISTER_THREAD(*thread, name);
  } else if (err != 0) {
    // If thread creation failed, free the wrapper we allocated
    SAFE_FREE(wrapper);
  }

  return err;
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
    // Unregister thread from debug naming registry before clearing handle
    NAMED_UNREGISTER_THREAD(*thread);

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

  // pthread_timedjoin_np hangs indefinitely on some systems, so use pthread_tryjoin_np
  // with polling to ensure the timeout is respected during shutdown
  int result;
  const uint64_t sleep_duration_ns = 10 * US_PER_MS_INT; // 10ms sleep per iteration

  while (true) {
    result = pthread_tryjoin_np(*thread, retval);

    if (result == 0) {
      // Thread successfully joined
      break;
    }
    if (result != EBUSY) {
      // Some other error
      break;
    }

    // Check if timeout expired
    uint64_t current_ns = time_get_realtime_ns();
    if (current_ns >= deadline_ns) {
      // Timeout reached
      result = ETIMEDOUT;
      break;
    }

    // Sleep a bit before retrying
    usleep((unsigned int)(sleep_duration_ns / 1000));
  }
  if (result == ETIMEDOUT) {
    return -2;
  }
  if (result == 0) {
    // Unregister thread from debug naming registry before clearing handle
    NAMED_UNREGISTER_THREAD(*thread);

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
    // Unregister thread from debug naming registry before clearing handle
    NAMED_UNREGISTER_THREAD(*thread);

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
  policy.computation = THREAD_COMPUTATION_TIME; // ~5ms computation time
  policy.constraint = THREAD_CONSTRAINT_TIME;   // ~10ms constraint time
  policy.preemptible = 0;                       // Not preemptible

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

uintptr_t asciichat_thread_to_key(asciichat_thread_t thread) {
  return (uintptr_t)thread;
}

#endif // !_WIN32
