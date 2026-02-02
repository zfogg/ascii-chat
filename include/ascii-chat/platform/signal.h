#pragma once

/**
 * @file platform/signal.h
 * @brief Cross-platform crash signal and exception handling
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides unified crash handler registration across Windows and POSIX platforms.
 * Enables capturing segmentation faults, access violations, aborts, and bus errors
 * with consistent behavior across platforms.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback function type for crash handlers
 *
 * Called when a critical signal (segfault, abort, etc.) is caught.
 *
 * @param signal Signal number (SIGSEGV, SIGABRT, etc.)
 * @param context Platform-specific context information (can be NULL)
 *
 * @note Callback should not perform blocking operations or allocate memory
 * @note On some platforms, only limited operations are safe in this callback
 *
 * @ingroup platform
 */
typedef void (*platform_crash_handler_t)(int signal, void *context);

/**
 * @brief Install a crash signal handler
 *
 * Registers a callback to be invoked when critical signals are received.
 * Handles platform differences:
 *   - POSIX: sigaction() for SIGSEGV, SIGABRT, SIGBUS, SIGILL
 *   - Windows: SetUnhandledExceptionFilter() for access violations, stack overflow
 *
 * @param handler Callback function to invoke on crash, or NULL to uninstall
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Only one handler can be active at a time; installing a new handler
 *       replaces the previous one
 * @note Crashes still terminate the process after the handler runs
 * @note Signal safety: handlers should only use async-signal-safe functions
 *
 * @par Example:
 * @code{.c}
 * void my_crash_handler(int sig, void *ctx) {
 *   fprintf(stderr, "Caught signal %d\n", sig);
 *   // Perform minimal cleanup, write logs, etc.
 * }
 *
 * platform_install_crash_handler(my_crash_handler);
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_install_crash_handler(platform_crash_handler_t handler);

/**
 * @brief Uninstall the crash signal handler
 *
 * Removes any installed crash handler and restores default signal behavior.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup platform
 */
asciichat_error_t platform_uninstall_crash_handler(void);

/**
 * @brief Get human-readable name for signal number
 *
 * @param signal Signal number (SIGSEGV, SIGABRT, etc.)
 * @return Human-readable signal name (e.g., "SIGSEGV"), or "UNKNOWN_SIGNAL"
 *
 * @note Returned string is not allocated; valid for program lifetime
 *
 * @ingroup platform
 */
const char *platform_signal_name(int signal);

#ifdef __cplusplus
}
#endif

/** @} */
