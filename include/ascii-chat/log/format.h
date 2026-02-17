/**
 * @file lib/log/format.h
 * @brief 📝 Log format parser - tokenize and compile custom log format strings
 * @ingroup logging
 *
 * Internal header for log format parsing. This module handles parsing of format
 * strings like "[%time(%H:%M:%S)] [%level_aligned] %message" into a compiled
 * format that can be efficiently rendered at log time.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/log/logging.h>

/* ============================================================================
 * Format Specifier Types
 * ============================================================================ */

/**
 * @brief Types of format specifiers that can appear in a format string
 */
typedef enum {
  LOG_FORMAT_LITERAL,         /* Plain text (no % prefix) */
  LOG_FORMAT_TIME,            /* %time(fmt) - custom time format */
  LOG_FORMAT_LEVEL,           /* %level - log level as string */
  LOG_FORMAT_LEVEL_ALIGNED,   /* %level_aligned - log level padded */
  LOG_FORMAT_FILE,            /* %file - file path */
  LOG_FORMAT_FILE_RELATIVE,   /* %file_relative - file path relative to project root */
  LOG_FORMAT_LINE,            /* %line - line number */
  LOG_FORMAT_FUNC,            /* %func - function name */
  LOG_FORMAT_TID,             /* %tid - thread ID */
  LOG_FORMAT_MESSAGE,         /* %message - log message */
  LOG_FORMAT_COLORLOG_LEVEL,  /* %colorlog_level_string_to_color - color code */
  LOG_FORMAT_COLOR,           /* %color(LEVEL, content) - colorize content using LEVEL's color */
  LOG_FORMAT_COLORED_MESSAGE, /* %colored_message - message with things like filenames and 0x numbers colored */
  LOG_FORMAT_NEWLINE,         /* Platform-aware newline (\n) */
} log_format_type_t;

/* ============================================================================
 * Parsed Format Specifier
 * ============================================================================ */

/**
 * @brief A single parsed format specifier
 */
typedef struct {
  log_format_type_t type; /* Type of specifier */
  char *literal;          /* For LOG_FORMAT_LITERAL, the text; for LOG_FORMAT_TIME, the format string */
  size_t literal_len;     /* Length of literal text */
} log_format_spec_t;

/* ============================================================================
 * Compiled Log Format
 * ============================================================================ */

/**
 * @brief Compiled log format ready for use in log_format_apply()
 */
typedef struct {
  log_format_spec_t *specs; /* Array of parsed specifiers */
  size_t spec_count;        /* Number of specifiers */
  char *original;           /* Original format string (for debugging) */
  bool console_only;        /* If true, apply only to console (not file) */
} log_format_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a format string into compiled format structure
 * @param format_str Format string (e.g., "[%time(%H:%M:%S)] [%level_aligned] %message")
 * @param console_only If true, format applies only to console (not file)
 * @return Compiled format, or NULL on parse error
 *
 * Parses format string and compiles it into efficient specifier array.
 * Format string must be valid UTF-8. Returns NULL on:
 * - Invalid UTF-8 in format string
 * - Invalid format specifiers
 * - Malformed %time(format) syntax
 * - Memory allocation failure
 *
 * @note Errors are logged with details about what failed
 * @ingroup logging
 */
log_format_t *log_format_parse(const char *format_str, bool console_only);

/**
 * @brief Free compiled format structure
 * @param format Pointer to format to free (safe to call with NULL)
 * @ingroup logging
 */
void log_format_free(log_format_t *format);

/**
 * @brief Apply format to a log entry and write result to buffer
 * @param format Compiled format (from log_format_parse)
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @param level Log level
 * @param timestamp Pre-formatted timestamp string
 * @param file Source file name (or NULL)
 * @param line Source line number (or 0)
 * @param func Function name (or NULL)
 * @param tid Thread ID
 * @param message Log message text
 * @param use_colors If true, apply ANSI color codes
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Renders the compiled format using provided log entry values. Evaluates each
 * specifier and writes results to output buffer.
 *
 * **Example:**
 * ```c
 * log_format_t *fmt = log_format_parse("[%time(%H:%M:%S)] [%level] %message", false);
 * char output[512];
 * int len = log_format_apply(fmt, output, sizeof(output),
 *                            LOG_INFO, "14:30:45.123456", "test.c", 42, "main",
 *                            1234, "Test message", false);
 * ```
 *
 * @ingroup logging
 */
int log_format_apply(const log_format_t *format, char *buf, size_t buf_size, log_level_t level, const char *timestamp,
                     const char *file, int line, const char *func, uint64_t tid, const char *message, bool use_colors);

#ifdef __cplusplus
}
#endif

/** @} */
