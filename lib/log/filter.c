/**
 * @file filter.c
 * @brief Log filtering with PCRE2 regex matching and highlighting
 *
 * Implements terminal-only log filtering:
 * - File logs remain complete (unfiltered)
 * - Terminal shows only matching lines
 * - Matches highlighted with yellow background
 *
 * Thread Safety:
 * - Thread-local match_data via pthread_key_create()
 * - No mutex contention in hot path
 * - Compiled regex is immutable after init
 */

#include <ascii-chat/log/filter.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/platform/terminal.h>

#include <pthread.h>
#include <string.h>

/**
 * @brief Default highlight colors (grey)
 * Dark grey (70) for dark backgrounds, light grey (200) for light backgrounds
 */
#define HIGHLIGHT_DARK_BG 70   // Dark grey for dark backgrounds
#define HIGHLIGHT_LIGHT_BG 200 // Light grey for light backgrounds

/**
 * @brief Minimum color difference threshold (0-255 scale)
 * If background is within this distance of the highlight, use black/white instead
 */
#define MIN_HIGHLIGHT_DISTANCE 40

/**
 * @brief Calculate luminance from RGB (0-255 scale)
 * Uses ITU-R BT.709 formula for relative luminance
 */
static inline float calculate_luminance(uint8_t r, uint8_t g, uint8_t b) {
  return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
}

/**
 * @brief Get highlight color based on terminal background
 * Simple logic: dark background = dark highlight, light background = light highlight
 * Falls back to black (dark) or white (light) if background is too close to the grey
 */
static void get_highlight_color(uint8_t *r, uint8_t *g, uint8_t *b) {
  // Try to query actual terminal background color via OSC 11
  uint8_t bg_r, bg_g, bg_b;
  bool has_bg_color = terminal_query_background_color(&bg_r, &bg_g, &bg_b);

  bool is_dark;
  if (has_bg_color) {
    // Calculate luminance from actual background color
    float luminance = calculate_luminance(bg_r, bg_g, bg_b);
    is_dark = (luminance < 0.5f); // Dark if luminance < 50%
  } else {
    // Fall back to heuristic detection
    is_dark = terminal_has_dark_background();
  }

  // Choose highlight: dark bg = dark highlight, light bg = light highlight
  uint8_t grey = is_dark ? HIGHLIGHT_DARK_BG : HIGHLIGHT_LIGHT_BG;

  // If we have the actual background color, check if it's too close to our grey
  if (has_bg_color) {
    uint8_t bg_grey = (uint8_t)((bg_r + bg_g + bg_b) / 3);
    int distance = abs((int)bg_grey - (int)grey);

    // If background is too close to our grey, use black/white for maximum contrast
    if (distance < MIN_HIGHLIGHT_DISTANCE) {
      grey = is_dark ? 0 : 255; // Black for dark terminals, white for light
    }
  }

  *r = *g = *b = grey;
}

/**
 * @brief Global filter state (initialized once at startup)
 */
static struct {
  pcre2_singleton_t *singleton;   ///< PCRE2 singleton (auto-registered for cleanup)
  const char *pattern;            ///< Original pattern string
  bool enabled;                   ///< Is filtering active?
  bool global_flag;               ///< Global flag (/g) - highlight all matches per line
  pthread_key_t match_data_key;   ///< Thread-local match_data key
  pthread_once_t match_data_once; ///< Initialize key once
} g_filter_state = {
    .singleton = NULL,
    .pattern = NULL,
    .enabled = false,
    .global_flag = false,
    .match_data_once = PTHREAD_ONCE_INIT,
};

/**
 * @brief Destructor for thread-local match_data
 */
static void destroy_match_data(void *data) {
  if (data) {
    pcre2_match_data_free((pcre2_match_data *)data);
  }
}

/**
 * @brief Initialize thread-local storage key (called once)
 */
static void create_match_data_key(void) {
  pthread_key_create(&g_filter_state.match_data_key, destroy_match_data);
}

/**
 * @brief Map character position in plain text to byte position in colored text
 *
 * @param colored_text Original text with ANSI escape codes
 * @param char_pos Character position (ignoring ANSI codes)
 * @return Byte position in colored_text
 */
