#pragma once

// Include system errno.h first to get the standard errno variable and constants
// Define feature test macros before including system headers for POSIX extensions
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <errno.h>

// Platform-specific includes - must be first
#ifdef _WIN32
#include "platform/windows_errno.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Platform-specific includes
#ifndef _WIN32
#include <unistd.h>
#endif

// Include common.h to get the asciichat_error_t definition
#include "common.h"

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

typedef struct {
  asciichat_error_t code;
  const char *file;
  int line;
  const char *function;
  char *context_message;    // Optional custom message (dynamically allocated)
  uint64_t timestamp;       // Microseconds since epoch
  int system_errno;         // System errno value (if applicable)
  int wsa_error;            // Windows socket error code (if applicable)
  void *backtrace[32];      // Stack trace (debug builds only)
  char **backtrace_symbols; // Stack symbols (debug builds only)
  int stack_depth;          // Number of frames captured
  bool has_system_error;    // Whether system_errno is valid
  bool has_wsa_error;       // Whether wsa_error is valid
} asciichat_error_context_t;

/* Thread-local storage for error context */
extern __thread asciichat_error_context_t asciichat_errno_context;

/* Thread-local storage for the current error code */
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
#define SET_ERRNO(code, context_msg, ...)                                                                              \
  ({                                                                                                                   \
    asciichat_set_errno_with_message(code, __FILE__, __LINE__, __func__, context_msg, ##__VA_ARGS__);                  \
    log_error("SET_ERRNO: " context_msg " (code: %d, meaning: %s)", ##__VA_ARGS__, code,                               \
              asciichat_error_string(code));                                                                           \
    (code);                                                                                                            \
  })

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
#define SET_ERRNO_SYS(code, context_msg, ...)                                                                          \
  ({                                                                                                                   \
    int captured_errno = platform_get_last_error();                                                                    \
    asciichat_set_errno_with_system_error_and_message(code, __FILE__, __LINE__, __func__, captured_errno, context_msg, \
                                                      ##__VA_ARGS__);                                                  \
    log_error("SETERRNO_SYS: " context_msg " (code: %d - %s, system error: %d - %s)", ##__VA_ARGS__, code,             \
              asciichat_error_string(code), captured_errno, platform_strerror(captured_errno));                        \
    (code);                                                                                                            \
  })

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
 * ============================================================================
 */

void asciichat_set_errno(asciichat_error_t code, const char *file, int line, const char *function,
                         const char *context_message);

void asciichat_set_errno_with_message(asciichat_error_t code, const char *file, int line, const char *function,
                                      const char *format, ...);

void asciichat_set_errno_with_system_error(asciichat_error_t code, const char *file, int line, const char *function,
                                           int sys_errno);

void asciichat_set_errno_with_system_error_and_message(asciichat_error_t code, const char *file, int line,
                                                       const char *function, int sys_errno, const char *format, ...);

void asciichat_set_errno_with_wsa_error(asciichat_error_t code, const char *file, int line, const char *function,
                                        int wsa_error);

bool asciichat_has_errno(asciichat_error_context_t *context);

// Helper function to check if the current error has a WSA error code
bool asciichat_has_wsa_error(void);

void asciichat_clear_errno(void);

asciichat_error_t asciichat_get_errno(void);

void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...);

void asciichat_print_error_context(const asciichat_error_context_t *context);

/* ============================================================================
 * Debug/Development Utilities
 * ============================================================================
 */

#ifdef DEBUG
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
#endif

/* ============================================================================
 * Error Statistics and Monitoring
 * ============================================================================
 */

typedef struct {
  uint64_t error_counts[256]; // Count for each error code
  uint64_t total_errors;
  uint64_t last_error_time;
  asciichat_error_t last_error_code;
} asciichat_error_stats_t;

void asciichat_error_stats_init(void);
void asciichat_error_stats_record(asciichat_error_t code);
void asciichat_error_stats_print(void);
void asciichat_error_stats_reset(void);
asciichat_error_stats_t asciichat_error_stats_get(void);

/* ============================================================================
 * Thread-Safe Error Propagation
 * ============================================================================
 * For multi-threaded scenarios
 */

asciichat_error_t asciichat_get_thread_error(int thread_id);
void asciichat_set_thread_error(int thread_id, asciichat_error_t code);
void asciichat_clear_thread_error(int thread_id);

/* Cleanup Functions */
void asciichat_errno_cleanup(void);

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
