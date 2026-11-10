#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/backtrace.h>
#include <libplatform/backtrace.h>

int platform_backtrace(void **buffer, int size) {
  return libplatform_backtrace_capture(buffer, size);
}

char **platform_backtrace_symbols(void *const *buffer, int size) {
  return libplatform_backtrace_symbolize(buffer, size);
}

void platform_backtrace_symbols_destroy(char **strings) {
  libplatform_backtrace_symbols_free(strings);
}

static void write_log_line(const char *line, void *context) {
  (void)context;
  log_debug("%s", line);
}

void backtrace_print_simple(int skip_frames) {
  libplatform_backtrace_print(skip_frames, write_log_line, NULL);
}
