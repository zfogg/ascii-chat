/**
 * @file ui/interactive_grep.h
 * @brief Interactive grep filtering for terminal screens
 *
 * Provides vim-style `/` grep functionality for status and splash screens.
 * Users can press `/` to activate search mode, type a pattern with full
 * /pattern/flags syntax support, and see logs filter in real-time.
 *
 * Features:
 * - Press `/` to enter search mode
 * - Bottom line becomes search input: `/<pattern>█` or `/<pattern>/flags█`
 * - Supports full flag syntax: i, F, g, I, A<n>, B<n>, C<n>
 * - Live filtering as user types
 * - Enter accepts, Escape/Ctrl+C cancels
 * - Integrates with both status screen and splash screen
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/platform/keyboard.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Interactive grep mode states
 */
typedef enum {
  GREP_MODE_INACTIVE, ///< Not in grep mode
  GREP_MODE_ENTERING, ///< '/' pressed, typing pattern
  GREP_MODE_ACTIVE    ///< Pattern accepted and filtering
} grep_mode_t;

/* ============================================================================
 * Lifecycle Functions
 * ========================================================================== */

/**
 * @brief Initialize interactive grep subsystem
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Must be called before using any other interactive_grep functions.
 * Initializes state, mutex, and loads CLI --grep patterns if any.
 */
asciichat_error_t interactive_grep_init(void);

/**
 * @brief Clean up interactive grep subsystem
 *
 * Frees all allocated resources. Safe to call multiple times.
 */
void interactive_grep_destroy(void);

/* ============================================================================
 * Mode Management
 * ========================================================================== */

/**
 * @brief Enter search mode (user pressed '/')
 *
 * Saves current CLI --grep patterns, activates input mode,
 * and prepares for interactive pattern entry.
 */
void interactive_grep_enter_mode(void);

/**
 * @brief Exit search mode
 * @param accept If true, compile and activate typed pattern; if false, restore previous patterns
 *
 * If accepting: Parses pattern with /pattern/flags syntax, compiles with PCRE2,
 * and activates filtering. If canceling: Restores CLI --grep patterns.
 */
void interactive_grep_exit_mode(bool accept);

/**
 * @brief Check if currently in input mode (typing pattern)
 * @return true if in GREP_MODE_ENTERING, false otherwise
 */
bool interactive_grep_is_entering(void);

/**
 * @brief Check if filtering is active
 * @return true if in GREP_MODE_ENTERING or GREP_MODE_ACTIVE, false if INACTIVE
 */
bool interactive_grep_is_active(void);

/* ============================================================================
 * Keyboard Handling
 * ========================================================================== */

/**
 * @brief Check if a key should be handled by grep module
 * @param key Keyboard key code
 * @return true if grep should handle this key, false to pass through
 *
 * Returns true for:
 * - '/' when not in input mode (to enter mode)
 * - All keys when in GREP_MODE_ENTERING (to edit pattern)
 */
bool interactive_grep_should_handle(int key);

/**
 * @brief Process keyboard input for grep
 * @param key The keyboard key to process
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Uses keyboard_read_line_interactive() to handle text editing.
 * Supports backspace, delete, arrows, home, end, enter, escape, UTF-8.
 *
 * Pattern validation happens live via validator callback.
 * Enter compiles and activates pattern.
 * Escape cancels and restores previous patterns.
 */
asciichat_error_t interactive_grep_handle_key(keyboard_key_t key);

/* ============================================================================
 * Log Filtering and Display
 * ========================================================================== */

/**
 * @brief Gather and filter logs for display
 * @param out_entries Output array of filtered log entries (caller must free with SAFE_FREE)
 * @param out_count Output count of filtered entries
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Hybrid approach:
 * 1. If --log-file specified: tail last N KB, parse entries
 * 2. Fetch entries from in-memory session_log_buffer (up to 100 entries)
 * 3. Merge and deduplicate by timestamp
 * 4. Filter combined set with active PCRE2 patterns
 * 5. Return filtered entries (caller must free)
 *
 * If no log file or read error: Falls back to in-memory buffer only.
 */
