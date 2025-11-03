#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Platform-specific includes
#ifndef _WIN32
#include <unistd.h>
#endif

// Platform-specific includes - must be first
#include "common.h"

/**
 * @file asciichat_errno.h
 * @ingroup module_development
 *
 * This header provides a comprehensive thread-local error number system that
 * captures full error context including location, stack traces, and system
 * errors. The system integrates with ASCII-Chat's error handling to provide
 * detailed debugging information.
 *
 * CORE FEATURES:
 * ==============
 * - Thread-local error storage (each thread has independent error state)
 * - Full error context capture (file, line, function, stack trace)
 * - System error integration (errno, WSA errors)
 * - Automatic error logging with context
 * - Debug build stack trace capture
 * - Error statistics tracking
 *
 * ERROR CONTEXT:
 * =============
 * The system captures:
 * - Error code (asciichat_error_t)
 * - File, line, function where error occurred
 * - Stack trace at error site (debug builds)
 * - Optional custom context message
 * - Timestamp of error
 * - System error context (errno, WSA errors)
 *
 * LIBRARY MACROS:
 * ===============
 * Use SET_ERRNO and SET_ERRNO_SYS in lib/ code:
 * - Automatically captures file/line/function (debug builds)
 * - Logs error with context message
 * - Sets thread-local error state
 * - Integrates with system error codes
 *
 * APPLICATION MACROS:
 * ===================
 * Use HAS_ERRNO, GET_ERRNO, CLEAR_ERRNO in src/ code:
 * - Check for library errors
 * - Retrieve error context
 * - Clear error state
 * - Integrate with FATAL() macro
 *
 * THREAD SAFETY:
 * ==============
 * - Thread-local storage ensures thread safety
 * - Each thread has independent error state
 * - No synchronization overhead
 * - Thread-specific error statistics
 *
 * DEBUG FEATURES:
 * ===============
 * In debug builds, the system:
 * - Captures stack traces at error sites
 * - Includes file/line/function in all errors
 * - Provides ASSERT_NO_ERRNO() for validation
 * - Enables error context printing
 *
 * @note Use SET_ERRNO() in library code to set errors.
 * @note Use HAS_ERRNO() in application code to check errors.
 * @note Stack traces are only captured in debug builds.
 * @note Thread-local storage ensures thread safety automatically.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

/* ============================================================================
 * ASCII-Chat Error Number System
 * ============================================================================
 * Thread-local error context that captures:
 * - Error code (asciichat_error_t)
 * - File, line, function where error occurred
 * - Stack trace at error site (debug builds)
 * - Optional custom context message
 * - Timestamp of error
 * - System error context (errno)
 */

/**
 * @brief Error context structure
 *
 * Contains full error context information including error code, location,
 * stack trace (debug builds), and optional context message. This structure
 * is stored in thread-local storage for thread-safe error handling.
 *
 * @note Stack traces are only captured in debug builds (DEBUG or ENABLE_ERRNO_BACKTRACES).
 * @note Context message is dynamically allocated and must be freed by the system.
 * @note Thread-local storage ensures each thread has independent error state.
 *
 * @ingroup module_core
 */
typedef struct {
  asciichat_error_t code;   ///< Error code (asciichat_error_t enum value)
  const char *file;         ///< Source file where error occurred (NULL in release builds)
  int line;                 ///< Line number where error occurred (0 in release builds)
  const char *function;     ///< Function name where error occurred (NULL in release builds)
  char *context_message;    ///< Optional custom message (dynamically allocated, owned by system)
  uint64_t timestamp;       ///< Timestamp when error occurred (microseconds since epoch)
  int system_errno;         ///< System errno value (if applicable, 0 otherwise)
  int wsa_error;            ///< Windows socket error code (if applicable, 0 otherwise)
  void *backtrace[32];      ///< Stack trace addresses (debug builds only)
  char **backtrace_symbols; ///< Stack trace symbol strings (debug builds only)
  int stack_depth;          ///< Number of stack frames captured (0 if not captured)
  bool has_system_error;    ///< True if system_errno is valid
  bool has_wsa_error;       ///< True if wsa_error is valid
} asciichat_error_context_t;

