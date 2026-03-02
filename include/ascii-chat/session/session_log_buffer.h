#pragma once

/**
 * @file session/session_log_buffer.h
 * @brief Thread-safe circular log buffer for session screens (status, splash)
 * @ingroup session
 *
 * Provides a reusable log capture mechanism for session screens that need to
 * display logs alongside fixed header content without terminal scrolling.
 *
 * Thread-safe for concurrent append operations. Designed for use by:
 * - Server status screen (live log feed under status header)
 * - Splash screen (logs under animated ASCII art)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Configuration
#define SESSION_LOG_BUFFER_SIZE 8192
#define SESSION_LOG_LINE_MAX 1024

/**
 * @brief Single log entry with sequence number for ordering
 */
typedef struct {
  char message[SESSION_LOG_LINE_MAX]; ///< Formatted log message (may include ANSI colors)
  uint64_t sequence;                  ///< Sequence number (monotonic, for ordering)
} session_log_entry_t;

/**
 * @brief Opaque handle to log buffer instance
 */
typedef struct session_log_buffer session_log_buffer_t;

/**
 * @brief Create a new session log buffer instance
 *
 * Allocates and initializes a circular buffer for log capture.
 * Caller is responsible for calling session_log_buffer_destroy() when done.
 *
 * @return Pointer to new buffer instance, or NULL on allocation failure
 */
session_log_buffer_t *session_log_buffer_create(void);

/**
 * @brief Cleanup session log buffer instance
 *
 * Frees the buffer and all internal resources. Safe to call with NULL.
 *
 * @param buf Buffer instance to destroy (can be NULL)
 */
void session_log_buffer_destroy(session_log_buffer_t *buf);

/**
 * @brief Clear all log entries from buffer
 *
 * Thread-safe. Resets buffer to empty state, discarding all captured logs.
 * Useful for clearing initialization logs before screen starts rendering.
 *
 * @param buf Buffer instance (must not be NULL)
 */
void session_log_buffer_clear(session_log_buffer_t *buf);

/**
 * @brief Append a log message to the buffer
 *
 * Thread-safe. Called from logging system to capture messages.
 * Message is copied into circular buffer (oldest entry overwritten if full).
 *
 * @param buf Buffer instance (must not be NULL)
 * @param message Log message text (already formatted with colors)
 */
void session_log_buffer_append(session_log_buffer_t *buf, const char *message);

/**
 * @brief Get recent log entries from buffer
 *
 * Thread-safe. Copies up to max_count recent entries into out_entries.
 * Entries are returned in chronological order (oldest first).
 *
 * @param buf Buffer instance (must not be NULL)
 * @param out_entries Output array to fill with log entries
 * @param max_count Maximum number of entries to return
 * @return Number of entries actually copied (may be less than max_count)
 */
size_t session_log_buffer_get_recent(session_log_buffer_t *buf, session_log_entry_t *out_entries, size_t max_count);
