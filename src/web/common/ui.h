/** Browser-facing C interface implemented in ui.c. */
#pragma once

#include <stddef.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct ui_session ui_session_t;
typedef struct terminal_renderer_s terminal_renderer_t;

#ifdef __cplusplus
extern "C" {
#endif

ui_session_t *browser_ui_create(void);
void browser_ui_destroy(ui_session_t *s);
void browser_ui_resize(ui_session_t *s, int cols, int rows);
void browser_ui_loading(ui_session_t *s, int loading, const char *message);
void browser_ui_log(ui_session_t *s, const char *message);
asciichat_error_t browser_ui_media(ui_session_t *s, const char *text, size_t length);
int browser_ui_key(ui_session_t *s, int key);
const char *browser_ui_tick(ui_session_t *s, double milliseconds);
void browser_ui_clear(ui_session_t *s);
void browser_ui_redraw(ui_session_t *s);
void browser_ui_capture_logs(ui_session_t *s);
asciichat_error_t browser_renderer_create(int cols, int rows, double font_size, int matrix, int selection,
                                          terminal_renderer_t **output);

double browser_ui_volume(void);

asciichat_error_t browser_ui_audio(int enabled);

int browser_ui_fps(void);

#ifdef __cplusplus
}
#endif
