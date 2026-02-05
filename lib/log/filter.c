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
 * @brief Single filter pattern with all its settings
 */
typedef struct {
  const char *original;         ///< Original pattern string (for display)
  char *parsed_pattern;         ///< Parsed pattern (without delimiters/flags)
  pcre2_singleton_t *singleton; ///< PCRE2 singleton (NULL if fixed string)
  bool is_fixed_string;         ///< True for fixed string matching (no regex)
  bool invert;                  ///< Invert match (I flag)
  bool global_flag;             ///< Highlight all matches (g flag)
  int context_before;           ///< Lines before match (B flag)
  int context_after;            ///< Lines after match (A flag)
} log_filter_pattern_t;

/**
 * @brief Global filter state (supports multiple patterns ORed together)
 */
static struct {
  log_filter_pattern_t *patterns; ///< Array of patterns
  int pattern_count;              ///< Number of active patterns
  int pattern_capacity;           ///< Allocated capacity
  bool enabled;                   ///< Is filtering active?
  pthread_key_t match_data_key;   ///< Thread-local match_data key
  pthread_once_t match_data_once; ///< Initialize key once

  // Context line buffering
  char **line_buffer;    ///< Circular buffer for context_before
  int buffer_size;       ///< Size of circular buffer
  int buffer_pos;        ///< Current position in buffer
  int lines_after_match; ///< Counter for context_after lines
  int max_context_after; ///< Maximum context_after across all patterns
} g_filter_state = {
    .patterns = NULL,
    .pattern_count = 0,
    .pattern_capacity = 0,
    .enabled = false,
    .match_data_once = PTHREAD_ONCE_INIT,
    .line_buffer = NULL,
    .buffer_size = 0,
    .buffer_pos = 0,
    .lines_after_match = 0,
    .max_context_after = 0,
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
  if (!data) {
    // Find any regex pattern to create match data from
    for (int i = 0; i < g_filter_state.pattern_count; i++) {
      if (g_filter_state.patterns[i].singleton) {
        pcre2_code *code = asciichat_pcre2_singleton_get_code(g_filter_state.patterns[i].singleton);
        if (code) {
          data = pcre2_match_data_create_from_pattern(code, NULL);
          if (data) {
            pthread_setspecific(g_filter_state.match_data_key, data);
            break;
          }
        }
      }
    }
  }

  return data;
}

/**
 * @brief Parsed pattern result
 */
typedef struct {
  char pattern[4096];     ///< Parsed pattern string
  uint32_t pcre2_options; ///< PCRE2 compile options
  bool is_fixed_string;   ///< True if fixed string (not regex)
  bool invert;            ///< Invert match (I flag)
  bool global_flag;       ///< Highlight all matches (g flag)
  int context_before;     ///< Lines before match (B<n> flag)
  int context_after;      ///< Lines after match (A<n> flag)
  bool valid;             ///< True if parsing succeeded
} parse_result_t;

/**
 * @brief Parse pattern in /pattern/flags format
 * @param input Input pattern string (must use /pattern/flags format)
 * @return Parse result with all extracted settings
 *
 * Format: /pattern/flags
 * - All patterns must be enclosed in forward slashes
 * - Use F flag for fixed string (literal) matching
 *
 * Flags:
 * - i: case-insensitive
 * - m: multiline mode (regex only)
 * - s: dotall mode (regex only)
 * - x: extended mode (regex only)
 * - g: global (highlight all matches)
 * - I: invert match (show non-matching lines)
 * - F: fixed string (literal match, no regex)
 * - A<n>: show n lines after match (e.g., A3)
 * - B<n>: show n lines before match (e.g., B2)
 * - C<n>: show n lines before and after match (e.g., C5)
 *
 * Examples:
 * - "/test/" - Simple regex
 * - "/query/i" - Case-insensitive regex
 * - "/test/F" - Fixed string match for "test"
 * - "/api/v1/users/F" - Fixed string match for "api/v1/users"
 * - "/ERROR/IA3" - Invert match + 3 lines after
 * - "/FATAL/B2A5F" - Fixed string, 2 before, 5 after
 */
