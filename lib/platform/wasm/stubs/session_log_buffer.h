/**
 * @file platform/wasm/stubs/session_log_buffer.h
 * @brief WASM stub for session log buffer
 */

#ifndef ASCIICHAT_SESSION_LOG_BUFFER_H
#define ASCIICHAT_SESSION_LOG_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Minimal stub structure for WASM */
#define SESSION_LOG_BUFFER_SIZE 8192
#define SESSION_LOG_LINE_MAX 1024

typedef struct {
  char message[SESSION_LOG_LINE_MAX];
  uint64_t sequence;
} session_log_entry_t;

typedef struct session_log_buffer session_log_buffer_t;

/* Forward declarations for stub implementations */
session_log_buffer_t *session_log_buffer_create(void);
void session_log_buffer_destroy(session_log_buffer_t *buf);
void session_log_buffer_clear(session_log_buffer_t *buf);
void session_log_buffer_append(session_log_buffer_t *buf, const char *message);
size_t session_log_buffer_get_recent(session_log_buffer_t *buf, session_log_entry_t *out_entries, size_t max_count);

#endif
