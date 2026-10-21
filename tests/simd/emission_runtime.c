/** Minimal process diagnostics for the isolated renderer executable.
 * Rendering, palette caches, color quantization, lifecycle, locks, and allocation
 * all use production sources. No application logger or crash handler is needed.
 */
#include <ascii-chat/common.h>
#include <stdarg.h>
#include <ascii-chat/debug/named.h>

void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  (void)level;
  (void)file;
  (void)line;
  (void)func;
  (void)fmt;
}

asciichat_error_t errno_set_with_message(asciichat_error_t code, const char *file, int line, const char *function,
                                         const char *format, ...) {
  (void)file;
  (void)line;
  (void)function;
  (void)format;
  return code;
}

void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...) {
  (void)code;
  (void)file;
  (void)line;
  (void)function;
  (void)format;
  abort();
}

const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func, uintptr_t parent_key) {
  (void)parent_key;
  (void)key;
  (void)type;
  (void)format_spec;
  (void)file;
  (void)line;
  (void)func;
  return base_name;
}

void named_unregister(uintptr_t key) {
  (void)key;
}