static parse_result_t parse_pattern_with_flags(const char *input) {
  parse_result_t result = {0};
  result.pcre2_options = PCRE2_UTF | PCRE2_UCP; // Default: UTF-8 mode

  if (!input || strlen(input) == 0) {
    return result; // Invalid: empty pattern
  }

  size_t len = strlen(input);

  // Require /pattern/flags format
  if (input[0] != '/') {
    return result; // Invalid: must start with /
  }

  // Regex or fixed string format: /pattern/flags
  if (len < 3) {
    return result; // Invalid: too short
  }

  // Find closing slash
  const char *closing_slash = strchr(input + 1, '/');
  if (!closing_slash) {
    return result; // Invalid: missing closing /
  }

  // Extract pattern between slashes
  size_t pattern_len = (size_t)(closing_slash - (input + 1));
  if (pattern_len == 0) {
    return result; // Invalid: empty pattern
  }
  if (pattern_len >= sizeof(result.pattern)) {
    pattern_len = sizeof(result.pattern) - 1;
  }
  memcpy(result.pattern, input + 1, pattern_len);
  result.pattern[pattern_len] = '\0';

  // Parse flags after closing slash
  // First pass: check for F flag
  const char *flags = closing_slash + 1;
  bool has_F_flag = (strchr(flags, 'F') != NULL);

  // Parse all flags
  for (const char *p = flags; *p; p++) {
    char c = *p;

    // Single-character flags
    if (c == 'i') {
      result.pcre2_options |= PCRE2_CASELESS;
    } else if (c == 'm') {
      result.pcre2_options |= PCRE2_MULTILINE;
    } else if (c == 's') {
      result.pcre2_options |= PCRE2_DOTALL;
    } else if (c == 'x') {
      result.pcre2_options |= PCRE2_EXTENDED;
    } else if (c == 'g') {
      result.global_flag = true;
    } else if (c == 'I') {
      result.invert = true;
    } else if (c == 'F') {
      result.is_fixed_string = true;
    }
    // Multi-character flags with integers
    else if (c == 'A') {
      p++; // Move to digits
      int num = 0;
      while (*p >= '0' && *p <= '9') {
        num = num * 10 + (*p - '0');
        p++;
      }
      p--;                                        // Back up one (for loop will increment)
      result.context_after = (num > 0) ? num : 1; // Default to 1 if no number
    } else if (c == 'B') {
      p++;
      int num = 0;
      while (*p >= '0' && *p <= '9') {
        num = num * 10 + (*p - '0');
        p++;
      }
      p--;
      result.context_before = (num > 0) ? num : 1;
    } else if (c == 'C') {
      p++;
      int num = 0;
      while (*p >= '0' && *p <= '9') {
        num = num * 10 + (*p - '0');
        p++;
      }
      p--;
      int ctx = (num > 0) ? num : 1;
      result.context_before = ctx;
      result.context_after = ctx;
    } else {
      // Invalid flag character - only error if not using F flag
      if (!has_F_flag) {
        return result; // Invalid
      }
      // Otherwise ignore invalid flags (they're part of the fixed string context)
    }
  }

  result.valid = true;
  return result;
}

