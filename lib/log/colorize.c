/**
 * @file log/colorize.c
 * @ingroup logging
 * @brief Log message colorization for terminal output
 */

#include <ascii-chat/log/colorize.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/video/ansi.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/**
 * @brief Check if a string is a known unit (MB, GB, ms, Hz, etc.)
 *
 * @param str String to check (should be null-terminated or length-bounded)
 * @param max_len Maximum length to check
 * @return true if the string is a recognized unit
 */
static bool is_known_unit(const char *str, size_t max_len) {
  if (!str || max_len == 0) {
    return false;
  }

  // Known units: byte sizes, time units, frequency, percentage, and count descriptors
  const char *known_units[] = {
      // Byte sizes
      "B",
      "KB",
      "MB",
      "GB",
      "TB",
      "PB",
      "EB",
      "KiB",
      "MiB",
      "GiB",
      "TiB",
      "PiB",
      "EiB",
      // Time
      "ms",
      "us",
      "ns",
      "ps",
      "s",
      "sec",
      "second",
      "seconds",
      "m",
      "min",
      "minute",
      "minutes",
      "h",
      "hr",
      "hour",
      "hours",
      // Frequency
      "Hz",
      "kHz",
      "MHz",
      "GHz",
      // Percentage
      "%",
      // Count descriptors (commonly used in logs)
      "items",
      "item",
      "entries",
      "entry",
      "packets",
      "packet",
      "frames",
      "frame",
      "messages",
      "message",
      "connections",
      "connection",
      "clients",
      "client",
      "events",
      "event",
      "bytes",
      "bits",
      "retries",
      "retry",
      "attempts",
      "attempt",
      "chunks",
      "chunk",
      "blocks",
      "block",
  };

  const size_t num_units = sizeof(known_units) / sizeof(known_units[0]);
  for (size_t i = 0; i < num_units; i++) {
    size_t unit_len = strlen(known_units[i]);
    if (unit_len > max_len) {
      continue;
    }
    // Case-insensitive comparison for units
    if (platform_strncasecmp(str, known_units[i], unit_len) == 0 && (unit_len == max_len || !isalpha(str[unit_len]))) {
      return true;
    }
  }

  return false;
}

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

  // Check for fraction format (1/2, 3/4, etc.)
  if (str[i] == '/' && isdigit(str[i + 1])) {
    i++;
    while (isdigit(str[i])) {
      i++;
    }
  }

  // Check for known units with optional spaces (e.g., "25 MB", "1024 GiB", "69.9%")
  // Only include units if they're in the known units list to avoid colorizing random words
  size_t j = i;
  bool has_space = false;

  // First try: units immediately following (no space) like "69.9%"
  if ((isalpha(str[j]) || str[j] == '%') && (j == i || !isalnum(str[j - 1]))) {
    size_t unit_start = j;
    while ((isalpha(str[j]) || str[j] == '%') && (j - unit_start) < 32) {
      j++;
    }
    size_t unit_len = j - unit_start;

    // Check if this is a known unit
    if (is_known_unit(str + unit_start, unit_len)) {
      // It's a known unit immediately following, include it
      i = j;
    } else {
      // Not a known unit, reset j
      j = i;
    }
  }

  // Second try: units with spaces (e.g., "25 MB")
  if (i == j) {
    while (str[j] == ' ' || str[j] == '\t') {
      has_space = true;
      j++;
    }

    if (has_space && (isalpha(str[j]) || str[j] == '%')) {
      // Collect potential unit (up to 32 characters)
      size_t unit_start = j;
      while ((isalpha(str[j]) || str[j] == '%') && (j - unit_start) < 32) {
        j++;
      }
      size_t unit_len = j - unit_start;

      // Check if this is a known unit
      if (is_known_unit(str + unit_start, unit_len)) {
        // It's a known unit, include the space and unit
        i = j;
      }
      // Otherwise, don't include - just the number (i stays as is)
    }
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
    while ((isalnum(str[lookahead]) || str[lookahead] == '_' || str[lookahead] == '-' || str[lookahead] == '.' ||
            str[lookahead] == '/' || str[lookahead] == '\\') &&
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
  while (str[i] != '\0' && str[i] != ' ' && str[i] != '\t' && str[i] != '\n' && str[i] != ':' && str[i] != ',' &&
         str[i] != ')' && str[i] != ']' && str[i] != '}' && str[i] != '"' && str[i] != '\'') {
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
 * @brief Check if character at position is the start of a URL
 *
 * Detects: http://..., https://..., ftp://..., etc.
 *
 * @param str Full string
 * @param pos Current position
 * @param end_pos Output: position after the URL
 * @return true if URL found
 */
static bool is_url(const char *str, size_t pos, size_t *end_pos) {
  // Check for common URL schemes: http, https, ftp, ws, wss
  const char *schemes[] = {"https://", "http://", "ftp://", "wss://", "ws://"};

  // Try each scheme
  for (size_t s = 0; s < sizeof(schemes) / sizeof(schemes[0]); s++) {
    size_t scheme_len = strlen(schemes[s]);
    if (strncmp(str + pos, schemes[s], scheme_len) == 0) {
      // Found the scheme, now collect the URL
      size_t i = pos + scheme_len;

      // URL continues until whitespace, common punctuation, or special chars
      // Stop at: space, tab, newline, ), ], }, ", ', <, >
      while (str[i] != '\0' && str[i] != ' ' && str[i] != '\t' && str[i] != '\n' && str[i] != ')' && str[i] != ']' &&
             str[i] != '}' && str[i] != '"' && str[i] != '\'' && str[i] != '<' && str[i] != '>' && str[i] != ',') {
        i++;
      }

      // Must have at least one character after the scheme
      if (i > pos + scheme_len) {
        *end_pos = i;
        return true;
      }
    }
  }

  return false;
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
 * @brief Determine the color for a value based on its type
 *
 * @param value The value string to analyze
 * @return log_color_t color code for this value
 */
static log_color_t get_value_color(const char *value) {
  if (!value || *value == '\0') {
    return LOG_COLOR_FATAL; // Default to magenta for empty/unknown
  }

  size_t end_pos = 0;

  // Try to detect value type in order of specificity
  if (is_numeric_pattern(value, 0, &end_pos)) {
    return LOG_COLOR_DEBUG; // Cyan for numbers
  }

  if (is_url(value, 0, &end_pos)) {
    return LOG_COLOR_INFO; // Blue for URLs
  }

  if (is_file_path(value, 0, &end_pos)) {
    return LOG_COLOR_FATAL; // Magenta for paths
  }

  if (is_env_var(value, 0, &end_pos)) {
    return LOG_COLOR_GREY; // Grey for environment variables
  }

  // Check for quoted strings or common value patterns
  if ((value[0] == '"' || value[0] == '\'' || value[0] == '`') ||
      strchr(value, ' ') != NULL) { // Space indicates likely string value
    return LOG_COLOR_FATAL;         // Magenta for strings/unknown values
  }

  // Default: magenta for unrecognized values
  return LOG_COLOR_FATAL;
}

/**
 * @brief Check if character at position is the start of a key=value pair
 *
 * Detects: key=value, connection_id=12345, status=ACTIVE, etc.
 * Key must be alphanumeric/underscore. Value extends until space or special chars.
 *
 * @param str Full string
 * @param pos Current position
 * @param key_end Output: position after the key (excluding the equals sign)
 * @param value_start Output: position where the value begins (after = and whitespace)
 * @param value_end Output: position after the value
 * @return true if key=value pair found
 */
static bool is_key_value_pair(const char *str, size_t pos, size_t *key_end, size_t *value_start, size_t *value_end) {
  // Check if we're at the start of an identifier (key)
  if (!isalpha(str[pos]) && str[pos] != '_') {
    return false;
  }

  // Must not be preceded by alphanumeric or underscore (to avoid matching mid-word)
  if (pos > 0 && (isalnum(str[pos - 1]) || str[pos - 1] == '_')) {
    return false;
  }

  size_t i = pos;

  // Collect the key (alphanumeric and underscores)
  while ((isalnum(str[i]) || str[i] == '_') && str[i] != '\0') {
    i++;
  }

  size_t key_len = i - pos;

  // Check for equals sign
  if (str[i] != '=') {
    return false;
  }

  i++; // Skip the equals sign

  // Skip optional whitespace after equals (though unusual)
  while (str[i] == ' ' || str[i] == '\t') {
    i++;
  }

  // Record where the value actually starts
  size_t val_start = i;

  // Value continues until we hit whitespace or special ending characters
  // Stop at: space, tab, newline, comma, semicolon, ), ], }, or null terminator
  while (str[i] != '\0' && str[i] != ' ' && str[i] != '\t' && str[i] != '\n' && str[i] != ',' && str[i] != ';' &&
         str[i] != ')' && str[i] != ']' && str[i] != '}') {
    i++;
  }

  size_t value_len = i - val_start;

  // Must have a non-empty value
  if (value_len == 0) {
    return false;
  }

  *key_end = pos + key_len; // Position after the key
  *value_start = val_start; // Position where value begins
  *value_end = i;           // Position after the value
  return true;
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

  // Check if colors should be used based on TTY status (same as ASCII art)
  // Colors are only applied when output is actually a TTY
  bool should_colorize = terminal_should_color_output(STDOUT_FILENO);

  if (!should_colorize) {
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

    // Check if already colorized - only colorize if NOT already colored
    bool can_colorize = !ansi_is_already_colorized(message, i);

    // Try key=value pair first (highest priority pattern)
    size_t key_end = 0, value_start = 0, value_end = 0;
    if (can_colorize && is_key_value_pair(message, i, &key_end, &value_start, &value_end)) {
      size_t key_len = key_end - i;
      size_t value_len = value_end - value_start;

      // Extract key
      char key_buf[256];
      safe_snprintf(key_buf, sizeof(key_buf), "%.*s", (int)key_len, message + i);

      // Extract value
      char value_buf[512];
      safe_snprintf(value_buf, sizeof(value_buf), "%.*s", (int)value_len, message + value_start);

      // Color the key (magenta)
      const char *colored_key = colored_string(LOG_COLOR_FATAL, key_buf);
      size_t colored_key_len = strlen(colored_key);

      // Check if we have enough space for key + equals + value
      // (accounting for ANSI codes which will be added by colored_string)
      if (out_pos + colored_key_len + 1 + value_len + 100 < max_size) {
        memcpy(output + out_pos, colored_key, colored_key_len);
        out_pos += colored_key_len;

        // Add uncolored equals sign
        output[out_pos++] = '=';

        // Determine value color and add colored value
        log_color_t value_color = get_value_color(value_buf);
        const char *colored_value = colored_string(value_color, value_buf);
        size_t colored_value_len = strlen(colored_value);
        if (out_pos + colored_value_len < max_size) {
          memcpy(output + out_pos, colored_value, colored_value_len);
          out_pos += colored_value_len;
        }
      }
      i = value_end - 1;
      continue;
    }

    // Try numeric pattern
    if (can_colorize && is_numeric_pattern(message, i, &end_pos)) {
      size_t pattern_len = end_pos - i;
      char pattern_buf[256];
      safe_snprintf(pattern_buf, sizeof(pattern_buf), "%.*s", (int)pattern_len, message + i);

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
      safe_snprintf(path_buf, sizeof(path_buf), "%.*s", (int)path_len, message + i);

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
      safe_snprintf(var_buf, sizeof(var_buf), "%.*s", (int)var_len, message + i);

      const char *colored = colored_string(LOG_COLOR_GREY, var_buf);
      size_t colored_len = strlen(colored);
      if (out_pos + colored_len < max_size) {
        memcpy(output + out_pos, colored, colored_len);
        out_pos += colored_len;
      }
      i = end_pos - 1;
      continue;
    }

    // Try URL
    if (can_colorize && is_url(message, i, &end_pos)) {
      size_t url_len = end_pos - i;
      char url_buf[2048];
      safe_snprintf(url_buf, sizeof(url_buf), "%.*s", (int)url_len, message + i);

      const char *colored = colored_string(LOG_COLOR_INFO, url_buf);
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
