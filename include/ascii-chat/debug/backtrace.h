/**
 * @file debug/backtrace.h
 * @ingroup debug_util
 * @brief 📍 Backtrace capture, symbolization, and formatting
 * @addtogroup debug_util
 * @{
 *
 * Provides a unified backtrace_t type and API for capturing, symbolizing, and
 * printing stack traces. This abstraction consolidates backtrace handling that
 * was previously scattered across multiple files.
 *
 * ## Purpose
 *
 * Stack traces are invaluable for understanding program flow during:
 * - **Development**: Debugging segfaults, assertion failures, and unexpected state
 * - **Production**: Correlating errors with their root causes in logs/reports
 * - **Testing**: Validating execution paths and error conditions
 *
 * This module provides efficient capture (single syscall), optional symbolization
 * (maps addresses to function names/source locations), and flexible printing
 * (colored terminal vs. plain log file formats).
 *
 * ## Key Features
 *
 * - **Platform abstraction**: POSIX (backtrace/addr2line) and Windows (StackWalk) support
 * - **Efficient capture**: Minimal overhead - single platform_backtrace() call
 * - **Lazy symbolization**: Only symbolize when needed, not on every capture
 * - **Frame filtering**: Skip internal/framework frames to focus on application code
 * - **Dual output**: Colored stderr for development, plain text for log files
 * - **Multiple formats**: Print to stderr, format to buffer, or batch-print multiple traces
 *
 * ## Usage Examples
 *
 * ### Development: Quick Backtrace on Error
 *
 * @code
 * asciichat_error_t result = some_operation();
 * if (result != ASCIICHAT_OK) {
 *     backtrace_t bt;
 *     backtrace_capture_and_symbolize(&bt);
 *     backtrace_print("Error backtrace", &bt, 0, 5, NULL);
 *     backtrace_t_free(&bt);
 *     return result;
 * }
 * @endcode
 *
 * ### Production: Memory Leak with Source Locations
 *
 * @code
 * // During allocation - store backtrace for leak reporting
 * backtrace_t alloc_trace;
 * backtrace_capture_and_symbolize(&alloc_trace);
 * // ... store trace in allocation metadata ...
 * // On leak detection (exit time):
 * backtrace_print("Leaked from", &alloc_trace, 0, 0, NULL);
 * @endcode
 *
 * ### Filtering: Skip Framework Frames
 *
 * @code
 * bool skip_internal_frames(const char *frame) {
 *     // Skip lib/platform and lib/log frames
 *     return strstr(frame, "lib/platform") != NULL ||
 *            strstr(frame, "lib/log") != NULL;
 * }
 *
 * backtrace_t bt;
 * backtrace_capture_and_symbolize(&bt);
 * backtrace_print("Application trace", &bt, 0, 10, skip_internal_frames);
 * @endcode
 *
 * ### Buffer Formatting: Log-Safe Output
 *
 * @code
 * backtrace_t bt;
 * backtrace_capture_and_symbolize(&bt);
 *
 * char buffer[4096];
 * int written = backtrace_format(buffer, sizeof(buffer),
 *                                "Trace", &bt, 0, 0, NULL);
 * if (written > 0) {
 *     log_info("Stack: %s", buffer);  // Safe for log system
 * }
 * backtrace_t_free(&bt);
 * @endcode
 *
 * ## Build Modes
 *
 * - **Full support**: Linux with addr2line, macOS, Windows with DWARF/PDB info
 * - **Limited support**: Embedded systems may have symbolic info stripped
 * - **Graceful degradation**: Hex addresses are always available even if symbols fail
 *
 * @see lib/platform/backtrace.h for platform-specific implementations
 * @see asciichat_errno.h for backtrace_t structure definition
 *
 * @} */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* backtrace_t is defined in asciichat_errno.h to avoid circular includes */
#include <ascii-chat/asciichat_errno.h>

/**
 * @brief Frame filter callback for selective frame inclusion
 *
 * Used by backtrace_print() to filter out unwanted frames.
 * Return true to skip (filter out) the frame, false to include it.
 */
typedef bool (*backtrace_frame_filter_t)(const char *frame);

/**
 * @brief Capture current stack into bt
 *
 * Calls platform_backtrace() to capture raw return addresses.
 * Does not symbolize (symbols remain NULL).
 *
 * @param bt Backtrace structure to fill (must not be NULL)
 */
void backtrace_capture(backtrace_t *bt);

/**
 * @brief Symbolize bt->ptrs into bt->symbols
 *
 * Calls platform_backtrace_symbols() to convert raw addresses to symbol strings.
 * This is a no-op if symbols are already set (already symbolized).
 *
 * @param bt Backtrace structure to symbolize (must not be NULL)
 */
void backtrace_symbolize(backtrace_t *bt);

/**
 * @brief Capture and symbolize in one call
 *
 * Convenience function combining backtrace_capture() and backtrace_symbolize().
 *
 * @param bt Backtrace structure to fill and symbolize (must not be NULL)
 */
