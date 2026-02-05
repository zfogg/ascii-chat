/**
 * @file filter.h
 * @brief Log filtering with PCRE2 regex matching and highlighting
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize log filtering with PCRE2 regex pattern
 *
 * Compiles the pattern eagerly at startup. If compilation fails,
 * logs a warning and disables filtering.
 *
 * @param pattern PCRE2 regex pattern (NULL or empty = no filtering)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on bad regex
 */
asciichat_error_t log_filter_init(const char *pattern);

/**
 * @brief Check if log line should be output (matches filter)
 *
 * Thread-safe: Uses thread-local match_data storage.
 * Non-allocating: Reuses pre-allocated match_data.
 *
 * @param log_line Formatted log line to check
 * @param match_start Output: start offset of match
 * @param match_len Output: length of match
 * @return true if line should be output, false to suppress
 */
bool log_filter_should_output(const char *log_line, size_t *match_start, size_t *match_len);

/**
 * @brief Highlight matched portion with background color
 *
 * Inserts ANSI escape codes for yellow background (255, 200, 0)
 * with black foreground (0, 0, 0) around the matched text.
 *
 * @param log_line Original log line
 * @param match_start Start offset of match
 * @param match_len Length of match
 * @return Highlighted string (pointer to static thread-local buffer)
 */
const char *log_filter_highlight(const char *log_line, size_t match_start, size_t match_len);

/**
 * @brief Highlight matches in colored text while preserving original colors
 *
 * @param colored_text Original text with ANSI color codes
 * @param plain_text Stripped plain text (for match positions)
 * @param match_start Match start position in plain text
 * @param match_len Match length in plain text
 * @return Highlighted text with yellow background added to matches
 */
const char *log_filter_highlight_colored(const char *colored_text, const char *plain_text, size_t match_start,
                                         size_t match_len);

/**
 * @brief Clean up filter resources
 *
 * Frees compiled regex. Thread-local match_data is cleaned up
 * via pthread_key destructor.
 */
void log_filter_destroy(void);