static size_t map_plain_to_colored_pos(const char *colored_text, size_t char_pos) {
  size_t byte_pos = 0;
  size_t chars_seen = 0;

  while (colored_text[byte_pos] != '\0' && chars_seen < char_pos) {
    // Check for ANSI escape sequence
    if (colored_text[byte_pos] == '\x1b' && colored_text[byte_pos + 1] == '[') {
      // Skip entire ANSI sequence
      byte_pos += 2;
      while (colored_text[byte_pos] != '\0') {
        char c = colored_text[byte_pos];
        byte_pos++;
        // Final byte ends the sequence (0x40-0x7E)
        if (c >= 0x40 && c <= 0x7E) {
          break;
        }
      }
    } else {
      // Regular character
      byte_pos++;
      chars_seen++;
    }
  }

  // Skip past any ANSI sequences at the insertion point
  // This prevents inserting highlight codes right before reset codes like [0m
  while (colored_text[byte_pos] == '\x1b' && colored_text[byte_pos + 1] == '[') {
    byte_pos += 2;
    while (colored_text[byte_pos] != '\0') {
      char c = colored_text[byte_pos];
      byte_pos++;
      // Final byte ends the sequence (0x40-0x7E)
      if (c >= 0x40 && c <= 0x7E) {
        break;
      }
    }
  }

  return byte_pos;
}

/**
 * @brief Get thread-local match_data (lazy allocation)
 */
static pcre2_match_data *get_thread_match_data(void) {
  pthread_once(&g_filter_state.match_data_once, create_match_data_key);

  pcre2_match_data *data = pthread_getspecific(g_filter_state.match_data_key);
  if (!data && g_filter_state.singleton) {
    pcre2_code *code = asciichat_pcre2_singleton_get_code(g_filter_state.singleton);
    if (code) {
      data = pcre2_match_data_create_from_pattern(code, NULL);
      if (data) {
        pthread_setspecific(g_filter_state.match_data_key, data);
      }
    }
  }

  return data;
}

/**
 * @brief Parse regex pattern in /pattern/flags format (e.g., "/my_query/i" or "/test/igm")
 * @param input Input pattern string (must be in /pattern/flags format)
 * @param pattern_out Output buffer for pattern without delimiters and flags
 * @param pattern_size Size of pattern_out buffer
 * @return PCRE2 compile options based on flags, or 0 on format error
 *
 * Format: /pattern/flags where:
 * - Pattern is enclosed in forward slashes
 * - Flags are optional: i (case-insensitive), m (multiline), s (dotall), x (extended), g (global)
 * - Examples: "/test/", "/query/i", "/foo.*bar/igm"
 */
static uint32_t parse_pattern_with_flags(const char *input, char *pattern_out, size_t pattern_size) {
  // Default flags: UTF-8 mode and Unicode character properties (always enabled)
  uint32_t options = PCRE2_UTF | PCRE2_UCP;

  if (!input || strlen(input) == 0) {
    pattern_out[0] = '\0';
    return 0; // Error: empty pattern
  }

  size_t len = strlen(input);

  // Require leading slash
  if (input[0] != '/') {
    return 0; // Error: must start with /
  }

  // Require at least "/x/" (minimum 3 chars)
  if (len < 3) {
    return 0; // Error: pattern too short
  }

  // Find closing slash (search from position 1 onwards)
  const char *closing_slash = strchr(input + 1, '/');
  if (!closing_slash) {
    return 0; // Error: missing closing /
  }

  // Extract pattern between the two slashes
  size_t pattern_len = (size_t)(closing_slash - (input + 1));
  if (pattern_len == 0) {
    return 0; // Error: empty pattern between slashes
  }
  if (pattern_len >= pattern_size) {
    pattern_len = pattern_size - 1;
  }
  memcpy(pattern_out, input + 1, pattern_len);
  pattern_out[pattern_len] = '\0';

  // Parse optional flags after closing slash
  const char *flags = closing_slash + 1;
  size_t flags_len = strlen(flags);

  // Validate that all characters after closing slash are valid flags
  for (size_t i = 0; i < flags_len; i++) {
    char c = flags[i];
    if (c != 'i' && c != 'm' && c != 's' && c != 'x' && c != 'g') {
      return 0; // Error: invalid flag character
    }
  }

  // Apply flags
  for (size_t i = 0; i < flags_len; i++) {
    switch (flags[i]) {
    case 'i': // case-insensitive
      options |= PCRE2_CASELESS;
      break;
    case 'm': // multiline mode (^ and $ match line boundaries)
      options |= PCRE2_MULTILINE;
      break;
    case 's': // dotall mode (. matches newlines)
      options |= PCRE2_DOTALL;
      break;
    case 'x': // extended mode (ignore whitespace and comments)
      options |= PCRE2_EXTENDED;
      break;
    case 'g': // global flag - highlight all matches per line
      // Handled separately in log_filter_init()
      break;
    default:
      // Already validated above, this shouldn't be reached
      log_error("BUG: Invalid flag character '%c' passed validation", flags[i]);
      break;
    }
  }

  return options;
}