void backtrace_capture_and_symbolize(backtrace_t *bt);

/**
 * @brief Free backtrace symbols
 *
 * Calls platform_backtrace_symbols_destroy() to free the symbols array.
 * Nulls the symbols pointer after freeing.
 *
 * @param bt Backtrace structure (must not be NULL)
 */
void backtrace_t_free(backtrace_t *bt);

/**
 * @brief Print backtrace with dual output: colored stderr + plain log file
 *
 * Intelligently formats and routes backtraces to multiple destinations:
 * - **stderr**: Colored output with semantic highlighting (for interactive debugging)
 * - **log file** (if logging): Plain ASCII without ANSI codes (for file parsing)
 *
 * This dual-output design ensures readable terminal output during development
 * while keeping log files clean and searchable in production.
 *
 * ## Output Format
 *
 * Terminal output example:
 * ```
 * === Backtrace: Error ===
 * [0] 0x7f1234567890 in main() [src/main.c:42]
 * [1] 0x7f1234567800 in process_request() [src/network.c:128]
 * [2] 0x7f1234567700 in send_data() [src/io.c:67]
 * ```
 *
 * Log file (same content, no colors):
 * ```
 * === Backtrace: Error ===
 * [0] 0x7f1234567890 in main() [src/main.c:42]
 * ...
 * ```
 *
 * @param label Header label (e.g., "Backtrace", "Leak trace", "Assertion failure")
 * @param bt Backtrace to print (must not be NULL, must be symbolized via backtrace_capture_and_symbolize)
 * @param skip_frames Number of frames to skip from start (0 = include all, 1 = skip first frame, etc.)
 * @param max_frames Maximum frames to print (0 = unlimited, 5 = first 5 frames only)
 * @param filter Optional callback to selectively skip frames (e.g., filter internal frames)
 *              Return true to skip frame, false to include. NULL = no filtering.
 *
 * @note Backtrace must be symbolized. Unsymbolized (raw hex) traces are less useful.
 * @note Frame filtering is useful to focus on application code, excluding:
 *       - lib/platform internal calls
 *       - lib/log infrastructure
 *       - Exception handling wrappers
 *
 * @see backtrace_format() for buffer-based output (safe for log system)
 */
void backtrace_print(const char *label, const backtrace_t *bt, int skip_frames, int max_frames,
                     backtrace_frame_filter_t filter);

/**
 * @brief Print multiple backtraces
 *
 * Convenience function for iterating and printing multiple backtrace_t entries.
 *
 * @param label Header label for all backtraces
 * @param bts Array of backtrace structures
 * @param count Number of backtraces to print
 */
void backtrace_print_many(const char *label, const backtrace_t *bts, int count);

/**
 * @brief Format backtrace to buffer (plain ASCII, no colors)
 *
 * Formats a backtrace into a plain ASCII buffer without ANSI color codes.
 * This is the safe choice for:
 * - Generating log file content
 * - Embedding in error messages or email alerts
 * - Storing in databases or structured logs
 * - Sending over network protocols
 *
 * The buffer must not be freed before use - the returned count is the number
 * of bytes written. The buffer is null-terminated if space permits.
 *
 * ## Output Example
 *
 * @code
 * char buffer[2048];
 * backtrace_t bt;
 * backtrace_capture_and_symbolize(&bt);
 *
 * int written = backtrace_format(buffer, sizeof(buffer),
 *                                "Crash", &bt, 0, 10, NULL);
 * if (written > 0) {
 *     // Safe to use with logging system
 *     log_error("Program crashed:\n%s", buffer);
 *     // Safe to include in structured fields
 *     json_add_string(crash_report, "backtrace", buffer);
 * }
 * @endcode
 *
 * @param buf Output buffer (will be null-terminated if space permits)
 * @param buf_size Size of output buffer in bytes (must be at least 1)
 * @param label Header label (e.g., "Crash", "Assertion")
 * @param bt Backtrace to format (must not be NULL, must be symbolized)
 * @param skip_frames Number of frames to skip from start (0 = all)
 * @param max_frames Maximum frames to include (0 = unlimited)
 * @param filter Optional filter callback (NULL = no filtering)
 *
 * @return Number of bytes written (not including null terminator), or -1 on error
 *         If buffer is too small, returns the bytes that would be written if space allowed
 *
 * @note Buffer is always null-terminated on success (output < buf_size)
 * @note This is the preferred output method for integration with structured logging
 * @note No ANSI color codes are included, making output safe for any destination
 *
 * @see backtrace_print() for dual (colored terminal + plain log file) output
 */
int backtrace_format(char *buf, size_t buf_size, const char *label, const backtrace_t *bt, int skip_frames,
                     int max_frames, backtrace_frame_filter_t filter);

#ifdef __cplusplus
}
#endif
