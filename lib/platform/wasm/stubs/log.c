/**
 * @file platform/wasm/stubs/log.c
 * @brief Logging utility stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <stdbool.h>
#include <stddef.h>

// Memory-mapped logging stubs (not supported in WASM)
asciichat_error_t log_mmap_init_simple(const char *log_path, size_t max_size) {
  (void)log_path;
  (void)max_size;
  return ASCIICHAT_OK; // No-op - mmap logging not supported in WASM
}

bool log_mmap_is_active(void) {
  return false; // mmap logging not active in WASM
}

void log_mmap_write(int level, const char *file, int line, const char *func, const char *fmt, ...) {
  (void)level;
  (void)file;
  (void)line;
  (void)func;
  (void)fmt;
  // No-op - mmap logging not supported in WASM
}

bool log_mmap_get_usage(size_t *used, size_t *capacity) {
  if (used)
    *used = 0;
  if (capacity)
    *capacity = 0;
  return false;
}

void log_mmap_rotate(void) {
  // No-op
}

// Server status logging stub (not used in mirror mode)
void server_status_log_append(const char *message) {
  (void)message;
  // No-op - server status screen not used in WASM mirror mode
}

// JSON logging stub
void log_json_write(int fd, int level, uint64_t time_ns, const char *file, int line, const char *func,
                    const char *json_msg) {
  (void)fd;
  (void)level;
  (void)time_ns;
  (void)file;
  (void)line;
  (void)func;
  (void)json_msg;
  // No-op - JSON logging in WASM uses console output instead
}

// Terminal logging fd selection stub
int terminal_choose_log_fd(log_level_t level) {
  (void)level;
  return 2; // Stub: return stderr-like fd
}
