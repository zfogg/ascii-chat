/**
 * @file ui/interactive_grep.c
 * @brief Interactive grep filtering implementation
 *
 * Implements vim-style `/` grep functionality for terminal screens.
 * Supports full /pattern/flags syntax with real-time filtering.
 */

#include "ascii-chat/ui/interactive_grep.h"
#include "ascii-chat/common.h"
#include "ascii-chat/log/logging.h"
#include "ascii-chat/log/filter.h"
#include "ascii-chat/platform/keyboard.h"
#include "ascii-chat/platform/mutex.h"
#include "ascii-chat/util/pcre2.h"
#include "ascii-chat/util/utf8.h"
#include "ascii-chat/session/session_log_buffer.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* ============================================================================
 * Constants
 * ========================================================================== */

#define MAX_GREP_PATTERNS 32       ///< Maximum number of patterns to support
#define GREP_INPUT_BUFFER_SIZE 256 ///< Input buffer size

/* ============================================================================
 * State Structure
 * ========================================================================== */

typedef struct {
  grep_mode_t mode;
  char input_buffer[GREP_INPUT_BUFFER_SIZE];
  size_t len;
  size_t cursor;

  // For cancel/restore
  char previous_patterns[MAX_GREP_PATTERNS][GREP_INPUT_BUFFER_SIZE];
  int previous_pattern_count;

  // Compiled patterns for display filtering
  pcre2_singleton_t *active_patterns[MAX_GREP_PATTERNS];
  int active_pattern_count;

  // Parsed flags (from /pattern/flags syntax)
  bool case_insensitive;
  bool fixed_string;
  bool global_highlight;
  bool invert_match;
  int context_before;
  int context_after;

  mutex_t mutex;
  _Atomic bool needs_rerender;
  _Atomic bool signal_cancelled; ///< Set by signal handler, checked by render loop
  _Atomic int mode_atomic;       ///< Shadow of mode for signal-safe reads
  bool initialized;
} interactive_grep_state_t;

static interactive_grep_state_t g_grep_state = {
    .mode = GREP_MODE_INACTIVE,
    .len = 0,
    .cursor = 0,
    .previous_pattern_count = 0,
    .active_pattern_count = 0,
    .case_insensitive = false,
    .fixed_string = false,
    .global_highlight = false,
    .invert_match = false,
    .context_before = 0,
    .context_after = 0,
    .needs_rerender = ATOMIC_VAR_INIT(false),
    .signal_cancelled = ATOMIC_VAR_INIT(false),
    .mode_atomic = GREP_MODE_INACTIVE,
    .initialized = false,
};

/* ============================================================================
 * Pattern Parsing (from filter.c)
 * ========================================================================== */

// Forward declaration - we'll use parse_pattern_with_flags from filter.c
// For now, we'll create a simple version inline

typedef struct {
  char pattern[256];
  bool case_insensitive;
  bool fixed_string;
  bool global_flag;
  bool invert;
  int context_before;
  int context_after;
  bool valid;
} parse_result_t;

/**
 * @brief Parse pattern with /pattern/flags format
 * @param input User input string
 * @return Parsed result with pattern and flags
 */
static parse_result_t parse_pattern_with_flags(const char *input) {
  parse_result_t result = {0};

  if (!input || strlen(input) == 0) {
    return result; // Invalid: empty pattern
  }

  size_t len = strlen(input);

  // Check if pattern uses /pattern/flags format
  if (input[0] == '/') {
    // Format 1: /pattern/flags
    if (len < 3) {
      return result; // Invalid: too short for /pattern/ format
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
    const char *flags = closing_slash + 1;

    for (const char *p = flags; *p; p++) {
      char c = *p;

      // Single-character flags
      if (c == 'i') {
        result.case_insensitive = true;
      } else if (c == 'g') {
        result.global_flag = true;
      } else if (c == 'I') {
        result.invert = true;
      } else if (c == 'F') {
        result.fixed_string = true;
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
        // Invalid flag character
        return result; // Invalid
      }
    }
  } else {
    // Format 2: Plain pattern without slashes (treat as regex, no flags)
    size_t pattern_len = len;
    if (pattern_len >= sizeof(result.pattern)) {
      pattern_len = sizeof(result.pattern) - 1;
    }
    memcpy(result.pattern, input, pattern_len);
    result.pattern[pattern_len] = '\0';
  }

  result.valid = true;
  return result;
}

/**
 * @brief Validate PCRE2 pattern (for keyboard validator callback)
 * @param input User input string
 * @return true if valid pattern, false if invalid
 */
static bool validate_pcre2_pattern(const char *input) {
  if (!input || strlen(input) == 0) {
    return true; // Empty is valid (no filtering)
  }

  // Try to parse as /pattern/flags format
  parse_result_t parsed = parse_pattern_with_flags(input);

  if (!parsed.valid) {
    return false; // Invalid format
  }

  // If fixed string, always valid
  if (parsed.fixed_string) {
    return true;
  }

  // Try to compile pattern
  pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, parsed.case_insensitive);
  if (!singleton) {
    return false; // Compilation failed
  }

  // Pattern is valid (singleton cached, will be reused later)
  return true;
}