asciichat_error_t interactive_grep_gather_and_filter_logs(session_log_entry_t **out_entries, size_t *out_count);

/**
 * @brief Render grep input line at bottom of screen
 * @param width Terminal width in columns
 *
 * Renders: `/<pattern>/flags█` with cursor position indicated.
 * Cursor shown as block character or with background color.
 *
 * If pattern is invalid (validator returns false), shows red background.
 */
void interactive_grep_render_input_line(int width);

/**
 * @brief Get match info for highlighting a log message
 * @param message Log message to check
 * @param out_match_start Output: start position of match in plain text
 * @param out_match_len Output: length of match
 * @return true if message matches current filter pattern, false otherwise
 *
 * Used by display code to apply highlighting to matching portions of logs.
 * Returns match position for use with log_filter_highlight().
 */
bool interactive_grep_get_match_info(const char *message, size_t *out_match_start, size_t *out_match_len);

/**
 * @brief Check if global highlighting (/g flag) is enabled in current pattern
 * @return true if interactive grep is active and /g flag is set, false otherwise
 *
 * Used by filter highlighting code to determine if all matches should be highlighted
 * or just the first match. Returns false when interactive grep is inactive.
 */
bool interactive_grep_get_global_highlight(void);

/**
 * @brief Get the compiled regex pattern for interactive grep (internal use)
 * @return Opaque PCRE2 singleton for the current pattern, or NULL if inactive/no pattern
 *
 * Internal function used by filter highlighting code to enable global match
 * highlighting in interactive grep mode. Returns NULL when not in active mode
 * or if pattern is fixed string type (no regex compilation).
 */
void *interactive_grep_get_pattern_singleton(void);

/* ============================================================================
 * Signal-Safe Interface
 * ========================================================================== */

/**
 * @brief Check if grep is in entering mode (async-signal-safe)
 * @return true if in GREP_MODE_ENTERING
 *
 * Uses atomic load only (no mutex). Safe to call from signal handlers.
 */
bool interactive_grep_is_entering_atomic(void);

/**
 * @brief Cancel grep mode from a signal handler (async-signal-safe)
 *
 * Sets an atomic flag that the status screen loop checks on its next
 * iteration. Does not use mutexes or allocate memory.
 */
void interactive_grep_signal_cancel(void);

/**
 * @brief Check and clear the signal-cancel flag
 * @return true if grep was cancelled by a signal since last check
 *
 * Called by the status screen loop to detect signal-initiated cancellation.
 */
bool interactive_grep_check_signal_cancel(void);

/* ============================================================================
 * Re-render Notification
 * ========================================================================== */

/**
 * @brief Check if screen needs immediate re-render
 * @return true if pattern changed and re-render needed, false otherwise
 *
 * Uses atomic flag for lock-free checking in render loops.
 * Automatically clears flag after returning true.
 */
bool interactive_grep_needs_rerender(void);

/* ============================================================================
 * Internal Access (for atomic rendering)
 * ========================================================================== */

/**
 * @brief Get the mutex protecting grep state
 * @return Pointer to the grep state mutex (internal use only)
 *
 * Used by terminal rendering to perform atomic read of grep input
 * for consistent rendering without flicker.
 */
void *interactive_grep_get_mutex(void);

/**
 * @brief Get current input buffer length (must hold mutex)
 * @return Length of the current grep input pattern
 *
 * Must be called while holding the mutex from interactive_grep_get_mutex().
 */
int interactive_grep_get_input_len(void);

/**
 * @brief Get current input buffer content (must hold mutex)
 * @return Pointer to the input buffer (valid only while holding mutex)
 *
 * Must be called while holding the mutex from interactive_grep_get_mutex().
 */
const char *interactive_grep_get_input_buffer(void);

/**
 * @brief Check if case-insensitive flag is set (must hold mutex)
 * @return true if /i flag is set, false otherwise
 *
 * Must be called while holding the mutex from interactive_grep_get_mutex().
 */
bool interactive_grep_get_case_insensitive(void);
