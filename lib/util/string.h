#pragma once

#include <stddef.h>
#include <stdbool.h>

void escape_ascii(const char *str, const char *escape_char, char *out_buffer, size_t out_buffer_size);

/**
 * @brief Validate that a string contains only safe characters for shell commands
 * @param str String to validate
 * @param allowed_chars Additional allowed characters (e.g., "-:_")
 * @return true if string is safe, false otherwise
 * @note Rejects shell metacharacters: ; & | $ ` \ " ' < > ( ) [ ] { } * ? ! ~ # @
 */
bool validate_shell_safe(const char *str, const char *allowed_chars);

/**
 * @brief Escape a string for safe use in shell commands (single quotes)
 * @param str String to escape
 * @param out_buffer Output buffer
 * @param out_buffer_size Size of output buffer
 * @return true on success, false on failure
 * @note Escapes single quotes by replacing ' with '\'' (single quote, escaped quote, single quote)
 */
bool escape_shell_single_quotes(const char *str, char *out_buffer, size_t out_buffer_size);

/**
 * @brief Escape a string for safe use in shell commands (double quotes, Windows-compatible)
 * @param str String to escape
 * @param out_buffer Output buffer
 * @param out_buffer_size Size of output buffer
 * @return true on success, false on failure
 * @note Escapes double quotes and backslashes for Windows cmd.exe compatibility
 */
bool escape_shell_double_quotes(const char *str, char *out_buffer, size_t out_buffer_size);
