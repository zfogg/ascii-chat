// SPDX-License-Identifier: MIT
/**
 * @file tooling/panic/instrument_log.h
 * @brief 🔍 Debug instrumentation logging runtime for ascii-chat line tracing
 * @ingroup panic
 * @addtogroup panic
 * @{
 *
 * This module provides the runtime logging infrastructure for the source-print
 * instrumentation system. When code is instrumented with ascii-instr-panic,
 * calls to ascii_instr_log_line() are inserted at each statement to trace
 * execution flow.
 *
 * ## Features
 *
 * - **Per-Thread Logging**: Each thread gets its own log file or runtime context
 * - **Configurable Filtering**: Filter by file path, function name, thread ID,
 *   or using regex patterns
 * - **Rate Limiting**: Sample every Nth log entry to reduce output volume
 * - **Coverage Mode**: Log program counter addresses for coverage analysis
 * - **Environment Configuration**: All settings controlled via environment variables
 *
 * ## Environment Variables
 *
 * - `ASCII_INSTR_SOURCE_PRINT_ENABLE`: Enable/disable tracing (default: enabled)
 * - `ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR`: Directory for log files
 * - `ASCII_INSTR_SOURCE_PRINT_INCLUDE`: Substring filter for file paths (include)
 * - `ASCII_INSTR_SOURCE_PRINT_EXCLUDE`: Substring filter for file paths (exclude)
 * - `ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE`: Substring filter for functions
 * - `ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE`: Substring filter for functions
 * - `ASCII_INSTR_SOURCE_PRINT_THREAD`: Filter by thread ID(s)
 * - `ASCII_INSTR_SOURCE_PRINT_RATE`: Log every Nth entry (rate limiting)
 * - `ASCII_INSTR_SOURCE_PRINT_ONLY`: Comma-separated selector filters
 * - `ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE`: Enable PC logging for coverage
 *
 * Regex variants (Unix only):
 * - `ASCII_INSTR_SOURCE_PRINT_INCLUDE_REGEX`
 * - `ASCII_INSTR_SOURCE_PRINT_EXCLUDE_REGEX`
 * - `ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE_REGEX`
 * - `ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE_REGEX`
 *
 * @see docs/tooling/panic-instrumentation.md for full documentation
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Macro expansion indicator values for ascii_instr_log_line()
 *
 * These values indicate whether a logged line originates from a macro
 * expansion, the macro invocation site, or regular code.
 */
enum {
  ASCII_INSTR_SOURCE_PRINT_MACRO_NONE = 0u,      /**< Regular code, not from a macro */
  ASCII_INSTR_SOURCE_PRINT_MACRO_EXPANSION = 1u, /**< Inside macro expansion */
  ASCII_INSTR_SOURCE_PRINT_MACRO_INVOCATION = 2u /**< At macro invocation site */
};

/**
 * @brief Attribute to mark signal handler functions
 *
 * Functions marked with this attribute are recognized by the instrumentation
 * tool and may receive special handling to avoid signal-unsafe operations.
 */
#if defined(__clang__)
#define ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER __attribute__((annotate("ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER")))
#else
#define ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER
#endif

/**
 * @brief Opaque runtime context for instrumentation logging
 *
 * Each thread has its own runtime context that tracks:
 * - File descriptor for the log file
 * - Sequence numbers and call counters
 * - Filter configuration (compiled regexes, substring patterns)
 * - Rate limiting state
 */
typedef struct ascii_instr_runtime ascii_instr_runtime_t;

/**
 * @brief Get or create the thread-local runtime context
 *
 * Returns the instrumentation runtime for the current thread, creating one
 * if it doesn't exist. The runtime is stored in thread-local storage and
 * cleaned up automatically when the thread exits.
 *
 * @return Runtime context, or NULL if disabled or initialization fails
 *
 * @note Thread-safe: each thread gets its own context
 * @note Returns NULL if ASCII_INSTR_SOURCE_PRINT_ENABLE=0
 */
ascii_instr_runtime_t *ascii_instr_runtime_get(void);

/**
 * @brief Destroy a runtime context and release resources
 *
 * Closes the log file descriptor, frees compiled regexes, and deallocates
 * the runtime structure. Called automatically by TLS destructor on thread exit.
 *
 * @param runtime Context to destroy (safe to pass NULL)
 */
void ascii_instr_runtime_destroy(ascii_instr_runtime_t *runtime);

/**
 * @brief Global shutdown of the instrumentation system
 *
 * Disables all logging, cleans up the TLS key, and resets global state.
 * Should be called at program exit to ensure clean shutdown.
 *
 * @note After calling this, logging can be re-enabled by resetting env vars
 * @note Thread-safe: acquires global mutex during shutdown
 */
void ascii_instr_runtime_global_shutdown(void);

/**
 * @brief Log a source line execution event
 *
 * Called by instrumented code at each statement to record execution trace.
 * The log entry includes timestamp, thread ID, sequence number, file location,
 * function name, and source code snippet.
 *
 * @param file_path        Source file path (typically from __FILE__)
 * @param line_number      Line number in the source file
 * @param function_name    Name of the containing function
 * @param snippet          Source code snippet for this line
 * @param is_macro_expansion One of ASCII_INSTR_SOURCE_PRINT_MACRO_* values
 *
 * @note Filters are applied before logging; entry may be skipped
 * @note Thread-safe: uses per-thread runtime context
 * @note Reentrant-safe: nested calls are detected and skipped
 */
void ascii_instr_log_line(const char *file_path, uint32_t line_number, const char *function_name, const char *snippet,
                          uint8_t is_macro_expansion);

/**
 * @brief Check if coverage logging is enabled
 *
 * Coverage mode logs program counter addresses instead of full source info,
 * useful for generating coverage reports with lower overhead.
 *
 * @return true if ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE is set
 */
bool ascii_instr_coverage_enabled(void);

/**
 * @brief Log a program counter for coverage analysis
 *
 * Records just the PC address with minimal metadata, intended for
 * post-processing into coverage reports.
 *
 * @param program_counter The instruction address to log
 *
 * @note Only logs if coverage mode is enabled
 */
void ascii_instr_log_pc(uintptr_t program_counter);

/** @} */

#ifdef __cplusplus
}
#endif
