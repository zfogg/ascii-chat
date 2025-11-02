#pragma once

/**
 * @file string.h
 * @brief String Manipulation and Shell Escaping Utilities
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

/** @} */
