/**
 * @file platform/wasm/console.c
 * @brief Browser console logging for WASM via Emscripten
 * @ingroup platform
 */

#include <emscripten.h>
#include <string.h>
#include <ascii-chat/log/logging.h>
#include <stdint.h>
#include <stddef.h>

// EM_JS bridge: route to appropriate console method based on log level
// level: 0=DEV, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL
EM_JS(void, js_console_log, (int level, const char *message), {
  const msg = UTF8ToString($1);

  switch ($0) {
  case 0: // LOG_DEV
  case 1: // LOG_DEBUG
    console.debug(msg);
    break;
  case 2: // LOG_INFO
    console.log(msg);
    break;
  case 3: // LOG_WARN
    console.warn(msg);
    break;
  case 4: // LOG_ERROR
  case 5: // LOG_FATAL
    console.error(msg);
    break;
  default:
    console.log(msg);
  }
});

// Platform hook called by logging system
// This is called after each log message is formatted, before printing to stderr/stdout
void platform_log_hook(log_level_t level, const char *message) {
  if (message) {
    js_console_log((int)level, message);
  }
}

// Parse log level from formatted message: "[LEVEL] message..."
// Returns the log_level_t enum value, or -1 if not found
int wasm_parse_log_level(const uint8_t *buf, size_t count) {
  if (!buf || count < 5) {
    return -1; // Too short to contain [LEVEL]
  }

  // Check if message starts with [
  if (buf[0] != '[') {
    return -1;
  }

  // Look for closing ]
  int bracket_end = -1;
  for (size_t i = 1; i < count && i < 10; i++) {
    if (buf[i] == ']') {
      bracket_end = (int)i;
      break;
    }
  }

  if (bracket_end < 3) {
    return -1; // Too short, minimum is [XX]
  }

  // Extract level string
  int level_len = bracket_end - 1;
  const char *level_start = (const char *)&buf[1];

  // Match against known level strings (from logging.c)
  if (level_len == 3 && strncmp(level_start, "DEV", 3) == 0) {
    return LOG_DEV;
  }
  if (level_len == 5 && strncmp(level_start, "DEBUG", 5) == 0) {
    return LOG_DEBUG;
  }
  if (level_len == 4 && strncmp(level_start, "INFO", 4) == 0) {
    return LOG_INFO;
  }
  if (level_len == 4 && strncmp(level_start, "WARN", 4) == 0) {
    return LOG_WARN;
  }
  if (level_len == 5 && strncmp(level_start, "ERROR", 5) == 0) {
    return LOG_ERROR;
  }
  if (level_len == 5 && strncmp(level_start, "FATAL", 5) == 0) {
    return LOG_FATAL;
  }

  return -1; // Level not recognized
}

// Public API for logging to console
void wasm_log_to_console(int fd, const uint8_t *buf, size_t count) {
  // For WASM, route stdout/stderr to browser console
  if ((fd == 1 || fd == 2) && buf && count > 0) {
    // Parse log level from formatted message
    int level = wasm_parse_log_level(buf, count);
    if (level >= 0) {
      // Create null-terminated string for console output
      // Copy buffer to local array and null-terminate
      static char msg_buffer[4096];
      size_t copy_len = (count < sizeof(msg_buffer) - 1) ? count : (sizeof(msg_buffer) - 1);
      memcpy(msg_buffer, buf, copy_len);
      msg_buffer[copy_len] = '\0';

      // Remove trailing newline if present (console adds it automatically)
      if (copy_len > 0 && msg_buffer[copy_len - 1] == '\n') {
        msg_buffer[copy_len - 1] = '\0';
      }

      // Route to console
      js_console_log(level, msg_buffer);
    }
  }
}
