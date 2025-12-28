/**
 * @file platform/question.h
 * @brief Cross-platform interactive prompting utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides interactive prompting functionality across Windows, Linux, and macOS.
 * Supports text input with optional echo, yes/no questions with defaults,
 * and configurable answer placement (same line or next line).
 *
 * All prompt functions handle terminal locking to prevent log interleaving,
 * check for TTY availability, and support non-interactive mode detection.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Options for text prompts
 */
typedef struct {
  bool echo;      /**< Whether to echo input (false for passwords) */
  bool same_line; /**< If true, answer on same line after prompt; if false, answer on next line */
  char mask_char; /**< Character to display instead of input when echo=false (0 for no mask, '*' typical) */
} prompt_opts_t;

/**
 * @brief Default prompt options (echo enabled, answer on next line)
 */
#define PROMPT_OPTS_DEFAULT (prompt_opts_t){.echo = true, .same_line = false, .mask_char = 0}

/**
 * @brief Prompt options for password input (no echo, asterisk masking, same line)
 */
#define PROMPT_OPTS_PASSWORD (prompt_opts_t){.echo = false, .same_line = true, .mask_char = '*'}

/**
 * @brief Prompt options for inline text input (echo enabled, same line)
 */
#define PROMPT_OPTS_INLINE (prompt_opts_t){.echo = true, .same_line = true, .mask_char = 0}

/**
 * @brief Prompt the user for text input
 * @param prompt The prompt message to display
 * @param buffer Buffer to store the entered text
 * @param max_len Maximum length of the buffer (including null terminator)
 * @param opts Prompt options (use PROMPT_OPTS_DEFAULT, PROMPT_OPTS_PASSWORD, etc.)
 * @return 0 on success, -1 on failure or user cancellation (Ctrl+C)
 *
 * Displays a prompt and reads user input. The prompt format depends on opts:
 * - same_line=true:  "prompt: " (user types on same line)
 * - same_line=false: "prompt:\n> " (user types on next line after "> ")
 *
 * When echo=false, input is hidden and optionally masked with mask_char.
 *
 * @note Acquires terminal lock during prompting to prevent log interleaving.
 * @note Returns -1 if stdin is not a TTY (non-interactive mode).
 *
 * @ingroup platform
 */
int platform_prompt_question(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts);

/**
 * @brief Prompt the user for a yes/no answer
 * @param prompt The question to ask (without the yes/no suffix)
 * @param default_yes If true, default is Yes (Y/n); if false, default is No (y/N)
 * @return true if user answered yes, false if user answered no or on error
 *
 * Displays a yes/no prompt with the default shown in uppercase:
 * - default_yes=true:  "prompt (Y/n)? "
 * - default_yes=false: "prompt (y/N)? "
 *
 * Accepts: "yes", "y", "Y" for yes; "no", "n", "N" for no.
 * Empty input (just Enter) returns the default value.
 *
 * @note Acquires terminal lock during prompting to prevent log interleaving.
 * @note Returns false if stdin is not a TTY (non-interactive mode).
 *
 * @ingroup platform
 */
bool platform_prompt_yes_no(const char *prompt, bool default_yes);

/**
 * @brief Check if interactive prompting is available
 * @return true if stdin is a TTY and interactive prompting is possible
 *
 * Use this to check before calling prompt functions in contexts where
 * non-interactive operation is acceptable (e.g., scripted usage).
 *
 * @ingroup platform
 */
bool platform_is_interactive(void);

#ifdef __cplusplus
}
#endif

/** @} */