asciichat_error_t log_filter_init(const char *pattern) {
  if (!pattern || strlen(pattern) == 0) {
    return ASCIICHAT_OK;
  }

  // Parse pattern
  parse_result_t parsed = parse_pattern_with_flags(pattern);
  if (!parsed.valid) {
    log_error("Invalid --grep pattern format: \"%s\"", pattern);
    log_error("Use /pattern/flags format (e.g., \"/query/ig\") or literal string (e.g., \"my literal string\")");
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid --grep pattern format");
  }

  // Allocate or expand pattern array
  if (g_filter_state.pattern_count >= g_filter_state.pattern_capacity) {
    int new_capacity = (g_filter_state.pattern_capacity == 0) ? 4 : g_filter_state.pattern_capacity * 2;
    log_filter_pattern_t *new_patterns =
        SAFE_REALLOC(g_filter_state.patterns, new_capacity * sizeof(log_filter_pattern_t), log_filter_pattern_t *);
    if (!new_patterns) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate pattern array");
    }
    g_filter_state.patterns = new_patterns;
    g_filter_state.pattern_capacity = new_capacity;
  }

  // Add new pattern
  log_filter_pattern_t *new_pat = &g_filter_state.patterns[g_filter_state.pattern_count];
  memset(new_pat, 0, sizeof(*new_pat));

  new_pat->original = pattern;
  new_pat->parsed_pattern = SAFE_MALLOC(strlen(parsed.pattern) + 1, char *);
  if (!new_pat->parsed_pattern) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate pattern string");
  }
  strcpy(new_pat->parsed_pattern, parsed.pattern);

  new_pat->is_fixed_string = parsed.is_fixed_string;
  new_pat->invert = parsed.invert;
  new_pat->global_flag = parsed.global_flag;
  new_pat->context_before = parsed.context_before;
  new_pat->context_after = parsed.context_after;

  // Compile regex (skip for fixed strings)
  if (!parsed.is_fixed_string) {
    new_pat->singleton = asciichat_pcre2_singleton_compile(parsed.pattern, parsed.pcre2_options);
    if (!new_pat->singleton) {
      SAFE_FREE(new_pat->parsed_pattern);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to compile regex pattern");
    }

    // Validate compilation
    pcre2_code *code = asciichat_pcre2_singleton_get_code(new_pat->singleton);
    if (!code) {
      SAFE_FREE(new_pat->parsed_pattern);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Regex compilation failed");
    }
  }

  g_filter_state.pattern_count++;

  // Update context buffer size if needed
  if (parsed.context_before > g_filter_state.buffer_size) {
    // Reallocate line buffer
    char **new_buffer = SAFE_REALLOC(g_filter_state.line_buffer, parsed.context_before * sizeof(char *), char **);
    if (!new_buffer) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate context line buffer");
    }
    // Initialize new slots
    for (int i = g_filter_state.buffer_size; i < parsed.context_before; i++) {
      new_buffer[i] = NULL;
    }
    g_filter_state.line_buffer = new_buffer;
    g_filter_state.buffer_size = parsed.context_before;
  }

  // Update max context_after
  if (parsed.context_after > g_filter_state.max_context_after) {
    g_filter_state.max_context_after = parsed.context_after;
  }

  g_filter_state.enabled = true;
  log_info("Added --grep pattern #%d: %s", g_filter_state.pattern_count, pattern);

  return ASCIICHAT_OK;
}

bool log_filter_should_output(const char *log_line, size_t *match_start, size_t *match_len) {
  if (!g_filter_state.enabled || !log_line) {
    return true; // No filtering, output everything
  }

  // Check if we're in "context after" mode (outputting lines after a match)
  if (g_filter_state.lines_after_match > 0) {
    g_filter_state.lines_after_match--;
    *match_start = 0;
    *match_len = 0;

    // Store in buffer even during context-after mode
    if (g_filter_state.buffer_size > 0) {
      SAFE_FREE(g_filter_state.line_buffer[g_filter_state.buffer_pos]);
      g_filter_state.line_buffer[g_filter_state.buffer_pos] = SAFE_MALLOC(strlen(log_line) + 1, char *);
      if (g_filter_state.line_buffer[g_filter_state.buffer_pos]) {
        strcpy(g_filter_state.line_buffer[g_filter_state.buffer_pos], log_line);
      }
      g_filter_state.buffer_pos = (g_filter_state.buffer_pos + 1) % g_filter_state.buffer_size;
    }

    return true; // Output context line
  }

  pcre2_match_data *match_data = get_thread_match_data();
  size_t line_len = strlen(log_line);

  // Try each pattern (OR logic)
  bool any_match = false;
  log_filter_pattern_t *matched_pattern = NULL;

  for (int i = 0; i < g_filter_state.pattern_count; i++) {
    log_filter_pattern_t *pat = &g_filter_state.patterns[i];
    bool this_match = false;

    if (pat->is_fixed_string) {
      // Fixed string matching
      const char *found = strstr(log_line, pat->parsed_pattern);
      if (found) {
        this_match = true;
        if (!matched_pattern) {
          *match_start = (size_t)(found - log_line);
          *match_len = strlen(pat->parsed_pattern);
          matched_pattern = pat;
        }
      }
    } else {
      // Regex matching
      if (!match_data || !pat->singleton) {
        continue;
      }

      pcre2_code *code = asciichat_pcre2_singleton_get_code(pat->singleton);
      if (!code) {
        continue;
      }

      int rc = pcre2_jit_match(code, (PCRE2_SPTR)log_line, line_len, 0, 0, match_data, NULL);
      if (rc >= 0) {
        this_match = true;
        if (!matched_pattern) {
          PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
          *match_start = (size_t)ovector[0];
          *match_len = (size_t)(ovector[1] - ovector[0]);
          matched_pattern = pat;
        }
      }
    }

    // Apply invert flag
    if (pat->invert) {
      this_match = !this_match;
    }

    if (this_match) {
      any_match = true;
      // Update matched_pattern if this one has more context
      if (!matched_pattern || (pat->context_before + pat->context_after) >
                                  (matched_pattern->context_before + matched_pattern->context_after)) {
        matched_pattern = pat;
      }
    }
  }

  // If any pattern matched
  if (any_match && matched_pattern) {
    // Output context_before lines from circular buffer
    if (matched_pattern->context_before > 0 && g_filter_state.buffer_size > 0) {
      int num_lines = (matched_pattern->context_before < g_filter_state.buffer_size) ? matched_pattern->context_before
                                                                                     : g_filter_state.buffer_size;

      // Calculate starting position in circular buffer
      int start_pos = (g_filter_state.buffer_pos - num_lines + g_filter_state.buffer_size) % g_filter_state.buffer_size;

      // Output buffered lines
      for (int i = 0; i < num_lines; i++) {
        int pos = (start_pos + i) % g_filter_state.buffer_size;
        if (g_filter_state.line_buffer[pos]) {
          fprintf(stderr, "%s\n", g_filter_state.line_buffer[pos]);
        }
      }
    }

    // Set up context_after counter
    if (matched_pattern->context_after > 0) {
      g_filter_state.lines_after_match = matched_pattern->context_after;
    }

    return true; // Match found, output line
  }

  // No match - store in context buffer for potential future use
  if (g_filter_state.buffer_size > 0) {
    SAFE_FREE(g_filter_state.line_buffer[g_filter_state.buffer_pos]);
    g_filter_state.line_buffer[g_filter_state.buffer_pos] = SAFE_MALLOC(strlen(log_line) + 1, char *);
    if (g_filter_state.line_buffer[g_filter_state.buffer_pos]) {
      strcpy(g_filter_state.line_buffer[g_filter_state.buffer_pos], log_line);
    }
    g_filter_state.buffer_pos = (g_filter_state.buffer_pos + 1) % g_filter_state.buffer_size;
  }

  return false; // No match, suppress line
}

