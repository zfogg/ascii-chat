/**
 * @file util/thread.h
 * @brief 🧵 Thread lifecycle management helpers
 * @ingroup util
 *
 * Provides macros and utilities for managing thread creation, initialization,
 * and cleanup patterns. Reduces code duplication in modules that manage
 * dedicated worker threads.
 *
 * Common Pattern:
 * Many modules in ascii-chat create dedicated threads with a similar lifecycle:
 * 1. Initialize a static thread handle
 * 2. Create the thread on demand
 * 3. Track creation status with a boolean flag
 * 4. Join the thread during cleanup
 * 5. Clear the handle to prevent accidental reuse
 *
 * Usage:
 * @code
 * // In module header
 * typedef struct {
 *     asciichat_thread_t thread_handle;
 *     bool created;
 * } my_worker_t;
 *
 * // In module implementation
 * static my_worker_t g_worker = {0};
 *
 * void start_worker(void) {
 *     if (THREAD_IS_CREATED(g_worker.created)) {
 *         return;  // Already created
 *     }
 *
 *     if (THREAD_CREATE_SAFE(g_worker.thread_handle, worker_func, NULL) != 0) {
 *         log_error("Failed to create worker thread");
 *         return;
 *     }
 *     g_worker.created = true;
 * }
 *
 * void stop_worker(void) {
 *     if (THREAD_IS_CREATED(g_worker.created)) {
 *         THREAD_JOIN_SAFE(g_worker.thread_handle);
 *         THREAD_CLEAR_HANDLE(g_worker.thread_handle);
 *         g_worker.created = false;
 *     }
 * }
 * @endcode
 */

#pragma once

#include <string.h>
#include <ascii-chat/platform/abstraction.h>

/**
 * Check if a thread has been created.
 *
 * @param created_flag Boolean flag indicating thread creation status
 * @return true if thread has been created, false otherwise
 */
#define THREAD_IS_CREATED(created_flag) ((created_flag) == true)

/**
 * Safely create a thread with error handling and logging.
 * Does NOT set the created flag - caller must do that.
 *
 * @param thread_var Thread handle (asciichat_thread_t)
 * @param func Thread function pointer (void *(*)(void *))
 * @param arg Thread argument (void *)
 * @return Return value from asciichat_thread_create (0 on success)
 *
 * This is a direct call to asciichat_thread_create with no error handling.
 * Caller should check return value and log if needed.
 */
#define THREAD_CREATE_SAFE(thread_var, func, arg) asciichat_thread_create(&(thread_var), (func), (arg))

/**
 * Create a thread with automatic error handling and logging (void return version).
 *
 * @param thread_var Thread handle (asciichat_thread_t)
 * @param func Thread function pointer
 * @param arg Thread argument
 * @param error_msg Message to log on failure
 *
 * On failure, logs an error and the macro exits the enclosing function with return.
 * Useful for mandatory thread creation where failure is fatal.
 *
 * Note: Use THREAD_CREATE_OR_RETURN from common.h for int-returning functions.
 * This version is for void-returning functions with custom error messages.
 *
 * Usage:
 * @code
 * THREAD_CREATE_OR_RETURN_VOID(g_worker_thread, worker_func, NULL, "Failed to create worker");
 * @endcode
 */
#define THREAD_CREATE_OR_RETURN_VOID(thread_var, func, arg, error_msg)                                                 \
  do {                                                                                                                 \
    if (asciichat_thread_create(&(thread_var), (func), (arg)) != 0) {                                                  \
      log_error("%s", (error_msg));                                                                                    \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Join a thread and wait for it to complete.
 * Returns the thread's exit code in the output parameter.
 *
 * @param thread_var Thread handle (asciichat_thread_t)
 * @param exit_code Pointer to store thread exit code (void **), or NULL to ignore
 *
 * Usage:
 * @code
 * void *exit_code = NULL;
 * THREAD_JOIN_SAFE(g_worker_thread, &exit_code);
 * log_debug("Worker thread exited with code %p", exit_code);
 * @endcode
 */
#define THREAD_JOIN_SAFE(thread_var, exit_code) asciichat_thread_join(&(thread_var), (exit_code))

/**
 * Join a thread without capturing its exit code.
 * Simpler version when exit code is not needed.
 *
 * @param thread_var Thread handle (asciichat_thread_t)
 *
 * Usage:
 * @code
 * THREAD_JOIN(g_worker_thread);
 * @endcode
 */
#define THREAD_JOIN(thread_var) asciichat_thread_join(&(thread_var), NULL)

/**
 * Initialize a thread handle to an invalid/uninitialized state.
 * Call this before creating the thread to ensure clean state.
 *
 * @param thread_var Thread handle (asciichat_thread_t)
 *
 * Usage:
 * @code
 * static asciichat_thread_t g_worker = THREAD_INVALID;
 * @endcode
 */
#define THREAD_CLEAR_HANDLE(thread_var) memset(&(thread_var), 0, sizeof(thread_var))
