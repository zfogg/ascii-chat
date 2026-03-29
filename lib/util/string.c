/**
 * @file util/string.c
 * @ingroup util
 * @brief 🔤 String manipulation utilities: ASCII escaping, trimming, case conversion, and formatting
 */

#include <ascii-chat/util/string.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat-deps/utf8proc/utf8proc.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
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

void truncate_with_ellipsis(const char *input, char *output, size_t output_size, int max_width) {
  if (!input || !output || output_size < 4) {
    if (output && output_size > 0) {
      output[0] = '\0';
    }
    return;
  }

  int content_width = display_width(input);

  // Content fits — copy as-is
  if (content_width <= max_width) {
    size_t len = strlen(input);
    if (len >= output_size) {
      len = output_size - 1;
    }
    memcpy(output, input, len);
    output[len] = '\0';
    return;
  }

  // Need to truncate. Walk forward copying bytes while counting visible
  // columns. ANSI escape sequences are copied whole (zero display width).
  // UTF-8 multi-byte characters are decoded to get their display width
  // (e.g., CJK = 2 columns, emoji = 2 columns) and copied as whole
  // codepoints so we never split a multi-byte sequence.
  const char *src = input;
  const char *src_end = input + strlen(input);
  char *dst = output;
  char *dst_end = output + output_size - 10; // Reserve for reset + ellipsis + NUL
  int cols = 0;
  int target = max_width - 1; // Reserve 1 column for "…"

  if (target < 0) {
    target = 0;
  }

  while (src < src_end && dst < dst_end) {
    if (*src == '\x1b') {
      // Copy the entire escape sequence (zero display width)
      const char *esc_end = ansi_skip_escape(src, src_end);
      size_t esc_len = (size_t)(esc_end - src);
      if (dst + esc_len < dst_end) {
        memcpy(dst, src, esc_len);
        dst += esc_len;
      }
      src = esc_end;
    } else {
      // Decode one UTF-8 codepoint to get its byte length and display width
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t cp_len = utf8proc_iterate((const utf8proc_uint8_t *)src, src_end - src, &codepoint);
      if (cp_len <= 0) {
        // Invalid UTF-8 — copy single byte, count as 1 column
        cp_len = 1;
        codepoint = (unsigned char)*src;
      }

      int char_width = utf8proc_charwidth(codepoint);
      if (char_width < 0) {
        char_width = 0; // Control characters
      }

      if (cols + char_width > target) {
        break;
      }

      // Copy the full codepoint bytes
      if (dst + cp_len < dst_end) {
        memcpy(dst, src, (size_t)cp_len);
        dst += cp_len;
      }
      src += cp_len;
      cols += char_width;
    }
  }

  // Reset colors and append ellipsis (U+2026, 3 bytes)
  size_t remaining = output_size - (size_t)(dst - output);
  int n = snprintf(dst, remaining, "\033[0m…");
  if (n > 0) {
    dst += n;
  }
  *dst = '\0';
}

void strip_ansi_codes(const char *input, char *output, size_t output_size) {
  if (!input || !output || output_size == 0) {
    return;
  }

  size_t out_idx = 0;
  size_t in_idx = 0;

  while (input[in_idx] != '\0' && out_idx < output_size - 1) {
    // Check for ANSI escape sequence: ESC[
    // Standard ANSI codes use escape character (0x1B or \033) followed by [
    if (input[in_idx] == '\033' && input[in_idx + 1] == '[') {
      in_idx += 2; // Skip \033[

      // Skip until we find the terminating letter (m is most common, also H, J, K, etc.)
      // This handles patterns like: 38;5;74m, 0m, 38;2;255;0;0m, 2J, H, etc.
      while (input[in_idx] != '\0') {
        if ((input[in_idx] >= 'A' && input[in_idx] <= 'Z') ||
            (input[in_idx] >= 'a' && input[in_idx] <= 'z')) {
          in_idx++; // Skip the terminating letter
          break;
        }
        in_idx++;
      }
    } else {
      // Regular character (including [ in normal text), copy to output
      output[out_idx++] = input[in_idx++];
    }
  }

  output[out_idx] = '\0';
}
