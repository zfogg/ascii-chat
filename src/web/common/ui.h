/** Browser-facing C interface implemented in ui.c. */
#pragma once

#include <stddef.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct ui_session ui_session_t;
typedef struct terminal_renderer_s terminal_renderer_t;

#ifdef __cplusplus
extern "C" {
#endif

ui_session_t *wasm_ui_create(void);
void wasm_ui_destroy(ui_session_t *s);
void wasm_ui_resize(ui_session_t *s, int cols, int rows);
void wasm_ui_loading(ui_session_t *s, int loading, const char *message);
void wasm_ui_log(ui_session_t *s, const char *message);
asciichat_error_t wasm_ui_media(ui_session_t *s, const char *text, size_t length);
int wasm_ui_key(ui_session_t *s, int key);
const char *wasm_ui_tick(ui_session_t *s, double milliseconds);
void wasm_ui_clear(ui_session_t *s);
void wasm_ui_redraw(ui_session_t *s);
void wasm_ui_capture_logs(ui_session_t *s);
asciichat_error_t wasm_renderer_create(int cols, int rows, double font_size, int matrix, int selection,
                                       terminal_renderer_t **output);

double wasm_ui_volume(void);

asciichat_error_t wasm_ui_audio(int enabled);

int wasm_ui_fps(void);

#ifdef __cplusplus
}
#endif
