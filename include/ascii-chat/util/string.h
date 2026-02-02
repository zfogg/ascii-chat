#pragma once

/**
 * @file util/string.h
 * @brief 🔤 String Manipulation and Shell Escaping Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides utilities for string manipulation, shell command
 * escaping, and validation. Functions ensure strings are safe for use in
 * shell commands and command-line interfaces.
 *
 * CORE FEATURES:
 * ==============
 * - Shell-safe string validation
 * - Single and double quote escaping
 * - ASCII character escaping
 * - Cross-platform shell compatibility (Unix shells and Windows cmd.exe)
 * - Buffer overflow protection
 *
 * SHELL ESCAPING:
 * ===============
 * The system provides different escaping strategies:
 * - Single quotes: Escapes for Unix shells (sh, bash, zsh)
 * - Double quotes: Escapes for Windows cmd.exe compatibility
 * - ASCII escaping: Escapes specific characters for terminal output
 *
 * SHELL METACHARACTERS:
 * =====================
 * The following characters are considered unsafe for shell commands:
 * - Control characters: ; & | $ ` \ " ' < > ( ) [ ] { } * ? ! ~ # @
 * - These characters can be used for command injection or shell expansion
 *
 * @note All escaping functions ensure buffer overflow protection.
 * @note Escaped strings are safe for use in system() and popen() calls.
 * @note Validation functions check for all common shell metacharacters.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * String Escaping Functions
 * @{
 */

/**
 * @brief Escape ASCII characters in a string
 * @param str Input string to escape (must not be NULL)
 * @param escape_char Character to escape (must not be NULL)
 * @param out_buffer Output buffer for escaped string (must not be NULL)
 * @param out_buffer_size Size of output buffer (must be > 0)
 *
 * Escapes a specific ASCII character in a string by adding an escape prefix.
 * Useful for terminal output or string formatting where certain characters
 * need to be escaped.
 *
 * @note Output buffer must be large enough to hold the escaped string.
 * @note Function truncates output if buffer is too small.
 * @note Does not validate input parameters for NULL (caller's responsibility).
 *
 * @ingroup util
 */
void escape_ascii(const char *str, const char *escape_char, char *out_buffer, size_t out_buffer_size);

/**
 * @brief Validate that a string contains only safe characters for shell commands
 * @param str String to validate (must not be NULL)
 * @param allowed_chars Additional allowed characters (e.g., "-:_", can be NULL)
 * @return true if string is safe, false otherwise
 *
 * Validates that a string contains only safe characters for use in shell
 * commands. Rejects shell metacharacters that could be used for command
 * injection or shell expansion.
 *
 * REJECTED METACHARACTERS:
 * - Control operators: ; & | $ `
 * - Quote characters: " '
 * - Redirection: < >
 * - Brackets: ( ) [ ] { }
 * - Wildcards: * ?
 * - Special: \ ! ~ # @
 *
 * @note The allowed_chars parameter adds exceptions to the safety check.
 * @note Empty strings are considered safe (returns true).
 * @note This function is essential for preventing command injection attacks.
 *
 * @par Example
 * @code
 * if (!validate_shell_safe(user_input, NULL)) {
 *     // Reject unsafe input
 * }
 * @endcode
 *
 * @ingroup util
 */
bool validate_shell_safe(const char *str, const char *allowed_chars);

/**
 * @brief Escape a string for safe use in shell commands (single quotes)
 * @param str String to escape (must not be NULL)
 * @param out_buffer Output buffer for escaped string (must not be NULL)
 * @param out_buffer_size Size of output buffer (must be > 0)
 * @return true on success, false on failure (buffer too small)
 *
 * Escapes a string for safe use in Unix shell commands using single quotes.
 * Single quotes in the input are escaped by replacing ' with '\'' (single quote,
 * escaped quote, single quote). This ensures the string is safe for shell
 * evaluation when wrapped in single quotes.
 *
 * ESCAPING STRATEGY:
 * - Input: "don't"
 * - Output: "don'\''t"
 * - Shell command: echo 'don'\''t' -> prints: don't
 *
 * @note Output buffer must be large enough (worst case: 4x input size).
 * @note Returns false if buffer is too small to hold escaped string.
 * @note This escaping method is compatible with sh, bash, zsh, and other Unix shells.
 *
 * @warning Buffer overflow protection is enforced - function fails safely.
 *
 * @ingroup util
 */
bool escape_shell_single_quotes(const char *str, char *out_buffer, size_t out_buffer_size);

