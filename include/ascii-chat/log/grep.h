/**
 * @file filter.h
 * @brief Log filtering with PCRE2 regex matching and highlighting
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Parsed pattern result from /pattern/flags syntax
 *
 * Shared between --grep CLI filtering and interactive grep.
 */
typedef struct {
  char pattern[4096];     ///< Parsed pattern string
  uint32_t pcre2_options; ///< PCRE2 compile options
  bool is_fixed_string;   ///< True if fixed string (not regex)
  bool case_insensitive;  ///< Case-insensitive (i flag)
  bool invert;            ///< Invert match (I flag)
  bool global_flag;       ///< Highlight all matches (g flag)
  int context_before;     ///< Lines before match (B<n> flag)
  int context_after;      ///< Lines after match (A<n> flag)
  bool valid;             ///< True if parsing succeeded
} grep_parse_result_t;

/**
 * @brief Parse pattern in /pattern/flags format or plain regex format
 *
 * Supports two formats:
 * - Format 1: /pattern/flags (e.g., "/error/igC2")
 * - Format 2: plain pattern (e.g., "error")
 *
 * Flags: i(case-insensitive), m(multiline), s(dotall), x(extended),
 * g(global highlight), I(invert), F(fixed string),
 * A<n>(after context), B<n>(before context), C<n>(both context)
 *
 * @param input Input pattern string
 * @return Parsed result with pattern, flags, and validity
 */
grep_parse_result_t grep_parse_pattern(const char *input);

/**
 * @brief Initialize log filtering with PCRE2 regex pattern
 *
 * Compiles the pattern eagerly at startup. If compilation fails,
 * logs a warning and disables filtering.
 *
 * @param pattern PCRE2 regex pattern (NULL or empty = no filtering)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on bad regex
 */
asciichat_error_t grep_init(const char *pattern);

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
bool grep_should_output(const char *log_line, size_t *match_start, size_t *match_len);

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
const char *grep_highlight(const char *log_line, size_t match_start, size_t match_len);

/**
 * @brief Highlight matches in colored text while preserving original colors
 *
 * @param colored_text Original text with ANSI color codes
 * @param plain_text Stripped plain text (for match positions)
 * @param match_start Match start position in plain text
 * @param match_len Match length in plain text
 * @return Highlighted text with yellow background added to matches (pointer to static thread-local buffer)
 */
const char *grep_highlight_colored(const char *colored_text, const char *plain_text, size_t match_start,
                                         size_t match_len);

/**
 * @brief Highlight matches in colored text, returning a new allocated copy
 *
 * Like grep_highlight_colored() but returns a newly allocated string
 * instead of a static buffer. Caller must free the returned string with SAFE_FREE().
 *
 * @param colored_text Original text with ANSI color codes
 * @param plain_text Stripped plain text (for match positions)
 * @param match_start Match start position in plain text
 * @param match_len Match length in plain text
 * @return Newly allocated highlighted text (caller must free), or NULL on error
 */
char *grep_highlight_colored_copy(const char *colored_text, const char *plain_text, size_t match_start,
                                        size_t match_len);

/**
 * @brief Clean up filter resources
 *
 * Frees compiled regex. Thread-local match_data is cleaned up
 * via pthread_key destructor.
 */
void grep_destroy(void);

/**
 * @brief Save current filter patterns for later restoration
 *
 * Saves the current state of CLI --grep patterns so they can be
 * restored later. Used by interactive grep to preserve initial
 * filters when entering interactive mode.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t grep_save_patterns(void);

/**
 * @brief Restore previously saved filter patterns
 *
 * Restores filter patterns saved by grep_save_patterns().
 * Used by interactive grep when user cancels (Escape) to restore
 * CLI --grep patterns.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t grep_restore_patterns(void);

/**
 * @brief Clear all filter patterns
 *
 * Temporarily disables filtering by setting pattern count to 0.
 * Patterns remain allocated and can be restored later.
 * Used by interactive grep when user types first character.
 */
void grep_clear_patterns(void);

/**
 * @brief Get number of active filter patterns
 *
 * @return Number of currently active patterns
 */
int grep_get_pattern_count(void);

/**
 * @brief Get the last (most recent) filter pattern string
 *
 * Returns the original pattern string (with /pattern/flags syntax if provided)
 * of the last --grep argument. Used by interactive grep to populate its input
 * buffer when starting with CLI --grep patterns.
 *
 * @return Pattern string of last CLI --grep, or NULL if no patterns
 */
const char *grep_get_last_pattern(void);
