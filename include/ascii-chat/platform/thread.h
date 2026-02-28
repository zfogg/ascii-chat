#pragma once

/**
 * @file platform/thread.h
 * @brief 🧵 Cross-platform thread interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides a unified thread interface that abstracts platform-specific
 * implementations (Windows threads vs POSIX pthreads).
 *
 * The interface provides:
 * - Thread creation and management
 * - Thread joining with optional timeout
 * - Thread ID operations
 * - Thread initialization state checking
 *
 * @note On Windows, uses HANDLE for thread representation.
 *       On POSIX systems, uses pthread_t.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <stdint.h>

// Type definitions MUST come before including common.h to avoid circular dependencies.
// common.h → abstraction.h → debug/lock.h → thread.h, and lock.h needs asciichat_thread_t.
#ifdef _WIN32
#include "windows_compat.h"
/** @brief Thread handle type (Windows: HANDLE) */
typedef HANDLE asciichat_thread_t;
/** @brief Thread ID type (Windows: DWORD) */
typedef DWORD thread_id_t;
/** @brief Thread-local storage key type (Windows: DWORD) */
typedef DWORD tls_key_t;
#else
#include <pthread.h>
/** @brief Thread handle type (POSIX: pthread_t) */
typedef pthread_t asciichat_thread_t;
/** @brief Thread ID type (POSIX: pthread_t) */
typedef pthread_t thread_id_t;
/** @brief Thread-local storage key type (POSIX: pthread_key_t) */
typedef pthread_key_t tls_key_t;
#endif

#include "../common.h" // For asciichat_error_t (must come AFTER type definitions)

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Thread-Local Storage Macros
// ============================================================================

/**
 * @brief Platform-specific thread-local storage declaration
 *
 * Use this macro to declare thread-local storage variables that are
 * initialized once per thread with zero/null value.
 *
 * Platform-specific behavior:
 *   - Windows: Uses __declspec(thread)
 *   - POSIX: Uses __thread
 *
 * @par Example:
 * @code{.c}
 * PLATFORM_THREAD_LOCAL bool g_in_callback = false;
 * @endcode
 *
 * @note Not compatible with dynamic TLS (ascii_tls_key_*). Use one or the other.
 * @note Static thread-local storage is allocated at program start.
 * @note Initialization is zero/null, per language spec.
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PLATFORM_THREAD_LOCAL __declspec(thread)
#else
#define PLATFORM_THREAD_LOCAL __thread
#endif

// ============================================================================
// Thread Lifecycle Management
// ============================================================================

/**
 * @brief Internal thread wrapper for automatic cleanup
 *
 * This structure is used internally by asciichat_thread_create() to wrap
 * user thread functions with automatic cleanup (mutex stack cleanup, etc).
 * Users should not interact with this directly.
 *
 * @internal
 */
typedef struct {
  void *(*user_func)(void *); // User's thread function
  void *user_arg;             // User's argument
} asciichat_thread_wrapper_t;

/**
 * @brief Internal thread wrapper function that executes user code with cleanup
 *
 * This function is called internally by pthread_create to wrap user threads
 * with automatic cleanup. It calls the user's thread function, then performs
 * cleanup operations (mutex stack cleanup) before exiting.
 *
 * @param arg Pointer to asciichat_thread_wrapper_t (allocated by thread_create)
 * @return Return value from user thread function
 *
 * @internal
 */
void *asciichat_thread_wrapper_impl(void *arg);

// ============================================================================
// Thread Functions
// ============================================================================

/**
 * @brief Create a new named thread
 * @param thread Pointer to thread handle (output parameter)
 * @param name Human-readable name for debugging (e.g., "audio_reader")
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, non-zero on error
 *
 * Creates a new thread that executes the given function with the provided argument.
 * The thread handle is stored in the thread parameter.
 * The name is registered in the debug named registry for thread identification.
 *
 * Threads created with this function automatically perform cleanup operations
 * (like mutex stack cleanup) before exiting.
 *
 * @ingroup platform
 */
int asciichat_thread_create(asciichat_thread_t *thread, const char *name, void *(*func)(void *), void *arg);

/**
 * @brief Wait for a thread to complete (blocking)
 * @param thread Thread handle to wait for
 * @param retval Pointer to store thread return value (or NULL to ignore)
 * @return 0 on success, non-zero on error
 *
 * Blocks the calling thread until the specified thread terminates.
 *
 * @ingroup platform
 */
int asciichat_thread_join(asciichat_thread_t *thread, void **retval);

/**
 * @brief Wait for a thread to complete with timeout
 * @param thread Thread handle to wait for
 * @param retval Pointer to store thread return value (or NULL to ignore)
 * @param timeout_ns Timeout in nanoseconds
 * @return 0 on success, non-zero on timeout or error
 *
 * Waits for the specified thread to terminate, with a maximum wait time.
 * Returns non-zero if the timeout expires before the thread completes.
 *
 * @ingroup platform
 */
int asciichat_thread_join_timeout(asciichat_thread_t *thread, void **retval, uint64_t timeout_ns);

/**
 * @brief Exit the current thread
 * @param retval Return value to pass to thread joiner (or NULL)
 *
 * Terminates the calling thread and optionally passes a return value
 * to any thread waiting to join.
 *
 * @ingroup platform
 */
void asciichat_thread_exit(void *retval);

