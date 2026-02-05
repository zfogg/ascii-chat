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
 * @brief Global filter state (initialized once at startup)
 */
static struct {
  pcre2_singleton_t *singleton;   ///< PCRE2 singleton (auto-registered for cleanup)
  const char *pattern;            ///< Original pattern string
  bool enabled;                   ///< Is filtering active?
  pthread_key_t match_data_key;   ///< Thread-local match_data key
  pthread_once_t match_data_once; ///< Initialize key once
} g_filter_state = {
    .singleton = NULL,
    .pattern = NULL,
    .enabled = false,
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

asciichat_error_t log_filter_init(const char *pattern) {
  if (!pattern || strlen(pattern) == 0) {
    g_filter_state.enabled = false;
    return ASCIICHAT_OK;
  }

  // Use singleton pattern (auto-registered for cleanup, includes JIT)
  g_filter_state.singleton =
      asciichat_pcre2_singleton_compile(pattern, PCRE2_CASELESS | PCRE2_UTF | PCRE2_MULTILINE | PCRE2_UCP);

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

  // Copy text before match
  if (match_start > 0) {
    memcpy(dst, log_line, match_start);
    dst += match_start;
  }

  // Add background color for match
  // Yellow background (255, 200, 0) + black foreground (0, 0, 0)
  dst = append_truecolor_fg_bg(dst, 0, 0, 0, 255, 200, 0);

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

  *dst = '\0';
  return highlight_buffer;
}

void log_filter_destroy(void) {
  // Singleton is auto-cleaned by asciichat_pcre2_cleanup_all() in common.c
  g_filter_state.singleton = NULL;
  g_filter_state.pattern = NULL;
  g_filter_state.enabled = false;

  // Note: Thread-local match_data is cleaned up via pthread_key destructor
}
