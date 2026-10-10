#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <stdbool.h>

typedef enum { UI_NOTICE_INFO, UI_NOTICE_WARNING, UI_NOTICE_DANGER, UI_NOTICE_FATAL } ui_notice_severity_t;

/** Append a complete box, or measure it when buffer is NULL. Returns its row count.
 * Text is plain UTF-8. Terminal control characters are made visible, never executed.
 */
int ui_notice_render(frame_buffer_t *buffer, const char *text, ui_notice_severity_t severity, int cols, bool unicode,
                     bool color);

/** Present a notice without changing authentication or input policy. */
asciichat_error_t ui_notice_present(ui_notice_severity_t severity, const char *text);

/** Explicitly styled yes/no prompt; default remains No. */
#ifdef EMSCRIPTEN_BUILD
#include <ascii-chat/platform/question.h>
static inline bool ui_notice_confirm(ui_notice_severity_t severity, const char *text, unsigned timeout_seconds) {
  (void)severity;
  return platform_prompt_yes_no_timeout(text, false, timeout_seconds);
}
#else
bool ui_notice_confirm(ui_notice_severity_t severity, const char *text, unsigned timeout_seconds);
#endif
ui_notice_severity_t ui_notice_prompt_severity(void);
void ui_notice_dismiss(void);

/** File/JSON logging stays undecorated; terminal presentation uses the notice renderer. */
void ui_notice_log(ui_notice_severity_t severity, const char *file, int line, const char *func, const char *title,
                   const char *format, ...) __attribute__((format(printf, 6, 7)));
#define NOTICE(severity, title, ...)                                                                                   \
  ui_notice_log(UI_NOTICE_##severity, __FILE__, __LINE__, __func__, title, __VA_ARGS__)

/** Informational user output independent of log level; quiet, grep and JSON still apply. */
void ui_notice_announce(const char *file, int line, const char *func, const char *title, const char *format, ...)
    __attribute__((format(printf, 5, 6)));
#define NOTICE_ANNOUNCE(title, ...) ui_notice_announce(__FILE__, __LINE__, __func__, title, __VA_ARGS__)

/** Controller teardown writes pending notices to scrollback before releasing them. */
void ui_notice_flush_snapshot(int fd, const void *snapshot);
