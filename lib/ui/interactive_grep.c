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
#include "ascii-chat/logging/file_parser.h"
#include "ascii-chat/session/session_log_buffer.h"
#include "ascii-chat/options/options.h"
#include "ascii-chat/video/ansi.h"

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
 * Pattern Validation
 * ========================================================================== */

/**
 * @brief Validate PCRE2 pattern (for keyboard validator callback)
 * @param input User input string
 * @return true if valid pattern, false if invalid
 */
static bool validate_pcre2_pattern(const char *input) {
  if (!input || strlen(input) == 0) {
    return true; // Empty is valid (no filtering)
  }

  // Use the shared parser from filter.c
  log_filter_parse_result_t parsed = log_filter_parse_pattern(input);

  if (!parsed.valid) {
    return false; // Invalid format
  }

  // If fixed string, always valid
  if (parsed.is_fixed_string) {
    return true;
  }

  // Try to compile pattern with full pcre2_options (includes UTF, UCP, CASELESS, etc.)
  pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, parsed.pcre2_options);
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

  // Initialize state (careful not to destroy the mutex!)
  // Save the mutex before clearing state
  mutex_t saved_mutex = g_grep_state.mutex;
  memset(&g_grep_state, 0, sizeof(g_grep_state));
  // Restore the mutex
  g_grep_state.mutex = saved_mutex;

  // Set default pattern to "DEBUG" for testing
  strncpy(g_grep_state.input_buffer, "DEBUG", GREP_INPUT_BUFFER_SIZE - 1);
  g_grep_state.len = strlen("DEBUG");
  g_grep_state.cursor = g_grep_state.len;
  g_grep_state.mode = GREP_MODE_ACTIVE;
  atomic_store(&g_grep_state.mode_atomic, GREP_MODE_ACTIVE);

  // Compile the default DEBUG pattern
  log_filter_parse_result_t parsed = log_filter_parse_pattern("DEBUG");
  if (parsed.valid) {
    pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, parsed.pcre2_options);
    if (singleton) {
      g_grep_state.active_patterns[0] = singleton;
      g_grep_state.active_pattern_count = 1;
    }
    g_grep_state.case_insensitive = parsed.case_insensitive;
    g_grep_state.fixed_string = parsed.is_fixed_string;
    g_grep_state.global_highlight = parsed.global_flag;
    g_grep_state.invert_match = parsed.invert;
    g_grep_state.context_before = parsed.context_before;
    g_grep_state.context_after = parsed.context_after;
  }

  atomic_store(&g_grep_state.needs_rerender, true);
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
  log_filter_parse_result_t parsed = log_filter_parse_pattern(g_grep_state.input_buffer);

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
  g_grep_state.fixed_string = parsed.is_fixed_string;
  g_grep_state.global_highlight = parsed.global_flag;
  g_grep_state.invert_match = parsed.invert;
  g_grep_state.context_before = parsed.context_before;
  g_grep_state.context_after = parsed.context_after;

  // Compile and store new pattern (if not fixed string)
  if (strlen(parsed.pattern) > 0 && !parsed.is_fixed_string) {
    pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, parsed.pcre2_options);
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
  // Use atomic read (async-signal-safe) to avoid mutex issues when called from signal handlers
  return atomic_load(&g_grep_state.mode_atomic) == GREP_MODE_ENTERING;
}

