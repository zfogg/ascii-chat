/**
 * @file lib/session/session_log_buffer.c
 * @brief Thread-safe circular log buffer for session screens
 */

#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <stdatomic.h>

/**
 * @brief Internal circular buffer structure
 */
typedef struct session_log_buffer {
  session_log_entry_t entries[SESSION_LOG_BUFFER_SIZE];
  _Atomic size_t write_pos;
  _Atomic uint64_t sequence;
  mutex_t mutex;
} session_log_buffer_t;

static session_log_buffer_t *g_log_buffer = NULL;

bool session_log_buffer_init(void) {
  if (g_log_buffer) {
    return true; // Already initialized
  }

  g_log_buffer = SAFE_CALLOC(1, sizeof(session_log_buffer_t), session_log_buffer_t *);
  if (!g_log_buffer) {
    return false;
  }

  atomic_init(&g_log_buffer->write_pos, 0);
  atomic_init(&g_log_buffer->sequence, 0);
  mutex_init(&g_log_buffer->mutex);

  return true;
}

void session_log_buffer_cleanup(void) {
  if (!g_log_buffer) {
    return;
  }
  mutex_destroy(&g_log_buffer->mutex);
  SAFE_FREE(g_log_buffer);
}

void session_log_buffer_clear(void) {
  if (!g_log_buffer) {
    return;
  }

  mutex_lock(&g_log_buffer->mutex);

  // Reset write position and sequence
  atomic_store(&g_log_buffer->write_pos, 0);
  atomic_store(&g_log_buffer->sequence, 0);

  // Clear all entries
  for (size_t i = 0; i < SESSION_LOG_BUFFER_SIZE; i++) {
    g_log_buffer->entries[i].message[0] = '\0';
    g_log_buffer->entries[i].sequence = 0;
  }

  mutex_unlock(&g_log_buffer->mutex);
}

void session_log_buffer_append(const char *message) {
  if (!g_log_buffer || !message) {
    return;
  }

  mutex_lock(&g_log_buffer->mutex);

  size_t pos = atomic_load(&g_log_buffer->write_pos);
  uint64_t seq = atomic_fetch_add(&g_log_buffer->sequence, 1);

  SAFE_STRNCPY(g_log_buffer->entries[pos].message, message, SESSION_LOG_LINE_MAX);
  g_log_buffer->entries[pos].sequence = seq;

  atomic_store(&g_log_buffer->write_pos, (pos + 1) % SESSION_LOG_BUFFER_SIZE);

  mutex_unlock(&g_log_buffer->mutex);
}

size_t session_log_buffer_get_recent(session_log_entry_t *out_entries, size_t max_count) {
  if (!g_log_buffer || !out_entries || max_count == 0) {
    return 0;
  }

  mutex_lock(&g_log_buffer->mutex);

  size_t write_pos = atomic_load(&g_log_buffer->write_pos);
  uint64_t total_entries = atomic_load(&g_log_buffer->sequence);

  size_t start_pos = write_pos;
  size_t entries_to_check = SESSION_LOG_BUFFER_SIZE;

  if (total_entries < SESSION_LOG_BUFFER_SIZE) {
    start_pos = 0;
    entries_to_check = write_pos;
  }

  size_t count = 0;
  for (size_t i = 0; i < entries_to_check && count < max_count; i++) {
    size_t idx = (start_pos + i) % SESSION_LOG_BUFFER_SIZE;
    if (g_log_buffer->entries[idx].sequence > 0) {
      memcpy(&out_entries[count], &g_log_buffer->entries[idx], sizeof(session_log_entry_t));
      count++;
    }
  }

  mutex_unlock(&g_log_buffer->mutex);
  return count;
}
