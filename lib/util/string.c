/**
 * @file util/string.c
 * @ingroup util
 * @brief 🔤 String manipulation utilities: ASCII escaping, trimming, case conversion, and formatting
 */

#include "util/string.h"
#include "common.h"
#include <string.h>
#include <ctype.h>

void escape_ascii(const char *str, const char *escape_char, char *out_buffer, size_t out_buffer_size) {
  if (escape_char == NULL || str == NULL || !out_buffer || out_buffer_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid string or escape character or output buffer");
    return;
  }

  // Count how many characters need escaping
  size_t escape_count = 0;
  for (size_t i = 0; i < strlen(str); i++) {
    for (size_t j = 0; j < strlen(escape_char); j++) {
      if (str[i] == escape_char[j]) {
        escape_count++;
        break;
      }
    }
  }

  size_t total_len = strlen(str) + escape_count;
  if (out_buffer_size < total_len) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer is too small");
    return;
  }

  size_t out_pos = 0;
  for (size_t i = 0; i < strlen(str); i++) {
    bool needs_escape = false;
    for (size_t j = 0; j < strlen(escape_char); j++) {
      if (str[i] == escape_char[j]) {
        needs_escape = true;
        break;
      }
    }

    if (needs_escape) {
      out_buffer[out_pos++] = '\\';
    }
    out_buffer[out_pos++] = str[i];
  }

  out_buffer[out_pos] = '\0';
}

bool validate_shell_safe(const char *str, const char *allowed_chars) {
  if (!str) {
    return false;
  }

  // Shell metacharacters that are dangerous
  // ; & | $ ` \ " ' < > ( ) [ ] { } * ? ! ~ # @ space tab newline
  const char *dangerous = ";|$`\\\"'<>()[]{}*?!~#@ \t\n\r";

  for (size_t i = 0; str[i] != '\0'; i++) {
    unsigned char c = (unsigned char)str[i];

    // Allow alphanumeric characters
    if (isalnum(c)) {
      continue;
    }

    // Check if character is in allowed_chars list
    if (allowed_chars) {
      bool is_allowed = false;
      for (size_t j = 0; allowed_chars[j] != '\0'; j++) {
        if (c == (unsigned char)allowed_chars[j]) {
          is_allowed = true;
          break;
        }
      }
      if (is_allowed) {
        continue;
      }
    }

    // Check if character is dangerous
    bool is_dangerous = false;
    for (size_t j = 0; dangerous[j] != '\0'; j++) {
      if (c == (unsigned char)dangerous[j]) {
        is_dangerous = true;
        break;
      }
    }
    if (is_dangerous) {
      return false;
    }
  }

  return true;
}

bool escape_shell_single_quotes(const char *str, char *out_buffer, size_t out_buffer_size) {
  if (!str || !out_buffer || out_buffer_size == 0) {
    return false;
  }

  // Calculate required size: each ' becomes '\'' (4 chars) instead of 1
  // Worst case: all chars are quotes, so we need strlen(str) * 4 + 2 (for surrounding quotes)
  size_t required_size = 2; // Start and end quotes
  for (size_t i = 0; str[i] != '\0'; i++) {
    if (str[i] == '\'') {
      required_size += 4; // '\''
    } else {
      required_size += 1;
    }
  }

  if (out_buffer_size < required_size) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small for shell escaping");
    return false;
  }

  size_t out_pos = 0;
  out_buffer[out_pos++] = '\''; // Opening quote

  for (size_t i = 0; str[i] != '\0'; i++) {
    if (str[i] == '\'') {
      // Replace ' with '\'' (close quote, escaped quote, open quote)
      out_buffer[out_pos++] = '\'';
      out_buffer[out_pos++] = '\\';
      out_buffer[out_pos++] = '\'';
      out_buffer[out_pos++] = '\'';
    } else {
      out_buffer[out_pos++] = str[i];
    }
  }

  out_buffer[out_pos++] = '\''; // Closing quote
  out_buffer[out_pos] = '\0';

  return true;
}

bool escape_shell_double_quotes(const char *str, char *out_buffer, size_t out_buffer_size) {
  if (!str || !out_buffer || out_buffer_size == 0) {
    return false;
  }

  // Calculate required size: each " becomes \" (2 chars) instead of 1
  // Each \ also needs escaping, so \\ becomes \\\\
  // Worst case: all chars are quotes/backslashes, so we need strlen(str) * 2 + 2 (for surrounding quotes)
  size_t required_size = 2; // Start and end quotes
  for (size_t i = 0; str[i] != '\0'; i++) {
    if (str[i] == '"' || str[i] == '\\') {
      required_size += 2; // \" or '\\'
    } else {
      required_size += 1;
    }
  }

  if (out_buffer_size < required_size) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small for shell escaping");
    return false;
  }

  size_t out_pos = 0;
  out_buffer[out_pos++] = '"'; // Opening quote

  for (size_t i = 0; str[i] != '\0'; i++) {
    if (str[i] == '"') {
      // Escape double quote: " becomes \"
      out_buffer[out_pos++] = '\\';
      out_buffer[out_pos++] = '"';
    } else if (str[i] == '\\') {
      // Escape backslash: \ becomes '\\'
      out_buffer[out_pos++] = '\\';
      out_buffer[out_pos++] = '\\';
    } else {
      out_buffer[out_pos++] = str[i];
    }
  }

  out_buffer[out_pos++] = '"'; // Closing quote
  out_buffer[out_pos] = '\0';

  return true;
}
