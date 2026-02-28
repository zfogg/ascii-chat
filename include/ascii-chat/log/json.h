/**
 * @file json.h
 * @ingroup logging
 * @brief 📝 JSON structured logging output
 */

#pragma once

#include <ascii-chat/log/log.h>
#include <stdint.h>

/**
 * @brief Write a log entry as a JSON object to the json output fd.
 *
 * Outputs a newline-delimited JSON (NDJSON) format log entry with the following structure:
 * ```json
 * {
 *   "header": {
 *     "timestamp": "14:30:45.123456",
 *     "level": "INFO",
 *     "tid": 12345,
 *     "file": "lib/network/server.c",
 *     "line": 42,
 *     "func": "handle_client"
 *   },
 *   "body": {
 *     "message": "Client connected"
 *   }
 * }
 * ```
 *
 * The `file`, `line`, and `func` fields are omitted from the output when they are
 * NULL/0 (which typically occurs in release builds).
 *
 * @param fd File descriptor to write JSON to (-1 = disabled)
 * @param level Log level (DEBUG, INFO, WARN, ERROR, FATAL)
 * @param time_nanoseconds Current time in nanoseconds (used to format timestamp)
 * @param file Source file name (can be NULL)
 * @param line Line number (can be 0)
 * @param func Function name (can be NULL)
 * @param message Formatted log message
 *
 * @see log_set_json_output() - Set the JSON output file descriptor
 */
void log_json_write(int fd, log_level_t level, uint64_t time_nanoseconds, const char *file, int line, const char *func,
                    const char *message);

/**
 * @brief Set the JSON output file descriptor.
 *
 * Sets which file descriptor should receive JSON-formatted logs.
 * Pass -1 to disable JSON output.
 *
 * @param fd File descriptor (typically STDERR_FILENO for terminal, or an open log file)
 *
 * @note This function is thread-safe (uses atomic operations)
 */
void log_set_json_output(int fd);

/**
 * @brief Async-safe JSON logging for signal handlers
 *
 * This function formats and writes JSON logs using ONLY async-safe operations:
 * - snprintf for string formatting
 * - write() for output
 * - No allocations, no locks, no library calls
 *
 * Suitable for calling from signal handlers (SIGTERM, SIGINT, etc.)
 *
 * @param fd File descriptor to write to (typically STDOUT_FILENO or STDERR_FILENO)
 * @param level Log level (DEBUG, INFO, WARN, ERROR, FATAL)
 * @param file Source file name (can be NULL)
 * @param line Source line number (can be 0)
 * @param func Function name (can be NULL)
 * @param message Log message (must not be NULL)
 */
void log_json_async_safe(int fd, log_level_t level, const char *file, int line, const char *func, const char *message);
