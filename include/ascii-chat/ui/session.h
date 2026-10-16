#pragma once

#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/video/anim/controller.h>

/** Host-independent session views. Hosts own scheduling, input and side effects. */
typedef struct {
  bool enabled;
  bool joining;
  char session_string[256];
} ui_discovery_view_t;

typedef struct {
  ui_discovery_view_t discovery;
  animation_sample_t animation;
  bool use_colors;
  char update_notification[1024];
  char connection_target[512];
} ui_splash_view_t;

void ui_session_splash_render(frame_buffer_t *buf, terminal_size_t size, void *view);
/** Returns owned ANSI text, released with SAFE_FREE. */
char *ui_session_help_render(terminal_size_t size, bool has_media, bool browser, terminal_size_t *minimum);

typedef enum {
  UI_ACTION_NONE,
  UI_ACTION_SETTINGS,
  UI_ACTION_HELP,
  UI_ACTION_CLOSE,
  UI_ACTION_SEEK_BACK,
  UI_ACTION_SEEK_FORWARD,
  UI_ACTION_PAUSE,
  UI_ACTION_VOLUME,
  UI_ACTION_DEBUG
} ui_session_action_t;

/** Mutates shared options for settings keys; returns an action for the host. */
ui_session_action_t ui_session_key(keyboard_key_t key, double *saved_volume);

typedef struct ui_session ui_session_t;
ui_session_t *ui_session_create(void);
void ui_session_destroy(ui_session_t *session);
void ui_session_resize(ui_session_t *session, int cols, int rows);
void ui_session_loading(ui_session_t *session, bool loading, const char *message);
void ui_session_log(ui_session_t *session, const char *message);
asciichat_error_t ui_session_media(ui_session_t *session, const char *data, size_t length);
ui_session_action_t ui_session_input(ui_session_t *session, keyboard_key_t key);
/** No sleeps or I/O. Returned frame remains valid until the next call. NULL means unchanged. */
const char *ui_session_tick(ui_session_t *session, uint64_t now_ns);
void ui_session_clear(ui_session_t *session);
void ui_session_redraw(ui_session_t *session);
/** Attach this instance to the module's existing C logger. Detaches on destruction. */
void ui_session_capture_logs(ui_session_t *session);