/* ============================================================================
 * Lifecycle Functions
 * ========================================================================== */

asciichat_error_t interactive_grep_init(void) {
  // Initialize mutex first (before any locking!)
  static bool mutex_inited = false;
  if (!mutex_inited) {
    mutex_init(&g_grep_state.mutex);
    mutex_inited = true;
  }

  mutex_lock(&g_grep_state.mutex);

  if (g_grep_state.initialized) {
    mutex_unlock(&g_grep_state.mutex);
    return ASCIICHAT_OK;
  }

  // Initialize state
  memset(&g_grep_state, 0, sizeof(g_grep_state));
  g_grep_state.mode = GREP_MODE_INACTIVE;
  atomic_store(&g_grep_state.mode_atomic, GREP_MODE_INACTIVE);
  atomic_store(&g_grep_state.needs_rerender, false);
  g_grep_state.initialized = true;

  mutex_unlock(&g_grep_state.mutex);
  return ASCIICHAT_OK;
}

void interactive_grep_destroy(void) {
  mutex_lock(&g_grep_state.mutex);

  if (!g_grep_state.initialized) {
    mutex_unlock(&g_grep_state.mutex);
    return;
  }

  // Free active patterns
  for (int i = 0; i < g_grep_state.active_pattern_count; i++) {
    if (g_grep_state.active_patterns[i]) {
      // Note: pcre2_singleton handles its own reference counting
      g_grep_state.active_patterns[i] = NULL;
    }
  }

  g_grep_state.initialized = false;

  mutex_unlock(&g_grep_state.mutex);
}

/* ============================================================================
 * Mode Management
 * ========================================================================== */

void interactive_grep_enter_mode(void) {
  mutex_lock(&g_grep_state.mutex);

  // Save current patterns (CLI --grep patterns)
  asciichat_error_t result = log_filter_save_patterns();
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to save filter patterns");
  }

  // Clear input buffer
  memset(g_grep_state.input_buffer, 0, sizeof(g_grep_state.input_buffer));
  g_grep_state.len = 0;
  g_grep_state.cursor = 0;

  // Enter input mode
  g_grep_state.mode = GREP_MODE_ENTERING;
  atomic_store(&g_grep_state.mode_atomic, GREP_MODE_ENTERING);
  atomic_store(&g_grep_state.needs_rerender, true);

  mutex_unlock(&g_grep_state.mutex);
}

