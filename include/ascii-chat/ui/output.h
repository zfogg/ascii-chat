#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>
typedef asciichat_error_t (*ui_output_write_fn)(int fd, const char *data, size_t length);
/** The presenter installs a sink; standalone widgets use platform output. */
void ui_output_set_writer(ui_output_write_fn writer);
asciichat_error_t ui_output_write(int fd, const char *data, size_t length);
asciichat_error_t ui_output_printf(int fd, const char *format, ...);