asciichat_error_t log_filter_init(const char *pattern) {
  if (!pattern || strlen(pattern) == 0) {
    g_filter_state.enabled = false;
    return ASCIICHAT_OK;
  }

  // Parse pattern and extract flags
  char parsed_pattern[4096];
  uint32_t pcre2_options = parse_pattern_with_flags(pattern, parsed_pattern, sizeof(parsed_pattern));

  // Check for format error (parse function returns 0 on invalid format)
  if (pcre2_options == 0) {
    log_error("Invalid --grep pattern format: \"%s\"", pattern);
    log_error("Use /pattern/flags format, e.g., \"/my_query/ig\" or \"/test/\"");
    g_filter_state.enabled = false;
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid --grep pattern format, use \"/my_query/ig\" format");
  }

  // Check for /g flag to enable global matching (highlight all matches per line)
  const char *last_slash = strrchr(pattern, '/');
  g_filter_state.global_flag = (last_slash && strchr(last_slash + 1, 'g') != NULL);

  // Use singleton pattern (auto-registered for cleanup, includes JIT)
  g_filter_state.singleton = asciichat_pcre2_singleton_compile(parsed_pattern, pcre2_options);

  if (!g_filter_state.singleton) {
    log_warn("Invalid --grep pattern: failed to allocate singleton");
    g_filter_state.enabled = false;
    return ERROR_INVALID_PARAM;
  }

  // Force compilation to validate pattern early
  pcre2_code *code = asciichat_pcre2_singleton_get_code(g_filter_state.singleton);
  if (!code) {
    log_warn("Invalid --grep pattern: compilation failed");
    g_filter_state.enabled = false;
    return ERROR_INVALID_PARAM;
  }

  g_filter_state.pattern = pattern;
  g_filter_state.enabled = true;

  log_info("Log filtering enabled with pattern: %s", pattern);
  return ASCIICHAT_OK;
}

bool log_filter_should_output(const char *log_line, size_t *match_start, size_t *match_len) {
  if (!g_filter_state.enabled || !log_line) {
    return true; // No filtering, output everything
  }

  pcre2_match_data *match_data = get_thread_match_data();
  if (!match_data) {
    // Allocation failed, pass through (log warning once per thread)
    static __thread bool warned = false;
    if (!warned) {
      log_warn("Failed to create thread-local match data for grep filtering");
      warned = true;
    }
    return true;
  }

  pcre2_code *code = asciichat_pcre2_singleton_get_code(g_filter_state.singleton);
  if (!code) {
    return true; // Compilation failed, pass through
  }

  size_t line_len = strlen(log_line);
  int rc = pcre2_jit_match(code, (PCRE2_SPTR)log_line, line_len, 0, 0, match_data, NULL);

  if (rc < 0) {
    return false; // No match, suppress line
  }

  // Extract match position (group 0 = entire match)
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  *match_start = (size_t)ovector[0];
  *match_len = (size_t)(ovector[1] - ovector[0]);

  return true; // Match found, output with highlighting
}