void interactive_grep_exit_mode(bool accept) {
  mutex_lock(&g_grep_state.mutex);

  if (g_grep_state.mode != GREP_MODE_ENTERING) {
    mutex_unlock(&g_grep_state.mutex);
    return;
  }

  if (!accept) {
    // Cancel - restore previous patterns
    asciichat_error_t result = log_filter_restore_patterns();
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to restore filter patterns");
    }

    // Clear interactive grep patterns
    for (int i = 0; i < g_grep_state.active_pattern_count; i++) {
      g_grep_state.active_patterns[i] = NULL;
    }
    g_grep_state.active_pattern_count = 0;

    g_grep_state.mode = GREP_MODE_INACTIVE;
    atomic_store(&g_grep_state.mode_atomic, GREP_MODE_INACTIVE);
    atomic_store(&g_grep_state.needs_rerender, true);
    mutex_unlock(&g_grep_state.mutex);
    return;
  }

  // Accept - parse and compile pattern
  parse_result_t parsed = parse_pattern_with_flags(g_grep_state.input_buffer);

  if (!parsed.valid) {
    log_error("Invalid pattern format");
    mutex_unlock(&g_grep_state.mutex);
    return; // Stay in input mode
  }

  // Clear old regex patterns
  for (int i = 0; i < g_grep_state.active_pattern_count; i++) {
    g_grep_state.active_patterns[i] = NULL;
  }
  g_grep_state.active_pattern_count = 0;

  // Store parsed flags first
  g_grep_state.case_insensitive = parsed.case_insensitive;
  g_grep_state.fixed_string = parsed.fixed_string;
  g_grep_state.global_highlight = parsed.global_flag;
  g_grep_state.invert_match = parsed.invert;
  g_grep_state.context_before = parsed.context_before;
  g_grep_state.context_after = parsed.context_after;

  // Compile and store new pattern (if not fixed string)
  if (strlen(parsed.pattern) > 0 && !parsed.fixed_string) {
    uint32_t pcre2_flags = 0;
    if (parsed.case_insensitive) {
      pcre2_flags |= PCRE2_CASELESS;
    }
    pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, pcre2_flags);
    if (singleton) {
      // Verify the pattern actually compiles
      pcre2_code *code = asciichat_pcre2_singleton_get_code(singleton);
      if (code) {
        g_grep_state.active_patterns[0] = singleton;
        g_grep_state.active_pattern_count = 1;
      } else {
        // Compilation failed - fall back to fixed string matching
        g_grep_state.fixed_string = true; // Override to use fixed string instead
      }
    } else {
      // Malloc failed - fall back to fixed string
      g_grep_state.fixed_string = true;
    }
  }
  // For fixed strings, pattern stays in input_buffer and fixed_string=true

  g_grep_state.mode = GREP_MODE_ACTIVE;
  atomic_store(&g_grep_state.mode_atomic, GREP_MODE_ACTIVE);
  atomic_store(&g_grep_state.needs_rerender, true);

  mutex_unlock(&g_grep_state.mutex);
}

/* ============================================================================
 * Signal-Safe Interface
 * ========================================================================== */

bool interactive_grep_is_entering_atomic(void) {
  return atomic_load(&g_grep_state.mode_atomic) == GREP_MODE_ENTERING;
}

void interactive_grep_signal_cancel(void) {
  atomic_store(&g_grep_state.signal_cancelled, true);
}

bool interactive_grep_check_signal_cancel(void) {
  return atomic_exchange(&g_grep_state.signal_cancelled, false);
}

bool interactive_grep_is_entering(void) {
  mutex_lock(&g_grep_state.mutex);
  bool result = (g_grep_state.mode == GREP_MODE_ENTERING);
  mutex_unlock(&g_grep_state.mutex);
  return result;
}

bool interactive_grep_is_active(void) {
  mutex_lock(&g_grep_state.mutex);
  bool result = (g_grep_state.mode != GREP_MODE_INACTIVE);
  mutex_unlock(&g_grep_state.mutex);
  return result;
}

/* ============================================================================
 * Keyboard Handling
 * ========================================================================== */

bool interactive_grep_should_handle(int key) {
  mutex_lock(&g_grep_state.mutex);

  // If in input mode, handle all keys
  if (g_grep_state.mode == GREP_MODE_ENTERING) {
    mutex_unlock(&g_grep_state.mutex);
    return true;
  }

  // If not in input mode, only handle '/' to enter mode
  bool should_handle = (key == '/');
  mutex_unlock(&g_grep_state.mutex);
  return should_handle;
}