/**
 * @brief Escape a string for safe use in shell commands (double quotes, Windows-compatible)
 * @param str String to escape (must not be NULL)
 * @param out_buffer Output buffer for escaped string (must not be NULL)
 * @param out_buffer_size Size of output buffer (must be > 0)
 * @return true on success, false on failure (buffer too small)
 *
 * Escapes a string for safe use in shell commands using double quotes,
 * with Windows cmd.exe compatibility. Escapes double quotes and backslashes
 * as required by Windows command-line parsing rules.
 *
 * ESCAPING STRATEGY:
 * - Input: "C:\Program Files"
 * - Output: "C:\\Program Files"
 * - Double quotes and backslashes are escaped: " -> \", \ -> \\
 *
 * @note Output buffer must be large enough (worst case: 2x input size + quotes).
 * @note Returns false if buffer is too small to hold escaped string.
 * @note This escaping method is compatible with Windows cmd.exe and PowerShell.
 *
 * @warning Buffer overflow protection is enforced - function fails safely.
 *
 * @ingroup util
 */
bool escape_shell_double_quotes(const char *str, char *out_buffer, size_t out_buffer_size);

/**
 * @brief Check if a string needs shell quoting (contains spaces or special chars)
 * @param str String to check (must not be NULL)
 * @return true if string contains characters that require shell quoting, false otherwise
 *
 * Detects if a string contains whitespace or special characters that require
 * shell quoting when used in command-line arguments. Useful for determining
 * whether to apply shell escaping functions.
 *
 * CHARACTERS REQUIRING QUOTING:
 * - Whitespace: space, tab
 * - Special shell characters: " ' $ ` \ < > & ; | ( ) [ ] { } * ? ! ~ # @
 * - Control characters: newline, carriage return, etc.
 *
 * @note Empty strings return false (no quoting needed).
 * @note Alphanumeric and some safe punctuation (- . _) return false.
 *
 * @par Example
 * @code
 * if (string_needs_shell_quoting("/path/to file.txt")) {
 *     // Path contains a space, needs quoting
 * }
 * @endcode
 *
 * @ingroup util
 */
bool string_needs_shell_quoting(const char *str);

/**
 * @brief Escape a path for safe use in shell commands (auto-platform)
 * @param path Path string to escape (must not be NULL)
 * @param out_buffer Output buffer for escaped path (must not be NULL)
 * @param out_buffer_size Size of output buffer (must be > 0)
 * @return true on success, false on failure
 *
 * Automatically escapes a path for safe use in shell commands. If the path
 * contains special characters or whitespace, it applies the appropriate
 * platform-specific escaping (single quotes on Unix, double quotes on Windows).
 * If no escaping is needed, the path is copied as-is.
 *
 * PLATFORM BEHAVIOR:
 * - Unix/Linux/macOS: Uses single quote escaping (escape_shell_single_quotes)
 * - Windows: Uses double quote escaping (escape_shell_double_quotes)
 *
 * @note Output buffer must be large enough (worst case: 4x input size for Unix).
 * @note Returns false if buffer is too small or escaping fails.
 * @note Automatically detects if quoting is needed (no unnecessary escaping).
 *
 * @par Example
 * @code
 * char escaped[1024];
 * if (escape_path_for_shell("/usr/local/bin/tool", escaped, sizeof(escaped))) {
 *     // escaped contains the path, properly escaped if needed
 * }
 * @endcode
 *
 * @ingroup util
 */
bool escape_path_for_shell(const char *path, char *out_buffer, size_t out_buffer_size);

/** @} */

/* ============================================================================
 * String Formatting and Display
 * @{
 */

#include "../log/logging.h"

/**
 * @brief Build a colored string for terminal output
 *
 * Wraps text with ANSI color codes based on terminal capabilities.
 * Uses a rotating buffer to handle multiple colored strings in the same scope.
 * Checks if stdout is a TTY and CLAUDECODE environment variable.
 *
 * BEHAVIOR:
 * - If stdout is a TTY and CLAUDECODE is not set: Returns colored text with ANSI codes
 * - If stdout is not a TTY or CLAUDECODE is set: Returns plain text without colors
 * - Detects terminal color capabilities (respects NO_COLOR and terminal restrictions)
 *
 * USAGE:
 * @code
 * // Single colored string
 * fprintf(stderr, "%s\n", colored_string(LOG_COLOR_FATAL, "Error"));
 *
 * // Multiple colored strings in same scope (uses rotating buffers)
 * fprintf(stderr, "%s %s\n",
 *         colored_string(LOG_COLOR_FATAL, "Error:"),
 *         colored_string(LOG_COLOR_WARN, "warning message"));
 * @endcode
 *
 * @param color The log color to apply (LOG_COLOR_DEBUG, LOG_COLOR_FATAL, LOG_COLOR_WARN, etc.)
 * @param text The text to color
 * @return Colored string with ANSI codes, or plain text if colors disabled
 *
 * @note The returned pointer points to a static rotating buffer.
 *       Use or copy the result before calling colored_string() again in tight loops.
 * @note Color codes are applied based on log_level_color() from logging subsystem.
 *
 * @ingroup util
 */
const char *colored_string(log_color_t color, const char *text);

/** @} */
