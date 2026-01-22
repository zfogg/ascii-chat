/**
 * @file log/colorize.c
 * @ingroup logging
 * @brief Log message colorization for terminal output
 */

#include "colorize.h"
#include "logging.h"
#include "../util/string.h"
#include "../platform/system.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/**
 * @brief Check if character at position is the start of a numeric pattern
 *
 * Detects: numbers, hex, decimals, dimensions (1920x1080), units (25 MB, 1024 GiB, etc.)
 * Does NOT match numbers in the middle of words (e.g., "0KXNWM" won't match the 0)
 *
 * @param str Full string
 * @param pos Current position
 * @param end_pos Output: position after the pattern
 * @return true if numeric pattern found
 */
static bool is_numeric_pattern(const char *str, size_t pos, size_t *end_pos) {
  if (!isdigit(str[pos]) && str[pos] != '.') {
    return false;
  }

  // Don't match numbers in the middle of words - check if preceded by alphanumeric, hyphen, or underscore
  if (pos > 0 && (isalnum(str[pos - 1]) || str[pos - 1] == '-' || str[pos - 1] == '_')) {
    return false;
  }

  size_t i = pos;

  // Handle hex numbers (0x...)
  if (str[i] == '0' && (str[i + 1] == 'x' || str[i + 1] == 'X')) {
    i += 2;
    while (isxdigit(str[i])) {
      i++;
    }
    *end_pos = i;
    return true;
  }

  // Handle regular numbers and decimals
  bool has_digit = false;
  while (isdigit(str[i])) {
    has_digit = true;
    i++;
  }

  // Handle decimal point
  if (str[i] == '.' && isdigit(str[i + 1])) {
    i++;
    while (isdigit(str[i])) {
      i++;
    }
    has_digit = true;
  }

  if (!has_digit) {
    return false;
  }

  // Check for dimension format (1920x1080)
  if (str[i] == 'x' || str[i] == 'X') {
    if (isdigit(str[i + 1])) {
      i++;
      while (isdigit(str[i])) {
        i++;
      }
    }
  }

  // Check for units or percent (with or without space)
  // Handle optional space before unit
  size_t j = i;
  bool has_space = false;
  while (str[j] == ' ' || str[j] == '\t') {
    has_space = true;
    j++;
  }

  // Handle units like MB, MiB, GB, GiB, KiB, B, ms, us, ns, %, Hz, MHz, entries, items, packets, etc.
  if (isalpha(str[j]) || str[j] == '%') {
    // Collect the unit (up to 32 characters for words like "entries", "packets", etc.)
    size_t unit_start = j;
    while ((isalpha(str[j]) || str[j] == '%') && (j - unit_start) < 32) {
      j++;
    }
    // Include the space(s) before the unit
    i = j;
  } else if (!has_space) {
    // No space, no unit - just return the number
  } else {
    // Had space but no unit following - don't include the space
    i = j;
  }

  *end_pos = i;
  return true;
}

/**
 * @brief Check if character at position is the start of a file path
 *
 * Detects:
 * - Unix absolute paths: /path/to/file
 * - Unix relative paths: ./path, ../path, src/main.c
 * - Windows absolute paths: C:\path, \\network\share
 *
 * @param str Full string
 * @param pos Current position
 * @param end_pos Output: position after the path
 * @return true if file path found
 */
static bool is_file_path(const char *str, size_t pos, size_t *end_pos) {
  size_t i = pos;
  bool found = false;

  // Check for Windows absolute path (C:\...)
  if (pos > 0 && isalpha(str[pos]) && str[pos + 1] == ':' && str[pos + 2] == '\\') {
    i = pos + 2; // Start after the colon
    found = true;
  }
  // Check for Windows UNC path (\\server\share)
  else if (str[pos] == '\\' && str[pos + 1] == '\\') {
    i = pos + 2;
    found = true;
  }
  // Check for Unix absolute path (/)
  else if (str[pos] == '/') {
    i = pos;
    found = true;
  }
  // Check for relative path (., .., or alphanumeric followed by /)
  else if ((str[pos] == '.' && (str[pos + 1] == '/' || str[pos + 1] == '\\')) ||
           (str[pos] == '.' && str[pos + 1] == '.' && (str[pos + 2] == '/' || str[pos + 2] == '\\'))) {
    i = pos;
    found = true;
  } else if (isalnum(str[pos]) || str[pos] == '_' || str[pos] == '-') {
    // Could be start of relative path like "src/main.c"
    // Look ahead for slash to confirm it's a path
    size_t lookahead = pos;
    while ((isalnum(str[lookahead]) || str[lookahead] == '_' || str[lookahead] == '-' ||
            str[lookahead] == '.' || str[lookahead] == '/' || str[lookahead] == '\\') &&
           str[lookahead] != '\0') {
      if (str[lookahead] == '/' || str[lookahead] == '\\') {
        // Found a slash, this is a path
        i = pos;
        found = true;
        break;
      }
      lookahead++;
    }
  }

  if (!found) {
    return false;
  }

  // Collect the path characters
  while (str[i] != '\0' && str[i] != ' ' && str[i] != '\t' && str[i] != '\n' &&
         str[i] != ':' && str[i] != ',' && str[i] != ')' && str[i] != ']' && str[i] != '}' &&
         str[i] != '"' && str[i] != '\'') {
    if ((str[i] == '/' || str[i] == '\\') || isalnum(str[i]) || strchr("._-~", str[i])) {
      i++;
    } else {
      break;
    }
  }

  // Must have at least one slash to be a valid path
  bool has_slash = false;
  for (size_t j = pos; j < i; j++) {
    if (str[j] == '/' || str[j] == '\\') {
      has_slash = true;
      break;
    }
  }

  if (!has_slash) {
    return false;
  }

  *end_pos = i;
  return true;
}

/**
 * @brief Check if character at position is the start of an environment variable
 *
 * Detects: $VAR_NAME, $VAR_NAME_123, etc.
 *
 * @param str Full string
 * @param pos Current position
 * @param end_pos Output: position after the variable
 * @return true if environment variable found
 */
static bool is_env_var(const char *str, size_t pos, size_t *end_pos) {
  if (str[pos] != '$') {
    return false;
  }

  size_t i = pos + 1;

  // Environment variables should be all caps or underscore
  if (!isupper(str[i]) && str[i] != '_') {
    return false;
  }

  while ((isupper(str[i]) || str[i] == '_' || isdigit(str[i])) && str[i] != '\0') {
    i++;
  }

  // Must have at least 2 characters ($X is too short)
  if (i - pos < 2) {
    return false;
  }

  *end_pos = i;
  return true;
}

/**
 * @brief Check if we're currently in a reset state (no active color)
 *
 * Scans the message from start to current position, tracking ANSI escape codes.
 * Returns true if the last color-affecting code was a reset.
 *
 * @param message Full message
 * @param pos Current position to check
 * @return true if currently in reset state (can apply colors)
 */
static bool is_in_reset_state(const char *message, size_t pos) {
  bool in_reset = true; // Start in reset state

  // Scan from beginning to current position looking for ANSI codes
  for (size_t i = 0; i < pos && message[i] != '\0'; i++) {
    if (message[i] == '\x1b' && message[i + 1] == '[') {
      // Found ANSI escape sequence start
      // Look for the end marker 'm'
      size_t j = i + 2;
      while (message[j] != '\0' && message[j] != 'm') {
        j++;
      }

      if (message[j] == 'm') {
        // Extract the color code part (between [ and m)
        // Check if it's a reset code: \x1b[0m or \x1b[m
        if ((j == i + 3 && message[i + 2] == '0') || (j == i + 2)) {
          // Reset code
          in_reset = true;
        } else {
          // Color code (anything else)
          in_reset = false;
        }
      }
    }
  }

  return in_reset;
}

/**
 * @brief Colorize a log message for terminal output
 *
 * Uses 4 rotating static buffers like colored_string() to handle multiple
 * colorizations in a single expression.
 *
 * Only applies colors if output is going to a TTY (not piped/redirected).
 * Only colorizes text that is not already colored (in reset state).
 */
const char *colorize_log_message(const char *message) {
  if (!message) {
    return message;
  }

  // Check if stdout is a TTY - only colorize for TTY output
  if (!platform_isatty(STDOUT_FILENO)) {
    return message;
  }

  // Use 4 static buffers for rotation (handles nested calls)
  static char buffers[4][4096];
  static int buffer_index = 0;

  char *output = buffers[buffer_index];
  buffer_index = (buffer_index + 1) % 4;

  size_t out_pos = 0;
  const size_t max_size = sizeof(buffers[0]);

  // Process the input string
  for (size_t i = 0; message[i] != '\0' && out_pos < max_size - 100; i++) {
    size_t end_pos = 0;

    // Check if we're in reset state before trying to colorize
    bool can_colorize = is_in_reset_state(message, i);

    // Try numeric pattern
    if (can_colorize && is_numeric_pattern(message, i, &end_pos)) {
      size_t pattern_len = end_pos - i;
      char pattern_buf[256];
      snprintf(pattern_buf, sizeof(pattern_buf), "%.*s", (int)pattern_len, message + i);

      const char *colored = colored_string(LOG_COLOR_DEBUG, pattern_buf);
      size_t colored_len = strlen(colored);
      if (out_pos + colored_len < max_size) {
        memcpy(output + out_pos, colored, colored_len);
        out_pos += colored_len;
      }
      i = end_pos - 1;
      continue;
    }

    // Try file path
    if (can_colorize && is_file_path(message, i, &end_pos)) {
      size_t path_len = end_pos - i;
      char path_buf[512];
      snprintf(path_buf, sizeof(path_buf), "%.*s", (int)path_len, message + i);

      const char *colored = colored_string(LOG_COLOR_FATAL, path_buf);
      size_t colored_len = strlen(colored);
      if (out_pos + colored_len < max_size) {
        memcpy(output + out_pos, colored, colored_len);
        out_pos += colored_len;
      }
      i = end_pos - 1;
      continue;
    }

    // Try environment variable
    if (can_colorize && is_env_var(message, i, &end_pos)) {
      size_t var_len = end_pos - i;
      char var_buf[256];
      snprintf(var_buf, sizeof(var_buf), "%.*s", (int)var_len, message + i);

      const char *colored = colored_string(LOG_COLOR_GREY, var_buf);
      size_t colored_len = strlen(colored);
      if (out_pos + colored_len < max_size) {
        memcpy(output + out_pos, colored, colored_len);
        out_pos += colored_len;
      }
      i = end_pos - 1;
      continue;
    }

    // Regular character - just copy
    if (out_pos < max_size - 1) {
      output[out_pos++] = message[i];
    }
  }

  output[out_pos] = '\0';
  return output;
}
