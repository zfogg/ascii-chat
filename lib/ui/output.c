#include <ascii-chat/ui/output.h>
#include <ascii-chat/platform/abstraction.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
static _Atomic(ui_output_write_fn) g_writer;
void ui_output_set_writer(ui_output_write_fn writer) {
  atomic_store(&g_writer, writer);
}
asciichat_error_t ui_output_write(int fd, const char *data, size_t length) {
  if (fd < 0 || (!data && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI output descriptor or text");
  ui_output_write_fn writer = atomic_load(&g_writer);
  if (writer)
    return writer(fd, data, length);
  return platform_write_all(fd, data, length) == length
             ? ASCIICHAT_OK
             : SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot write complete UI output");
}
asciichat_error_t ui_output_printf(int fd, const char *format, ...) {
  if (!format)
    return SET_ERRNO(ERROR_INVALID_PARAM, "UI output format is NULL");
  char text[4096];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (n < 0 || (size_t)n >= sizeof(text))
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "UI formatted output exceeds capacity");
  return ui_output_write(fd, text, (size_t)n);
}
