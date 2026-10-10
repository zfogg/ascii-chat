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

  // Keep every line of multiline notices and split long lines at UTF-8 boundaries.
  // Hold the lock for the entire message so concurrent producers cannot interleave it.
  do {
    const char *newline = strchr(message, '\n');
    size_t length = newline ? (size_t)(newline - message) : strlen(message);
    if (length >= SESSION_LOG_LINE_MAX) {
      length = SESSION_LOG_LINE_MAX - 1;
      while (length && ((unsigned char)message[length] & 0xc0) == 0x80)
        --length;
      if (!length)
        length = SESSION_LOG_LINE_MAX - 1; // Malformed UTF-8 must still make progress.
    }
    size_t pos = atomic_load_u64(&buf->write_pos);
    memcpy(buf->entries[pos].message, message, length);
    buf->entries[pos].message[length] = '\0';
    buf->entries[pos].sequence = atomic_fetch_add_u64(&buf->sequence, 1) + 1;
    atomic_store_u64(&buf->write_pos, (pos + 1) % SESSION_LOG_BUFFER_SIZE);
    message += length;
    if (*message == '\n')
      ++message;
  } while (*message);

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
