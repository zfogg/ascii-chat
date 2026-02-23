/**
 * @file debug/backtrace.h
 * @ingroup debug_util
 * @brief 📍 Backtrace capture, symbolization, and formatting
 *
 * Provides a unified backtrace_t type and API for capturing, symbolizing, and
 * printing stack traces. This abstraction consolidates backtrace handling that
 * was previously scattered across multiple files.
 */

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
 * @brief Print backtrace with colored terminal output and plain log file output
 *
 * Formats and prints a single backtrace to:
 * - stderr: colored with semantic highlighting (colored terminal)
 * - log file: plain ASCII (no colors)
 *
 * @param label Header label (e.g., "Backtrace", "Leak trace")
 * @param bt Backtrace to print (must not be NULL, must be symbolized)
 * @param skip_frames Number of frames to skip from the start (usually 0)
 * @param max_frames Maximum frames to print (0 = unlimited)
 * @param filter Optional filter callback to skip specific frames (NULL = no filtering)
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
 * @brief Format backtrace to buffer
 *
 * Formats backtrace into a plain ASCII buffer (no colors).
 * Useful for generating log file content or messages.
 *
 * @param buf Output buffer
 * @param buf_size Size of output buffer
 * @param label Header label
 * @param bt Backtrace to format (must not be NULL, must be symbolized)
 * @param skip_frames Number of frames to skip from the start
 * @param max_frames Maximum frames to include (0 = unlimited)
 * @param filter Optional filter callback (NULL = no filtering)
 *
 * @return Number of bytes written (not including null terminator), or -1 on error
 */
int backtrace_format(char *buf, size_t buf_size, const char *label, const backtrace_t *bt, int skip_frames,
                     int max_frames, backtrace_frame_filter_t filter);

#ifdef __cplusplus
}
#endif