const char *log_filter_highlight_colored(const char *colored_text, const char *plain_text, size_t match_start,
                                         size_t match_len) {
  static __thread char highlight_buffer[16384];

  if (!colored_text || !plain_text || match_len == 0) {
    return colored_text;
  }

  // If global flag is set, find and highlight ALL matches
  if (g_filter_state.global_flag && g_filter_state.singleton) {
    pcre2_code *code = asciichat_pcre2_singleton_get_code(g_filter_state.singleton);
    pcre2_match_data *match_data = get_thread_match_data();

    if (!code || !match_data) {
      return colored_text; // Fall back to no highlighting
    }

    size_t plain_len = strlen(plain_text);
    size_t colored_len = strlen(colored_text);
    char *dst = highlight_buffer;
    size_t plain_offset = 0;
    size_t colored_pos = 0;

    // Find all matches in plain text and highlight in colored text
    while (plain_offset < plain_len) {
      int rc = pcre2_jit_match(code, (PCRE2_SPTR)plain_text, plain_len, plain_offset, 0, match_data, NULL);
      if (rc < 0) {
        break; // No more matches
      }

      PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
      size_t plain_match_start = (size_t)ovector[0];
      size_t plain_match_end = (size_t)ovector[1];

      // Map to colored text positions
      size_t colored_match_start = map_plain_to_colored_pos(colored_text, plain_match_start);
      size_t colored_match_end = map_plain_to_colored_pos(colored_text, plain_match_end);

      // Copy text before match
      if (colored_match_start > colored_pos) {
        memcpy(dst, colored_text + colored_pos, colored_match_start - colored_pos);
        dst += (colored_match_start - colored_pos);
      }

      // Add highlight background
      uint8_t r, g, b;
      get_highlight_color(&r, &g, &b);
      dst = append_truecolor_bg(dst, r, g, b);

      // Copy matched text, re-applying background after any [0m or [00m reset codes
      size_t match_byte_len = colored_match_end - colored_match_start;
      const char *match_src = colored_text + colored_match_start;
      char *dst_end = highlight_buffer + sizeof(highlight_buffer) - 50; // Leave room for final codes
      size_t i = 0;
      while (i < match_byte_len && dst < dst_end) {
        // Check for [0m or [00m reset codes
        if (i + 4 <= match_byte_len && match_src[i] == '\x1b' && match_src[i + 1] == '[' && match_src[i + 2] == '0' &&
            match_src[i + 3] == 'm') {
          // Found [0m - copy it and re-apply background
          if (dst + 23 >= dst_end)
            break;                 // Need room for [0m + background code
          *dst++ = match_src[i++]; // ESC
          *dst++ = match_src[i++]; // '['
          *dst++ = match_src[i++]; // '0'
          *dst++ = match_src[i++]; // 'm'
          // Re-apply highlight background after reset
          uint8_t r, g, b;
          get_highlight_color(&r, &g, &b);
          dst = append_truecolor_bg(dst, r, g, b);
        } else if (i + 5 <= match_byte_len && match_src[i] == '\x1b' && match_src[i + 1] == '[' &&
                   match_src[i + 2] == '0' && match_src[i + 3] == '0' && match_src[i + 4] == 'm') {
          // Found [00m - copy it and re-apply background
          if (dst + 24 >= dst_end)
            break;                 // Need room for [00m + background code
          *dst++ = match_src[i++]; // ESC
          *dst++ = match_src[i++]; // '['
          *dst++ = match_src[i++]; // '0'
          *dst++ = match_src[i++]; // '0'
          *dst++ = match_src[i++]; // 'm'
          // Re-apply highlight background after reset
          uint8_t r, g, b;
          get_highlight_color(&r, &g, &b);
          dst = append_truecolor_bg(dst, r, g, b);
        } else {
          // Regular character - just copy
          if (dst + 1 >= dst_end)
            break;
          *dst++ = match_src[i++];
        }
      }

      // Reset background only
      memcpy(dst, "\x1b[49m", 5);
      dst += 5;

      colored_pos = colored_match_end;
      plain_offset = plain_match_end;

      // Prevent infinite loop on zero-length matches
      if (plain_match_end == plain_match_start) {
        plain_offset++;
      }
    }

    // Copy remaining text
    if (colored_pos < colored_len) {
      memcpy(dst, colored_text + colored_pos, colored_len - colored_pos);
      dst += (colored_len - colored_pos);
    }

    *dst = '\0';
    return highlight_buffer;
  }

  // Single match highlighting (original behavior without /g)
  size_t colored_start = map_plain_to_colored_pos(colored_text, match_start);
  size_t colored_end = map_plain_to_colored_pos(colored_text, match_start + match_len);

  size_t colored_len = strlen(colored_text);
  char *dst = highlight_buffer;

  // Copy text before match
  if (colored_start > 0) {
    memcpy(dst, colored_text, colored_start);
    dst += colored_start;
  }

  // Add highlight background
  uint8_t r, g, b;
  get_highlight_color(&r, &g, &b);
  dst = append_truecolor_bg(dst, r, g, b);

  // Copy matched text, re-applying background after any [0m or [00m reset codes
  size_t match_byte_len = colored_end - colored_start;
  const char *match_src = colored_text + colored_start;
  char *dst_end = highlight_buffer + sizeof(highlight_buffer) - 50; // Leave room for final codes
  size_t i = 0;
  while (i < match_byte_len && dst < dst_end) {
    // Check for [0m or [00m reset codes
    if (i + 4 <= match_byte_len && match_src[i] == '\x1b' && match_src[i + 1] == '[' && match_src[i + 2] == '0' &&
        match_src[i + 3] == 'm') {
      // Found [0m - copy it and re-apply background
      if (dst + 23 >= dst_end)
        break;                 // Need room for [0m + background code
      *dst++ = match_src[i++]; // ESC
      *dst++ = match_src[i++]; // '['
      *dst++ = match_src[i++]; // '0'
      *dst++ = match_src[i++]; // 'm'
      // Re-apply highlight background after reset
      uint8_t r2, g2, b2;
      get_highlight_color(&r2, &g2, &b2);
      dst = append_truecolor_bg(dst, r2, g2, b2);
    } else if (i + 5 <= match_byte_len && match_src[i] == '\x1b' && match_src[i + 1] == '[' &&
               match_src[i + 2] == '0' && match_src[i + 3] == '0' && match_src[i + 4] == 'm') {
      // Found [00m - copy it and re-apply background
      if (dst + 24 >= dst_end)
        break;                 // Need room for [00m + background code
      *dst++ = match_src[i++]; // ESC
      *dst++ = match_src[i++]; // '['
      *dst++ = match_src[i++]; // '0'
      *dst++ = match_src[i++]; // '0'
      *dst++ = match_src[i++]; // 'm'
      // Re-apply highlight background after reset
      uint8_t r3, g3, b3;
      get_highlight_color(&r3, &g3, &b3);
      dst = append_truecolor_bg(dst, r3, g3, b3);
    } else {
      // Regular character - just copy
      if (dst + 1 >= dst_end)
        break;
      *dst++ = match_src[i++];
    }
  }

  // Reset background only
  memcpy(dst, "\x1b[49m", 5);
  dst += 5;

  // Copy remaining text
  size_t remaining = colored_len - colored_end;
  if (remaining > 0) {
    memcpy(dst, colored_text + colored_end, remaining);
    dst += remaining;
  }

  *dst = '\0';
  return highlight_buffer;
}