const char *log_filter_highlight_colored(const char *colored_text, const char *plain_text, size_t match_start,
                                         size_t match_len) {
  static __thread char highlight_buffer[16384];

  if (!colored_text || !plain_text || match_len == 0) {
    return colored_text;
  }

  // If any pattern has global flag, highlight ALL matches for that pattern
  // Find first pattern with global flag
  log_filter_pattern_t *global_pat = NULL;
  for (int i = 0; i < g_filter_state.pattern_count; i++) {
    if (g_filter_state.patterns[i].global_flag && !g_filter_state.patterns[i].is_fixed_string) {
      global_pat = &g_filter_state.patterns[i];
      break;
    }
  }

  if (global_pat && global_pat->singleton) {
    pcre2_code *code = asciichat_pcre2_singleton_get_code(global_pat->singleton);
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

  // If any pattern has global flag, highlight ALL matches
  log_filter_pattern_t *global_pat = NULL;
  for (int i = 0; i < g_filter_state.pattern_count; i++) {
    if (g_filter_state.patterns[i].global_flag && !g_filter_state.patterns[i].is_fixed_string) {
      global_pat = &g_filter_state.patterns[i];
      break;
    }
  }

  if (global_pat && global_pat->singleton) {
    pcre2_code *code = asciichat_pcre2_singleton_get_code(global_pat->singleton);
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
  // Free all patterns
  for (int i = 0; i < g_filter_state.pattern_count; i++) {
    log_filter_pattern_t *pat = &g_filter_state.patterns[i];
    SAFE_FREE(pat->parsed_pattern);
    // Singletons are auto-cleaned by asciichat_pcre2_cleanup_all()
    pat->singleton = NULL;
  }
  SAFE_FREE(g_filter_state.patterns);
  g_filter_state.patterns = NULL; // Make idempotent

  // Free context buffer
  if (g_filter_state.line_buffer) {
    for (int i = 0; i < g_filter_state.buffer_size; i++) {
      SAFE_FREE(g_filter_state.line_buffer[i]);
    }
    SAFE_FREE(g_filter_state.line_buffer);
    g_filter_state.line_buffer = NULL; // Make idempotent
  }

  g_filter_state.pattern_count = 0;
  g_filter_state.pattern_capacity = 0;
  g_filter_state.buffer_size = 0;
  g_filter_state.buffer_pos = 0;
  g_filter_state.lines_after_match = 0;
  g_filter_state.max_context_after = 0;
  g_filter_state.enabled = false;

  // Note: Thread-local match_data is cleaned up via pthread_key destructor
}