asciichat_error_t interactive_grep_handle_key(keyboard_key_t key) {
  mutex_lock(&g_grep_state.mutex);

  // If not in input mode, check if user pressed '/'
  if (g_grep_state.mode != GREP_MODE_ENTERING) {
    mutex_unlock(&g_grep_state.mutex);

    // Check for '/' key (key already passed as parameter, don't read again!)
    if (key == '/') {
      interactive_grep_enter_mode();
    }
    return ASCIICHAT_OK;
  }

  // In input mode - use keyboard_read_line_interactive()
  keyboard_line_edit_opts_t opts = {
      .buffer = g_grep_state.input_buffer,
      .max_len = sizeof(g_grep_state.input_buffer),
      .len = &g_grep_state.len,
      .cursor = &g_grep_state.cursor,
      .echo = false,                       // We handle rendering ourselves
      .mask_char = 0,                      // No masking
      .prefix = "/",                       // Show "/" prefix
      .validator = validate_pcre2_pattern, // Live validation
      .key = key                           // Pass the pre-read key
  };

  mutex_unlock(&g_grep_state.mutex);

  keyboard_line_edit_result_t result = keyboard_read_line_interactive(&opts);

  switch (result) {
  case LINE_EDIT_ACCEPTED:
    interactive_grep_exit_mode(true); // Parse and accept pattern
    break;
  case LINE_EDIT_CANCELLED:
    interactive_grep_exit_mode(false); // Restore previous
    break;
  case LINE_EDIT_CONTINUE:
    // Still editing - compile pattern for live filtering
    mutex_lock(&g_grep_state.mutex);

    // If buffer is empty, clear patterns to show all logs (don't exit grep mode)
    if (g_grep_state.len == 0) {
      g_grep_state.active_pattern_count = 0;
      mutex_unlock(&g_grep_state.mutex);
      atomic_store(&g_grep_state.needs_rerender, true);
      break;
    }

    // Parse and compile pattern for live filtering
    parse_result_t parsed = parse_pattern_with_flags(g_grep_state.input_buffer);

    if (!parsed.valid) {
      // Invalid pattern - keep previous patterns active (don't clear)
      mutex_unlock(&g_grep_state.mutex);
      atomic_store(&g_grep_state.needs_rerender, true);
      break;
    }

    // Valid pattern - update filtering state
    if (parsed.fixed_string) {
      // Fixed string matching - we'll handle this in the filter function
      // Store pattern as-is (no regex compilation needed)
      g_grep_state.active_pattern_count = 0; // Clear regex patterns
      g_grep_state.case_insensitive = parsed.case_insensitive;
      g_grep_state.fixed_string = true;
      // Pattern is stored in input_buffer, will be used by filter function
    } else {
      // Regex pattern - compile with PCRE2
      uint32_t pcre2_flags = 0;
      if (parsed.case_insensitive) {
        pcre2_flags |= PCRE2_CASELESS;
      }
      pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, pcre2_flags);
      if (singleton) {
        // Try to get the compiled code to verify it compiles successfully
        pcre2_code *code = asciichat_pcre2_singleton_get_code(singleton);
        if (code) {
          // Compilation successful - use regex pattern
          g_grep_state.active_patterns[0] = singleton;
          g_grep_state.active_pattern_count = 1;
          g_grep_state.case_insensitive = parsed.case_insensitive;
          g_grep_state.fixed_string = false;
        } else {
          // Compilation failed - fall back to fixed string matching
          g_grep_state.active_pattern_count = 0;
          g_grep_state.fixed_string = true; // Fall back to fixed string
          g_grep_state.case_insensitive = parsed.case_insensitive;
        }
      } else {
        // Malloc failed - fall back to fixed string
        g_grep_state.active_pattern_count = 0;
        g_grep_state.fixed_string = true;
        g_grep_state.case_insensitive = parsed.case_insensitive;
      }
    }

    // Store parsed flags
    g_grep_state.global_highlight = parsed.global_flag;
    g_grep_state.invert_match = parsed.invert;
    g_grep_state.context_before = parsed.context_before;
    g_grep_state.context_after = parsed.context_after;

    mutex_unlock(&g_grep_state.mutex);
    atomic_store(&g_grep_state.needs_rerender, true);
    break;
  case LINE_EDIT_NO_INPUT:
    // No input available
    break;
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Log Filtering
 * ========================================================================== */

asciichat_error_t interactive_grep_gather_and_filter_logs(session_log_entry_t **out_entries, size_t *out_count) {
  if (!out_entries || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "out_entries and out_count must not be NULL");
  }

  // For now, just use in-memory buffer (log file tailing in task #3)
  session_log_entry_t *buffer_entries =
      SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
  if (!buffer_entries) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate log buffer");
  }

  size_t buffer_count = session_log_buffer_get_recent(buffer_entries, SESSION_LOG_BUFFER_SIZE);

  // Check if filtering is active (either regex patterns or fixed string)
  mutex_lock(&g_grep_state.mutex);
  bool has_regex_patterns = (g_grep_state.active_pattern_count > 0);
  bool has_fixed_string = (g_grep_state.fixed_string && g_grep_state.len > 0);
  bool filtering_active = has_regex_patterns || has_fixed_string;

  if (!filtering_active) {
    // No filtering active - return all entries
    mutex_unlock(&g_grep_state.mutex);
    *out_entries = buffer_entries;
    *out_count = buffer_count;
    return ASCIICHAT_OK;
  }

  // Filter entries with PCRE2
  session_log_entry_t *filtered =
      SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
  if (!filtered) {
    mutex_unlock(&g_grep_state.mutex);
    SAFE_FREE(buffer_entries);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate filtered buffer");
  }

  size_t filtered_count = 0;

  // Create match data for PCRE2 matching
  pcre2_match_data *match_data = NULL;
  for (int p = 0; p < g_grep_state.active_pattern_count; p++) {
    if (g_grep_state.active_patterns[p]) {
      pcre2_code *code = asciichat_pcre2_singleton_get_code(g_grep_state.active_patterns[p]);
      if (code) {
        match_data = pcre2_match_data_create_from_pattern(code, NULL);
        break;
      }
    }
  }

  if (!match_data && g_grep_state.active_pattern_count > 0) {
    // Couldn't create match data - fall back to returning all entries
    mutex_unlock(&g_grep_state.mutex);
    SAFE_FREE(filtered);
    *out_entries = buffer_entries;
    *out_count = buffer_count;
    return ASCIICHAT_OK;
  }

  // Parse pattern once before loop (for fixed string mode)
  parse_result_t parsed = {0};
  if (has_fixed_string) {
    parsed = parse_pattern_with_flags(g_grep_state.input_buffer);
  }

  for (size_t i = 0; i < buffer_count; i++) {
    const char *message = buffer_entries[i].message;
    size_t message_len = strlen(message);
    bool matches = false;

    // Fixed string matching
    if (has_fixed_string) {
      const char *search_pattern = parsed.pattern;

      if (g_grep_state.case_insensitive) {
        // Unicode-aware case-insensitive fixed string search
        matches = (utf8_strcasestr(message, search_pattern) != NULL);
      } else {
        // Case-sensitive fixed string search
        matches = (strstr(message, search_pattern) != NULL);
      }
    }
    // Regex pattern matching
    else if (has_regex_patterns) {
      // Check against all active patterns (OR logic)
      for (int p = 0; p < g_grep_state.active_pattern_count; p++) {
        pcre2_singleton_t *singleton = g_grep_state.active_patterns[p];
        if (!singleton) {
          continue;
        }

        pcre2_code *code = asciichat_pcre2_singleton_get_code(singleton);
        if (!code) {
          continue;
        }

        // Match against message
        int rc = pcre2_jit_match(code, (PCRE2_SPTR)message, message_len, 0, 0, match_data, NULL);
        if (rc >= 0) {
          matches = true;
          break;
        }
      }
    }

    // Apply invert logic
    if (g_grep_state.invert_match) {
      matches = !matches;
    }

    if (matches) {
      filtered[filtered_count++] = buffer_entries[i];
    }
  }

  // Free match data
  if (match_data) {
    pcre2_match_data_free(match_data);
  }

  mutex_unlock(&g_grep_state.mutex);

  SAFE_FREE(buffer_entries);
  *out_entries = filtered;
  *out_count = filtered_count;
  return ASCIICHAT_OK;
}

/* ============================================================================
 * Display Rendering
 * ========================================================================== */

void interactive_grep_render_input_line(int width) {
  (void)width; // Reserved for future use (line truncation)

  mutex_lock(&g_grep_state.mutex);

  if (g_grep_state.mode != GREP_MODE_ENTERING) {
    mutex_unlock(&g_grep_state.mutex);
    return;
  }

  // Just show slash and pattern (cursor already positioned by caller)
  char output_buf[256];
  int len = snprintf(output_buf, sizeof(output_buf), "/%.*s", (int)g_grep_state.len, g_grep_state.input_buffer);
  if (len > 0) {
    platform_write_all(STDOUT_FILENO, output_buf, len);
  }

  mutex_unlock(&g_grep_state.mutex);
}

/* ============================================================================
 * Re-render Notification
 * ========================================================================== */

bool interactive_grep_needs_rerender(void) {
  bool needs = atomic_load(&g_grep_state.needs_rerender);
  if (needs) {
    atomic_store(&g_grep_state.needs_rerender, false);
  }
  return needs;
}