const char *log_filter_highlight(const char *log_line, size_t match_start, size_t match_len) {
  static __thread char highlight_buffer[16384];

  if (!log_line || match_len == 0) {
    return log_line; // No highlighting needed
  }

  size_t line_len = strlen(log_line);
  if (match_start + match_len > line_len) {
    return log_line; // Invalid range
  }

  char *dst = highlight_buffer;
  const char *src = log_line;
  size_t pos = 0;

  // If global flag is set, find and highlight ALL matches
  if (g_filter_state.global_flag) {
    pcre2_code *code = asciichat_pcre2_singleton_get_code(g_filter_state.singleton);
    pcre2_match_data *match_data = get_thread_match_data();

    if (!code || !match_data) {
      return log_line; // Fall back to no highlighting
    }

    // Find all matches in the line
    size_t offset = 0;
    while (offset < line_len) {
      int rc = pcre2_jit_match(code, (PCRE2_SPTR)log_line, line_len, offset, 0, match_data, NULL);
      if (rc < 0) {
        break; // No more matches
      }

      PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
      size_t match_pos = (size_t)ovector[0];
      size_t match_end = (size_t)ovector[1];
      size_t len = match_end - match_pos;

      // Copy text before match
      if (match_pos > pos) {
        memcpy(dst, src + pos, match_pos - pos);
        dst += (match_pos - pos);
      }

      // Highlight the match (background only, preserve foreground color)
      uint8_t r, g, b;
      get_highlight_color(&r, &g, &b);
      dst = append_truecolor_bg(dst, r, g, b);
      memcpy(dst, src + match_pos, len);
      dst += len;
      memcpy(dst, "\x1b[0m", 4);
      dst += 4;

      pos = match_end;
      offset = match_end;

      // Prevent infinite loop on zero-length matches
      if (len == 0) {
        offset++;
      }
    }

    // Copy remaining text after last match
    if (pos < line_len) {
      memcpy(dst, src + pos, line_len - pos);
      dst += (line_len - pos);
    }

  } else {
    // Single match highlighting (original behavior)
    // Copy text before match
    if (match_start > 0) {
      memcpy(dst, log_line, match_start);
      dst += match_start;
    }

    // Add background color for match (preserve foreground color)
    uint8_t r, g, b;
    get_highlight_color(&r, &g, &b);
    dst = append_truecolor_bg(dst, r, g, b);

    // Copy matched text
    memcpy(dst, log_line + match_start, match_len);
    dst += match_len;

    // Reset color
    memcpy(dst, "\x1b[0m", 4);
    dst += 4;

    // Copy text after match
    size_t remaining = line_len - (match_start + match_len);
    if (remaining > 0) {
      memcpy(dst, log_line + match_start + match_len, remaining);
      dst += remaining;
    }
  }

  *dst = '\0';
  return highlight_buffer;
}

void log_filter_destroy(void) {
  // Singleton is auto-cleaned by asciichat_pcre2_cleanup_all() in common.c
  g_filter_state.singleton = NULL;
  g_filter_state.pattern = NULL;
  g_filter_state.enabled = false;
  g_filter_state.global_flag = false;

  // Note: Thread-local match_data is cleaned up via pthread_key destructor
}