bool interactive_grep_is_active(void) {
  // Use atomic read (async-signal-safe) to avoid mutex issues when called from signal handlers
  return atomic_load(&g_grep_state.mode_atomic) != GREP_MODE_INACTIVE;
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
      .prefix = NULL,                      // Don't render prefix - interactive_grep_render_input_line() handles it
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
    log_filter_parse_result_t parsed = log_filter_parse_pattern(g_grep_state.input_buffer);

    if (!parsed.valid) {
      // Invalid pattern - keep previous patterns active (don't clear)
      mutex_unlock(&g_grep_state.mutex);
      atomic_store(&g_grep_state.needs_rerender, true);
      break;
    }

    // Valid pattern - update filtering state
    if (parsed.is_fixed_string) {
      // Fixed string matching - no regex compilation needed
      g_grep_state.active_pattern_count = 0;
      g_grep_state.case_insensitive = parsed.case_insensitive;
      g_grep_state.fixed_string = true;
    } else {
      // Regex pattern - compile with full pcre2_options
      pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile(parsed.pattern, parsed.pcre2_options);
      if (singleton) {
        pcre2_code *code = asciichat_pcre2_singleton_get_code(singleton);
        if (code) {
          g_grep_state.active_patterns[0] = singleton;
          g_grep_state.active_pattern_count = 1;
          g_grep_state.case_insensitive = parsed.case_insensitive;
          g_grep_state.fixed_string = false;
        } else {
          // Compilation failed - fall back to fixed string matching
          g_grep_state.active_pattern_count = 0;
          g_grep_state.fixed_string = true;
          g_grep_state.case_insensitive = parsed.case_insensitive;
        }
      } else {
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

  // Get logs from in-memory buffer
  session_log_entry_t *buffer_entries =
      SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
  if (!buffer_entries) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate log buffer");
  }

  size_t buffer_count = session_log_buffer_get_recent(buffer_entries, SESSION_LOG_BUFFER_SIZE);

  // Try to tail log file if specified
  session_log_entry_t *file_entries = NULL;
  size_t file_count = 0;

  const char *log_file = GET_OPTION(log_file);
  if (log_file && log_file[0] != '\0') {
    // Tail last 100KB of log file, but limit to half SESSION_LOG_BUFFER_SIZE
    // to avoid buffer overflow when merging with in-memory buffer
    file_count = log_file_parser_tail(log_file, 100 * 1024, &file_entries, SESSION_LOG_BUFFER_SIZE / 2);
    if (file_count > 0) {
      log_debug("Log file tailing: read %zu entries from %s", file_count, log_file);
    }
  }

  // Merge and deduplicate if we have file entries
  session_log_entry_t *merged_entries = NULL;
  size_t merged_count = 0;

  if (file_count > 0) {
    // Ensure colors are initialized before recoloring file logs
    log_init_colors();

    merged_count =
        log_file_parser_merge_and_dedupe(buffer_entries, buffer_count, file_entries, file_count, &merged_entries);
    SAFE_FREE(buffer_entries);
    SAFE_FREE(file_entries);

    // Cap merged entries at SESSION_LOG_BUFFER_SIZE to prevent buffer overflow
    if (merged_count > SESSION_LOG_BUFFER_SIZE) {
      size_t truncated = merged_count - SESSION_LOG_BUFFER_SIZE;
      log_warn("Log buffer overflow: truncated %zu oldest entries", truncated);
      merged_count = SESSION_LOG_BUFFER_SIZE;
    }

    buffer_entries = merged_entries;
    buffer_count = merged_count;
  }

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
  log_filter_parse_result_t parsed = {0};
  if (has_fixed_string) {
    parsed = log_filter_parse_pattern(g_grep_state.input_buffer);
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
 * Display Highlighting
 * ========================================================================== */

bool interactive_grep_get_match_info(const char *message, size_t *out_match_start, size_t *out_match_len) {
  if (!message || !out_match_start || !out_match_len) {
    return false;
  }

  *out_match_start = 0;
  *out_match_len = 0;

  // Use atomic read to avoid mutex contention with keyboard handler
  if (atomic_load(&g_grep_state.mode_atomic) == GREP_MODE_INACTIVE) {
    return false;
  }

  // Make a safe copy of state under mutex, then release before doing expensive work
  mutex_lock(&g_grep_state.mutex);

  bool has_fixed_string = g_grep_state.fixed_string && g_grep_state.len > 0;
  bool case_insensitive = g_grep_state.case_insensitive;
  char pattern_copy[GREP_INPUT_BUFFER_SIZE];
  size_t pattern_len = g_grep_state.len;
  int pattern_count = g_grep_state.active_pattern_count;
  pcre2_singleton_t *patterns_copy[MAX_GREP_PATTERNS];

  if (has_fixed_string) {
    strncpy(pattern_copy, g_grep_state.input_buffer, sizeof(pattern_copy) - 1);
    pattern_copy[sizeof(pattern_copy) - 1] = '\0';
  }

  for (int i = 0; i < pattern_count && i < MAX_GREP_PATTERNS; i++) {
    patterns_copy[i] = g_grep_state.active_patterns[i];
  }

  mutex_unlock(&g_grep_state.mutex);

  // Now do matching without holding mutex
  size_t message_len = strlen(message);
  bool matches = false;

  // Fixed string matching
  if (has_fixed_string) {
    const char *found = NULL;

    if (case_insensitive) {
      found = utf8_strcasestr(message, pattern_copy);
    } else {
      found = strstr(message, pattern_copy);
    }

    if (found) {
      matches = true;
      *out_match_start = (size_t)(found - message);
      *out_match_len = pattern_len;
    }
  }
  // Regex pattern matching
  else if (pattern_count > 0) {
    pcre2_match_data *match_data = NULL;
    for (int p = 0; p < pattern_count; p++) {
      if (patterns_copy[p]) {
        pcre2_code *code = asciichat_pcre2_singleton_get_code(patterns_copy[p]);
        if (code) {
          match_data = pcre2_match_data_create_from_pattern(code, NULL);
          break;
        }
      }
    }

    if (match_data) {
      for (int p = 0; p < pattern_count; p++) {
        pcre2_singleton_t *singleton = patterns_copy[p];
        if (!singleton) {
          continue;
        }

        pcre2_code *code = asciichat_pcre2_singleton_get_code(singleton);
        if (!code) {
          continue;
        }

        int rc = pcre2_jit_match(code, (PCRE2_SPTR)message, message_len, 0, 0, match_data, NULL);
        if (rc >= 0) {
          matches = true;
          PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
          *out_match_start = (size_t)ovector[0];
          *out_match_len = (size_t)(ovector[1] - ovector[0]);
          break;
        }
      }
      pcre2_match_data_free(match_data);
    }
  }

  return matches;
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
