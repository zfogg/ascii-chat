/**
 * @file thread_helpers.h
 * @ingroup thread_helpers
 * @brief 🧵 Thread creation and management helper functions
 *
 * Provides reusable utilities for thread creation with standardized
 * error handling and logging.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include "platform/abstraction.h"

/**
 * @defgroup thread_helpers Thread Management Helpers
 * @ingroup module_core
 * @brief Utilities for thread creation and management
 * @{
 */

/**
 * @brief Create a thread with standard error handling and logging
 *
 * Wraps ascii_thread_create() with unified error handling and logging.
 * On success, logs the thread creation. On failure, logs an error and
 * returns the error code.
 *
 * @param[out] thread Thread handle to fill on success
 * @param[in] func Thread function to execute
 * @param[in] arg Argument to pass to thread function
 * @param[in] thread_name Human-readable name for logging (e.g., "video_render")
 *
 * @return 0 on success, non-zero error code on failure
 *
 * @par Example:
 * @code{.c}
 * ascii_thread_t render_thread;
 * if (thread_create_or_fail(&render_thread, video_render_func, client, "video_render") != 0) {
 *   log_error("Failed to create render thread");
 *   return ERROR_PLATFORM_INIT;
 * }
 * @endcode
 *
 * @note This function logs errors internally, caller should handle the return code
 * @see ascii_thread_create() For low-level thread creation
 */
int thread_create_or_fail(ascii_thread_t *thread, void *(*func)(void *), void *arg, const char *thread_name);

/** @} */
