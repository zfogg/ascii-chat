/**
 * @file platform/wasm/stubs/util.c
 * @brief Utility stubs for WASM/Emscripten (backtrace, localtime)
 * @ingroup platform
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/asciichat_errno.h>
#include <time.h>
#include <stddef.h>
#include <stdbool.h>

// Backtrace stubs (not supported in WASM)
int platform_backtrace(void **buffer, int size) {
  (void)buffer;
  (void)size;
  return 0; // No backtrace support in WASM
}

char **platform_backtrace_symbols(void *const *buffer, int size) {
  (void)buffer;
  (void)size;
  return NULL; // No backtrace symbols in WASM
}

void platform_backtrace_symbols_destroy(char **symbols) {
  (void)symbols;
  // No-op
}

void platform_print_backtrace_symbols(const char *label, char **symbols, int count, int skip_frames, int max_frames,
                                      backtrace_frame_filter_t filter) {
  (void)label;
  (void)symbols;
  (void)count;
  (void)skip_frames;
  (void)max_frames;
  (void)filter;
  // No-op - backtrace not supported in WASM
}

void platform_print_backtrace(int skip_frames) {
  (void)skip_frames;
  // No-op - backtrace not supported in WASM
}

// Localtime stub
asciichat_error_t platform_localtime(const time_t *timer, struct tm *result) {
  if (!timer || !result) {
    return ERROR_INVALID_PARAM;
  }
  // Use thread-safe localtime_r for both WASM and native builds
  // IMPORTANT: localtime() is NOT thread-safe and causes deadlocks in threaded WASM builds
  if (localtime_r(timer, result) != NULL) {
    return ASCIICHAT_OK;
  }
  return ERROR_PLATFORM_INIT;
}

// Isatty stub
int platform_isatty(int fd) {
  (void)fd;
  return 1; // Always true for WASM terminal (xterm.js)
}

// Error state management
void platform_clear_error_state(void) {
  // No-op - error state is thread-local, nothing to clear in WASM
}

// Temp directory stub
bool platform_get_temp_dir(char *temp_dir, size_t path_size) {
  if (!temp_dir || path_size == 0) {
    return false;
  }
  platform_strlcpy(temp_dir, "/tmp", path_size);
  return true;
}

// Prompt stub
bool platform_prompt_yes_no(const char *question, bool default_yes) {
  (void)question;
  return default_yes; // Always return default in WASM
}

// Error handling stub
int platform_get_last_error(void) {
  return 0; // No system error in WASM
}

// Path utility stubs
const char *platform_get_home_dir(void) {
  return "/home"; // WASM virtual filesystem home
}

void platform_normalize_path_separators(char *path) {
  // No-op - WASM uses forward slashes (Unix-style)
  (void)path;
}

// Config directory stub
char *platform_get_config_dir(void) {
  static char config_dir[] = "/config";
  return config_dir; // WASM virtual filesystem config directory
}

// Current working directory stub
bool platform_get_cwd(char *cwd, size_t path_size) {
  if (!cwd || path_size == 0) {
    return false;
  }
  platform_strlcpy(cwd, "/", path_size);
  return true;
}

// Path comparison stub (case-sensitive on WASM)
int platform_path_strcasecmp(const char *a, const char *b, size_t n) {
  return strncmp(a, b, n); // Case-sensitive on WASM
}

// File type check stub
int platform_is_regular_file(const char *path) {
  (void)path;
  return 0; // No file system access in WASM mirror mode
}