/* ============================================================================
 * Thread-Local Error Storage
 * ============================================================================
 */

/**
 * @brief Thread-local error context storage
 *
 * Each thread has its own independent error context. This ensures thread-safe
 * error handling without synchronization overhead.
 *
 * @note Use HAS_ERRNO() macro to check for errors and get context.
 * @note Context is automatically updated when errors are set via SET_ERRNO().
 *
 * @ingroup module_core
 */
extern __thread asciichat_error_context_t asciichat_errno_context;

/**
 * @brief Thread-local current error code
 *
 * Current error code for the calling thread. Set to ASCIICHAT_OK when no error.
 * Updated automatically when errors are set via SET_ERRNO() macros.
 *
 * @note Use GET_ERRNO() macro to read the current error code.
 * @note Use CLEAR_ERRNO() macro to clear the error state.
 *
 * @ingroup module_core
 */
extern __thread asciichat_error_t asciichat_errno;

/* ============================================================================
 * Library Error Setting Macros
 * ============================================================================
 * Use these in lib/ code to set error context
 */

/**
 * @brief Set error code with custom context message and log it
 * @param code Error code to set
 * @param context_msg Custom message explaining the error
 *
 * Usage in lib/ code:
 *   if (bind(sockfd, ...) < 0) {
 *       SET_ERRNO(ERROR_NETWORK_BIND, "Cannot bind to port %d", port);
 *       return ERROR_NETWORK_BIND;
 *   }
 */
