/**
 * @file lib/session/session_log_buffer.c
 * @brief Thread-safe circular log buffer for session screens
 */

#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <ascii-chat/atomic.h>

/**
 * @brief Internal circular buffer structure
 */
typedef struct session_log_buffer {
  session_log_entry_t entries[SESSION_LOG_BUFFER_SIZE];
  atomic_t write_pos;
  atomic_t sequence;
  mutex_t mutex;
} session_log_buffer_t;

session_log_buffer_t *session_log_buffer_create(void) {
  session_log_buffer_t *buf = SAFE_CALLOC(1, sizeof(session_log_buffer_t), session_log_buffer_t *);
  if (!buf) {
    return NULL;
  }

  atomic_store_u64(&buf->write_pos, 0);
  atomic_store_u64(&buf->sequence, 0);
  mutex_init(&buf->mutex, "log_buffer");

  return buf;
}

void session_log_buffer_destroy(session_log_buffer_t *buf) {
  if (!buf) {
    return;
  }

  mutex_destroy(&buf->mutex);
  SAFE_FREE(buf);
}

void session_log_buffer_clear(session_log_buffer_t *buf) {
  if (!buf) {
    return;
  }

  mutex_lock(&buf->mutex);

  // Reset write position and sequence
  atomic_store_u64(&buf->write_pos, 0);
  atomic_store_u64(&buf->sequence, 0);

  // Clear all entries
  for (size_t i = 0; i < SESSION_LOG_BUFFER_SIZE; i++) {
    buf->entries[i].message[0] = '\0';
    buf->entries[i].sequence = 0;
  }

  mutex_unlock(&buf->mutex);
}

void session_log_buffer_append(session_log_buffer_t *buf, const char *message) {
  if (!buf || !message) {
    // Fail silently - this is called FROM the logging system
    // Using SET_ERRNO here would cause infinite recursion
    return;
  }

  mutex_lock(&buf->mutex);

  size_t pos = atomic_load_u64(&buf->write_pos);
  uint64_t seq = atomic_fetch_add_u64(&buf->sequence, 1);

  SAFE_STRNCPY(buf->entries[pos].message, message, SESSION_LOG_LINE_MAX);
  buf->entries[pos].sequence = seq;

  atomic_store_u64(&buf->write_pos, (pos + 1) % SESSION_LOG_BUFFER_SIZE);

  mutex_unlock(&buf->mutex);
}

size_t session_log_buffer_get_recent(session_log_buffer_t *buf, session_log_entry_t *out_entries, size_t max_count) {
  if (!buf || !out_entries || max_count == 0) {
    // Fail silently - called from display code that handles 0 gracefully
    // Using SET_ERRNO here could cause recursion if error logging is enabled
    return 0;
  }

  mutex_lock(&buf->mutex);

  size_t write_pos = atomic_load_u64(&buf->write_pos);
  uint64_t total_entries = atomic_load_u64(&buf->sequence);

  size_t start_pos = write_pos;
  size_t entries_to_check = SESSION_LOG_BUFFER_SIZE;

  if (total_entries < SESSION_LOG_BUFFER_SIZE) {
    start_pos = 0;
    entries_to_check = write_pos;
  }

  size_t count = 0;
  for (size_t i = 0; i < entries_to_check && count < max_count; i++) {
    size_t idx = (start_pos + i) % SESSION_LOG_BUFFER_SIZE;
    if (buf->entries[idx].sequence > 0) {
      memcpy(&out_entries[count], &buf->entries[idx], sizeof(session_log_entry_t));
      count++;
    }
  }

  mutex_unlock(&buf->mutex);
  return count;
}
