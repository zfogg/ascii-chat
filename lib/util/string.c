/**
 * @file util/string.c
 * @ingroup util
 * @brief 🔤 String manipulation utilities: ASCII escaping, trimming, case conversion, and formatting
 */

#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/logging.h>
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

  // First pass: validate UTF-8 encoding
  if (!utf8_is_valid(str)) {
    return false;
  }

  // Second pass: validate each character for shell safety
  // Convert to codepoints and check only ASCII range for dangerous characters
  // (non-ASCII characters are allowed by default)
  const uint8_t *p = (const uint8_t *)str;
  while (*p) {
    uint32_t codepoint;
    int decode_len = utf8_decode(p, &codepoint);
    if (decode_len < 0) {
      return false; // Should not happen after utf8_is_valid check
    }

    // Only validate ASCII range where shell metacharacters exist
    // Non-ASCII characters (codepoint > 127) are allowed
    if (codepoint <= 127) {
      unsigned char c = (unsigned char)codepoint;

      // Allow ASCII alphanumeric characters
      if (isalnum(c)) {
        p += decode_len;
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
          p += decode_len;
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

    p += decode_len;
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

bool string_needs_shell_quoting(const char *str) {
  if (!str || str[0] == '\0') {
    return false; // Empty strings don't need quoting
  }

  // Check for characters that need shell quoting
  for (size_t i = 0; str[i] != '\0'; i++) {
    char c = str[i];

    // Whitespace characters always need quoting
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      return true;
    }

    // Shell special characters
    if (c == '"' || c == '\'' || c == '$' || c == '`' || c == '\\' || c == '<' || c == '>' || c == '&' || c == ';' ||
        c == '|' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '*' || c == '?' ||
        c == '!' || c == '~' || c == '#' || c == '@') {
      return true;
    }
  }

  return false;
}

bool escape_path_for_shell(const char *path, char *out_buffer, size_t out_buffer_size) {
  if (!path || !out_buffer || out_buffer_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path or output buffer");
    return false;
  }

  // Check if quoting is needed
  if (!string_needs_shell_quoting(path)) {
    // No quoting needed, just copy the path
    size_t path_len = strlen(path);
    if (out_buffer_size <= path_len) {
      SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small for path");
      return false;
    }
    SAFE_STRNCPY(out_buffer, path, out_buffer_size);
    return true;
  }

  // Quoting is needed, use platform-specific escaping
#ifdef _WIN32
  return escape_shell_double_quotes(path, out_buffer, out_buffer_size);
#else
  return escape_shell_single_quotes(path, out_buffer, out_buffer_size);
#endif
}

// ============================================================================
// String Formatting and Display
// ============================================================================

const char *colored_string(log_color_t color, const char *text) {
#define COLORED_BUFFERS 4
#define COLORED_BUFFER_SIZE 256
  static char buffers[COLORED_BUFFERS][COLORED_BUFFER_SIZE];
  static int buffer_idx = 0;

  if (!text) {
    return "";
  }

  // Check if we should use colors
  // Use global flag that persists even after options cleanup
  extern bool g_color_flag_passed;
  extern bool g_color_flag_value;

  bool use_colors = false;

  // Priority 1: If --color was explicitly passed, force colors
  if (g_color_flag_passed && g_color_flag_value) {
    use_colors = true;
  }
  // Priority 2: If --color NOT explicitly passed, use terminal detection
  else if (!g_color_flag_passed) {
    use_colors = terminal_should_color_output(STDOUT_FILENO);
  }
  // Priority 3: If --color=false was explicitly passed, disable colors
  // (use_colors stays false)

  if (!use_colors) {
    // No colors, just return the text directly
    return text;
  }

  // Use rotating buffer to handle multiple calls in same fprintf
  char *current_buf = buffers[buffer_idx];
  buffer_idx = (buffer_idx + 1) % COLORED_BUFFERS;

  const char *color_code = log_level_color(color);
  const char *reset_code = log_level_color(LOG_COLOR_RESET);

  // Ensure we never pass NULL to snprintf (handle uninitialized color system)
  if (!color_code)
    color_code = "";
  if (!reset_code)
    reset_code = "";

  // Format into rotating static buffer: color_code + text + reset_code
  safe_snprintf(current_buf, COLORED_BUFFER_SIZE, "%s%s%s", color_code, text, reset_code);
  return current_buf;
}