#ifdef NDEBUG
#define SET_ERRNO(code, context_msg, ...)                                                                              \
  ({                                                                                                                   \
    asciichat_set_errno_with_message(code, NULL, 0, NULL, context_msg, ##__VA_ARGS__);                                 \
    log_error("SET_ERRNO: " context_msg " (code: %d, meaning: %s)", ##__VA_ARGS__, code,                               \
              asciichat_error_string(code));                                                                           \
    (code);                                                                                                            \
  })
#else
#define SET_ERRNO(code, context_msg, ...)                                                                              \
  ({                                                                                                                   \
    asciichat_set_errno_with_message(code, __FILE__, __LINE__, __func__, context_msg, ##__VA_ARGS__);                  \
    log_error("SET_ERRNO: " context_msg " (code: %d, meaning: %s)", ##__VA_ARGS__, code,                               \
              asciichat_error_string(code));                                                                           \
    (code);                                                                                                            \
  })
#endif

/**
 * @brief Set error code with custom message and system error context
 * @param code Error code to set
 * @param context_msg Custom message explaining the error
 *
 * Usage in lib/ code:
 *   if (open(file, O_RDONLY) < 0) {
 *       SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open config file: %s", path);
 *       return ERROR_CONFIG;
 *   }
 */
#ifdef NDEBUG
#define SET_ERRNO_SYS(code, context_msg, ...)                                                                          \
  ({                                                                                                                   \
    int captured_errno = platform_get_last_error();                                                                    \
    asciichat_set_errno_with_system_error_and_message(code, NULL, 0, NULL, captured_errno, context_msg,                \
                                                      ##__VA_ARGS__);                                                  \
    log_error("SETERRNO_SYS: " context_msg " (code: %d - %s, system error: %d - %s)", ##__VA_ARGS__, code,             \
              asciichat_error_string(code), captured_errno, platform_strerror(captured_errno));                        \
    (code);                                                                                                            \
  })
#else
#define SET_ERRNO_SYS(code, context_msg, ...)                                                                          \
  ({                                                                                                                   \
    int captured_errno = platform_get_last_error();                                                                    \
    asciichat_set_errno_with_system_error_and_message(code, __FILE__, __LINE__, __func__, captured_errno, context_msg, \
                                                      ##__VA_ARGS__);                                                  \
    log_error("SETERRNO_SYS: " context_msg " (code: %d - %s, system error: %d - %s)", ##__VA_ARGS__, code,             \
              asciichat_error_string(code), captured_errno, platform_strerror(captured_errno));                        \
    (code);                                                                                                            \
  })
#endif

/* ============================================================================
 * Error Logging Integration Macros
 * ============================================================================
 * These macros combine error setting with automatic logging
 */

/* ============================================================================
 * Application Error Checking Macros
 * ============================================================================
 * Use these in src/ code to check and handle errors
 */

/**
 * @brief Check if an error occurred and get full context
 * @param var Variable to store error context
 *
 * Usage in src/ code:
 *   asciichat_error_context_t err_ctx;
 *   if (HAS_ERRNO(&err_ctx)) {
 *       FATAL(err_ctx.code, "Library error occurred");
 *   }
 */
#define HAS_ERRNO(var) asciichat_has_errno(var)

/**
 * @brief Clear the current error state
 */
#define CLEAR_ERRNO() asciichat_clear_errno()

/**
 * @brief Get current error code (0 if no error)
 */
#define GET_ERRNO() asciichat_get_errno()

/* ============================================================================
 * Implementation Functions
 * @{
 */

/**
 * @brief Set error code with basic context
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param context_message Custom context message (can be NULL, will be copied)
 *
 * Sets the thread-local error code with basic context information. This is
 * the low-level function used by SET_ERRNO() macros.
 *
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Context message is copied if provided (caller can free original).
 * @note Thread-safe: Uses thread-local storage (no synchronization needed).
 *
 * @warning Use SET_ERRNO() macros instead of calling this directly.
 *
 * @ingroup module_core
 */
void asciichat_set_errno(asciichat_error_t code, const char *file, int line, const char *function,
                         const char *context_message);

/**
 * @brief Set error code with formatted message
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param format Format string (printf-style)
 * @param ... Format arguments
 *
 * Sets the thread-local error code with a formatted context message. This is
 * the low-level function used by SET_ERRNO() macros.
 *
 * @note Message is formatted using vsnprintf and stored in error context.
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use SET_ERRNO() macros instead of calling this directly.
 *
 * @ingroup module_core
 */
void asciichat_set_errno_with_message(asciichat_error_t code, const char *file, int line, const char *function,
                                      const char *format, ...);

/**
 * @brief Set error code with system error (errno)
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param sys_errno System errno value
 *
 * Sets the thread-local error code with system error context. Captures the
 * system errno value for detailed error reporting.
 *
 * @note System errno is stored in error context for later retrieval.
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use SET_ERRNO_SYS() macro instead of calling this directly.
 *
 * @ingroup module_core
 */
void asciichat_set_errno_with_system_error(asciichat_error_t code, const char *file, int line, const char *function,
                                           int sys_errno);

/**
 * @brief Set error code with system error and formatted message
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param sys_errno System errno value
 * @param format Format string (printf-style)
 * @param ... Format arguments
 *
 * Sets the thread-local error code with both system error context and a
 * formatted message. This is the low-level function used by SET_ERRNO_SYS().
 *
 * @note Combines system error capture with formatted message.
 * @note In debug builds, automatically captures stack trace if enabled.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use SET_ERRNO_SYS() macro instead of calling this directly.
 *
 * @ingroup module_core
 */
void asciichat_set_errno_with_system_error_and_message(asciichat_error_t code, const char *file, int line,
                                                       const char *function, int sys_errno, const char *format, ...);

/**
 * @brief Set error code with Windows socket error (WSA error)
 * @param code Error code to set (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param wsa_error Windows socket error code
 *
 * Sets the thread-local error code with Windows-specific socket error context.
 * Used for Windows socket operations that fail with WSA error codes.
 *
 * @note WSA error is stored in error context for later retrieval.
 * @note This function is only useful on Windows platforms.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @ingroup module_core
 */
void asciichat_set_errno_with_wsa_error(asciichat_error_t code, const char *file, int line, const char *function,
                                        int wsa_error);

/**
 * @brief Check if error occurred and get full context
 * @param context Output structure for error context (must not be NULL)
 * @return true if error occurred, false if no error
 *
 * Checks if an error has occurred and copies the full error context into
 * the provided structure. This is the low-level function used by HAS_ERRNO().
 *
 * @note Returns false if no error has occurred (asciichat_errno == ASCIICHAT_OK).
 * @note If error occurred, context structure is filled with all error information.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use HAS_ERRNO() macro instead of calling this directly.
 *
 * @ingroup module_core
 */
bool asciichat_has_errno(asciichat_error_context_t *context);

/**
 * @brief Check if current error has WSA error code
 * @return true if error has WSA error code, false otherwise
 *
 * Checks whether the current thread-local error context includes a Windows
 * socket error (WSA error) code. Useful for platform-specific error handling.
 *
 * @note Only meaningful on Windows platforms.
 * @note Returns false if no error occurred or error doesn't have WSA code.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @ingroup module_core
 */
bool asciichat_has_wsa_error(void);

/**
 * @brief Clear the current error state
 *
 * Clears the thread-local error state, resetting error code to ASCIICHAT_OK
 * and freeing any associated context message. This is the low-level function
 * used by CLEAR_ERRNO() macro.
 *
 * @note Error context structure is reset to default state.
 * @note Any context message is freed automatically.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning Use CLEAR_ERRNO() macro instead of calling this directly.
 *
 * @ingroup module_core
 */
void asciichat_clear_errno(void);

/**
 * @brief Get current error code
 * @return Current error code (ASCIICHAT_OK if no error)
 *
 * Returns the current thread-local error code. Returns ASCIICHAT_OK if no
 * error has occurred. This is the low-level function used by GET_ERRNO().
 *
 * @note Thread-safe: Uses thread-local storage.
 * @note For full error context, use HAS_ERRNO() instead.
 *
 * @warning Use GET_ERRNO() macro instead of calling this directly.
 *
 * @ingroup module_core
 */
asciichat_error_t asciichat_get_errno(void);

/**
 * @brief Exit with error code and context (used by FATAL macro)
 * @param code Error code (asciichat_error_t)
 * @param file Source file name (can be NULL)
 * @param line Line number (0 if not provided)
 * @param function Function name (can be NULL)
 * @param format Format string (printf-style)
 * @param ... Format arguments
 *
 * Low-level function used by FATAL() macro. Sets error context and exits
 * the program with the specified error code. In debug builds, prints stack
 * trace before exiting.
 *
 * @note This function never returns (terminates program).
 * @note In debug builds, prints full error context and stack trace.
 * @note Thread-safe: Uses thread-local storage.
 *
 * @warning This function terminates the program. Use FATAL() macro instead
 *          of calling this directly.
 *
 * @ingroup module_core
 */
void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...);

/**
 * @brief Print full error context to stderr
 * @param context Error context to print (must not be NULL)
 *
 * Prints comprehensive error information including error code, location,
 * context message, system errors, and stack trace (if available) to stderr.
 * Useful for debugging and error reporting.
 *
 * @note Stack trace is only printed in debug builds.
 * @note System errors (errno, WSA) are included if present.
 * @note Timestamp and location information are included.
 *
 * @ingroup module_core
 */
void asciichat_print_error_context(const asciichat_error_context_t *context);

/** @} */

/* ============================================================================
 * Debug/Development Utilities
 * ============================================================================
 */

#if defined(DEBUG) || defined(ENABLE_ERRNO_BACKTRACES)
/**
 * @brief Print full error context including stack trace
 * @param context Error context to print
 */
#define PRINT_ERRNO_CONTEXT(context) asciichat_print_error_context(context)

/**
 * @brief Assert that no error occurred
 */
#define ASSERT_NO_ERRNO()                                                                                              \
  do {                                                                                                                 \
    asciichat_error_t err = asciichat_get_errno();                                                                     \
    if (err != ASCIICHAT_OK) {                                                                                         \
      asciichat_error_context_t ctx;                                                                                   \
      asciichat_has_errno(&ctx);                                                                                       \
      asciichat_print_error_context(&ctx);                                                                             \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Print current error context if any error exists
 */
#define PRINT_ERRNO_IF_ERROR()                                                                                         \
  do {                                                                                                                 \
    asciichat_error_context_t ctx;                                                                                     \
    if (HAS_ERRNO(&ctx)) {                                                                                             \
      asciichat_print_error_context(&ctx);                                                                             \
    }                                                                                                                  \
  } while (0)

#else
// No-op versions for Release builds
#define PRINT_ERRNO_CONTEXT(context) ((void)(context))
#define ASSERT_NO_ERRNO() ((void)0)
#define PRINT_ERRNO_IF_ERROR() ((void)0)
#endif

/* ============================================================================
 * Error Statistics and Monitoring
 * @{
 */

/**
 * @brief Error statistics structure
 *
 * Tracks cumulative error statistics across all threads for monitoring
 * and analysis. Statistics include per-error-code counts and aggregate
 * information.
 *
 * @note Statistics are aggregated across all threads.
 * @note Counts are cumulative since initialization.
 *
 * @ingroup module_core
 */
typedef struct {
  /** @brief Count for each error code (256 possible error codes) */
  uint64_t error_counts[256];
  /** @brief Total number of errors recorded (sum of all error_counts) */
  uint64_t total_errors;
  /** @brief Timestamp of last error (microseconds since epoch) */
  uint64_t last_error_time;
  /** @brief Error code of last error recorded */
  asciichat_error_t last_error_code;
} asciichat_error_stats_t;

/**
 * @brief Initialize error statistics system
 *
 * Initializes the error statistics tracking system. Must be called before
 * recording any error statistics. Statistics are initialized to zero.
 *
 * @note Idempotent: Safe to call multiple times (no-op after first call).
 * @note Thread-safe: Can be called from any thread.
 *
 * @ingroup module_core
 */
void asciichat_error_stats_init(void);

/**
 * @brief Record an error in statistics
 * @param code Error code to record (asciichat_error_t)
 *
 * Records an error occurrence in the statistics system. Updates per-error-code
 * count, total error count, and last error information.
 *
 * @note Thread-safe: Can be called from multiple threads simultaneously.
 * @note Statistics counters are updated atomically.
 *
 * @ingroup module_core
 */
void asciichat_error_stats_record(asciichat_error_t code);

/**
 * @brief Print error statistics to stderr
 *
 * Prints comprehensive error statistics including per-error-code counts,
 * total errors, and last error information. Useful for periodic monitoring
 * and debugging.
 *
 * @note Statistics are formatted and printed at INFO level.
 * @note Requires statistics system to be initialized.
 *
 * @ingroup module_core
 */
void asciichat_error_stats_print(void);

/**
 * @brief Reset all error statistics to zero
 *
 * Resets all error statistics counters to zero. Useful for resetting
 * statistics between test runs or monitoring periods.
 *
 * @note Thread-safe: Can be called from any thread.
 * @note All counters are reset to zero (total_errors, error_counts, etc.).
 *
 * @ingroup module_core
 */
void asciichat_error_stats_reset(void);

/**
 * @brief Get current error statistics
 * @return Current error statistics structure (copy)
 *
 * Retrieves a copy of the current error statistics. Returns all statistics
 * including per-error-code counts, total errors, and last error information.
 *
 * @note Returns a copy of statistics (caller can modify without affecting
 *       internal state).
 * @note Thread-safe: Can be called from any thread.
 *
 * @ingroup module_core
 */
asciichat_error_stats_t asciichat_error_stats_get(void);

/** @} */

/* ============================================================================
 * Thread-Safe Error Propagation
 * @{
 *
 * For multi-threaded scenarios where errors need to be propagated across
 * thread boundaries. These functions allow storing error codes per-thread
 * for later retrieval.
 */

/**
 * @brief Get error code for a specific thread
 * @param thread_id Thread ID to query
 * @return Error code for thread, or ASCIICHAT_OK if no error
 *
 * Retrieves the error code for a specific thread. Useful for checking errors
 * from other threads or propagating errors across thread boundaries.
 *
 * @note Thread ID must be valid (positive integer).
 * @note Returns ASCIICHAT_OK if thread has no error or thread ID is invalid.
 *
 * @ingroup module_core
 */
asciichat_error_t asciichat_get_thread_error(int thread_id);

/**
 * @brief Set error code for a specific thread
 * @param thread_id Thread ID to set error for
 * @param code Error code to set (asciichat_error_t)
 *
 * Sets the error code for a specific thread. Useful for propagating errors
 * from one thread to another or storing thread-specific error states.
 *
 * @note Thread ID must be valid (positive integer).
 * @note This is separate from thread-local error state (asciichat_errno).
 *
 * @ingroup module_core
 */
void asciichat_set_thread_error(int thread_id, asciichat_error_t code);

/**
 * @brief Clear error code for a specific thread
 * @param thread_id Thread ID to clear error for
 *
 * Clears the error code for a specific thread, resetting it to ASCIICHAT_OK.
 *
 * @note Thread ID must be valid (positive integer).
 *
 * @ingroup module_core
 */
void asciichat_clear_thread_error(int thread_id);

/** @} */

/* ============================================================================
 * Error System Control Functions
 * @{
 */

/**
 * @brief Suppress error logging and reporting
 * @param suppress If true, suppress error logging; if false, enable logging
 *
 * Controls whether errors are automatically logged when set. When suppressed,
 * errors are still set in thread-local storage but logging is disabled.
 * Useful for testing or when error logging would be too verbose.
 *
 * @note Suppression only affects automatic logging, not manual error checking.
 * @note Errors can still be checked using HAS_ERRNO() and GET_ERRNO().
 *
 * @ingroup module_core
 */
void asciichat_errno_suppress(bool suppress);

/**
 * @brief Cleanup error system resources
 *
 * Cleans up error system resources including statistics and thread error
 * storage. Should be called at application shutdown after all error handling
 * is complete.
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note All error statistics and context messages are freed.
 *
 * @ingroup module_core
 */
void asciichat_errno_cleanup(void);

/** @} */

/* ============================================================================
 * Application Error Checking Macros
 * ============================================================================
 * Use these in src/ code to check and log errors
 */

/**
 * @brief Check if any error occurred and log it if so
 * @param message Custom message to log
 *
 * Usage in src/ code:
 *   if (asciichat_errno != ASCIICHAT_OK) {
 *       LOG_ERRNO_IF_SET("Operation failed");
 *   }
 */
#define LOG_ERRNO_IF_SET(message)                                                                                      \
  do {                                                                                                                 \
    if (asciichat_errno != ASCIICHAT_OK) {                                                                             \
      asciichat_print_error_context(&asciichat_errno_context);                                                         \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Check if a specific error occurred and log it if so
 * @param code Error code to check for
 * @param message Custom message to log
 *
 * Usage in src/ code:
 *   if (asciichat_errno == ERROR_NETWORK) {
 *       LOG_ERRNO_IF_CODE(ERROR_NETWORK, "Network operation failed");
 *   }
 */
#define LOG_ERRNO_IF_CODE(code, message)                                                                               \
  do {                                                                                                                 \
    if (asciichat_errno == (code)) {                                                                                   \
      asciichat_print_error_context(&asciichat_errno_context);                                                         \
    }                                                                                                                  \
  } while (0)