/**
 * @brief Get the current thread's ID
 * @return Thread ID of the calling thread
 *
 * Returns a platform-specific thread identifier for the calling thread.
 *
 * @ingroup platform
 */
thread_id_t asciichat_thread_self(void);

/**
 * @brief Compare two thread IDs for equality
 * @param t1 First thread ID
 * @param t2 Second thread ID
 * @return Non-zero if thread IDs are equal, 0 otherwise
 *
 * @ingroup platform
 */
int asciichat_thread_equal(thread_id_t t1, thread_id_t t2);

/**
 * @brief Get the current thread's unique numeric ID
 * @return Unique thread identifier as a 64-bit unsigned integer
 *
 * Returns a unique numeric identifier for the current thread.
 * This is more portable than thread_id_t for comparisons.
 *
 * @ingroup platform
 */
uint64_t asciichat_thread_current_id(void);

/**
 * @brief Check if a thread handle has been initialized
 * @param thread Pointer to thread handle
 * @return true if thread is initialized, false otherwise
 *
 * @ingroup platform
 */
bool asciichat_thread_is_initialized(asciichat_thread_t *thread);

/**
 * @brief Initialize a thread handle to an uninitialized state
 * @param thread Pointer to thread handle
 *
 * Sets the thread handle to an uninitialized state. Useful for
 * static initialization or resetting a thread handle.
 *
 * @ingroup platform
 */
void asciichat_thread_init(asciichat_thread_t *thread);

/**
 * @brief Set the current thread to real-time priority
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Attempts to set the current thread to real-time priority for
 * time-critical operations like audio processing.
 *
 * Platform-specific implementations:
 *   - Linux: Uses pthread_setschedparam() with SCHED_FIFO at priority 80
 *   - macOS: Uses thread_policy_set() with THREAD_TIME_CONSTRAINT_POLICY
 *   - Windows: Uses SetThreadPriority() with THREAD_PRIORITY_TIME_CRITICAL
 *
 * @note On Linux, requires CAP_SYS_NICE capability or rtprio resource limit
 * @note On Windows, does not require special privileges
 * @note On macOS, requires mach_thread_self() to work
 *
 * @ingroup platform
 */
asciichat_error_t asciichat_thread_set_realtime_priority(void);

/**
 * @brief Create a thread with standardized error handling and logging
 * @param thread Thread handle to fill on success
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @param thread_name Human-readable name for logging (e.g., "video_render")
 * @param client_id Client ID for error context in logs
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM or ERROR_PLATFORM_INIT on failure
 *
 * Wraps asciichat_thread_create() with unified error handling and logging.
 * On success, logs at debug level. On failure, uses SET_ERRNO() to record
 * error context and returns:
 * - ERROR_INVALID_PARAM if parameters are invalid
 * - ERROR_PLATFORM_INIT if thread creation fails
 *
 * @note Errors are logged via SET_ERRNO(), use HAS_ERRNO() to check context
 * @note thread_name and client_id are used in log messages for debugging
 *
 * @ingroup platform
 */
asciichat_error_t thread_create_or_fail(asciichat_thread_t *thread, void *(*func)(void *), void *arg,
                                        const char *thread_name, uint32_t client_id);

/**
 * @brief Convert a thread handle to a uintptr_t registry key
 * @param thread Thread handle (asciichat_thread_t)
 * @return uintptr_t representation suitable for pointer/ID lookups
 *
 * Platform-specific conversion for use with registry systems (e.g., named object registry).
 * On POSIX, pthread_t is cast directly to uintptr_t.
 * On Windows, HANDLE is cast directly to uintptr_t.
 *
 * @ingroup platform
 */
uintptr_t asciichat_thread_to_key(asciichat_thread_t thread);

// ============================================================================
// Thread-Local Storage (TLS) Functions
// ============================================================================

/**
 * @brief Create a thread-local storage key
 * @param key Pointer to TLS key (output parameter)
 * @param destructor Optional destructor function called when thread exits (may be NULL)
 * @return 0 on success, non-zero on error
 *
 * Creates a new TLS key that can be used to store thread-specific data.
 * If a destructor is provided, it will be called with the stored value when
 * a thread terminates (if the value is non-NULL).
 *
 * @ingroup platform
 */
int ascii_tls_key_create(tls_key_t *key, void (*destructor)(void *));

/**
 * @brief Delete a thread-local storage key
 * @param key TLS key to delete
 * @return 0 on success, non-zero on error
 *
 * Deletes the specified TLS key. Does NOT call destructors for existing
 * thread-local values. The caller is responsible for cleanup before deletion.
 *
 * @ingroup platform
 */
int ascii_tls_key_delete(tls_key_t key);

/**
 * @brief Get thread-local value for a key
 * @param key TLS key
 * @return Thread-local value, or NULL if not set
 *
 * Returns the thread-local value associated with the specified key
 * for the calling thread.
 *
 * @ingroup platform
 */
void *ascii_tls_get(tls_key_t key);

/**
 * @brief Set thread-local value for a key
 * @param key TLS key
 * @param value Value to store
 * @return 0 on success, non-zero on error
 *
 * Associates the specified value with the key for the calling thread.
 *
 * @ingroup platform
 */
int ascii_tls_set(tls_key_t key, void *value);

#ifdef __cplusplus
}
#endif

/** @} */