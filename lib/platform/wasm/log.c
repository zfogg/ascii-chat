/**
 * @file platform/wasm/log.c
 * @brief WASM logging implementation using console output
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <emscripten.h>

// Forward declare types for stub implementations
typedef int log_level_t;
typedef int log_color_t;
typedef int color_scheme_t;

// JavaScript function to log to console
EM_JS(void, js_console_error, (const char *msg), {
  console.error(UTF8ToString(msg));
});

// Log message functions - output to console in WASM
void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  js_console_error(buffer);
}

void log_file_msg(const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  js_console_error(buffer);
}

void log_plain_msg(const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  js_console_error(buffer);
}

void log_msg_internal(const char *level, const char *file, int line, const char *func, const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  js_console_error(buffer);
}

void log_terminal_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  js_console_error(buffer);
}

int log_template_parse(const char *template_str) {
  (void)template_str;
  return 0;
}

void log_plain_stderr_msg(const char *format, ...) {
  (void)format;
}

char *format_message(const char *format, va_list args) {
  (void)format;
  (void)args;
  return NULL;
}

log_level_t log_get_level(void) {
  return 0;
}

void log_set_level(log_level_t level) {
  (void)level;
}

void log_set_color_scheme(const color_scheme_t *scheme) {
  (void)scheme;
}

void log_set_terminal_output(bool enabled) {
  (void)enabled;
}

bool log_get_terminal_output(void) {
  return false;
}

const char *log_level_color(log_color_t color) {
  (void)color;
  return "";
}

void log_init(const char *filename, log_level_t level, bool force_stderr, bool use_mmap) {
  (void)filename;
  (void)level;
  (void)force_stderr;
  (void)use_mmap;
}

void log_init_colors(void) {
}
