#pragma once

/**
 * @file platform/thread.h
 * @ingroup platform
 * @brief 🧵 Cross-platform thread interface for ascii-chat
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

#ifdef _WIN32
#include "windows_compat.h"
/** @brief Thread handle type (Windows: HANDLE) */
typedef HANDLE asciithread_t;
/** @brief Thread ID type (Windows: DWORD) */
typedef DWORD thread_id_t;
/** @brief Thread-local storage key type (Windows: DWORD) */
typedef DWORD tls_key_t;
#else
#include <pthread.h>
/** @brief Thread handle type (POSIX: pthread_t) */
typedef pthread_t asciithread_t;
/** @brief Thread ID type (POSIX: pthread_t) */
typedef pthread_t thread_id_t;
/** @brief Thread-local storage key type (POSIX: pthread_key_t) */
typedef pthread_key_t tls_key_t;
#endif

// ============================================================================
// Thread Functions
// ============================================================================

/**
 * @brief Create a new thread
 * @param thread Pointer to thread handle (output parameter)
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, non-zero on error
 *
 * Creates a new thread that executes the given function with the provided argument.
 * The thread handle is stored in the thread parameter.
 *
 * @ingroup platform
 */
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg);

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
int ascii_thread_join(asciithread_t *thread, void **retval);

/**
 * @brief Wait for a thread to complete with timeout
 * @param thread Thread handle to wait for
 * @param retval Pointer to store thread return value (or NULL to ignore)
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, non-zero on timeout or error
 *
 * Waits for the specified thread to terminate, with a maximum wait time.
 * Returns non-zero if the timeout expires before the thread completes.
 *
 * @ingroup platform
 */
int ascii_thread_join_timeout(asciithread_t *thread, void **retval, uint32_t timeout_ms);

/**
 * @brief Exit the current thread
 * @param retval Return value to pass to thread joiner (or NULL)
 *
 * Terminates the calling thread and optionally passes a return value
 * to any thread waiting to join.
 *
 * @ingroup platform
 */
void ascii_thread_exit(void *retval);

/**
 * @brief Get the current thread's ID
 * @return Thread ID of the calling thread
 *
 * Returns a platform-specific thread identifier for the calling thread.
 *
 * @ingroup platform
 */
thread_id_t ascii_thread_self(void);

/**
 * @brief Compare two thread IDs for equality
 * @param t1 First thread ID
 * @param t2 Second thread ID
 * @return Non-zero if thread IDs are equal, 0 otherwise
 *
 * @ingroup platform
 */
int ascii_thread_equal(thread_id_t t1, thread_id_t t2);

/**
 * @brief Get the current thread's unique numeric ID
 * @return Unique thread identifier as a 64-bit unsigned integer
 *
 * Returns a unique numeric identifier for the current thread.
 * This is more portable than thread_id_t for comparisons.
 *
 * @ingroup platform
 */
uint64_t ascii_thread_current_id(void);

/**
 * @brief Check if a thread handle has been initialized
 * @param thread Pointer to thread handle
 * @return true if thread is initialized, false otherwise
 *
 * @ingroup platform
 */
bool ascii_thread_is_initialized(asciithread_t *thread);

/**
 * @brief Initialize a thread handle to an uninitialized state
 * @param thread Pointer to thread handle
 *
 * Sets the thread handle to an uninitialized state. Useful for
 * static initialization or resetting a thread handle.
 *
 * @ingroup platform
 */
void ascii_thread_init(asciithread_t *thread);

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
