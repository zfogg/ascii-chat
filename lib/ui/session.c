#include <ascii-chat/ui/session.h>
#include <ascii-chat/ui/too_small.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/options/options.h>
#include <string.h>

struct ui_session {
  terminal_size_t size;
  frame_buffer_t *frame;
  frame_buffer_t *media;
  session_log_buffer_t *logs;
  session_log_entry_t recent[128];
  animation_t animation;
  ui_splash_view_t splash;
  bool loading, help, dirty;
  double saved_volume;
  uint64_t presentation_times[120];
  unsigned presentation_index;
};

ui_session_t *ui_session_create(void) {
  ui_session_t *s = SAFE_CALLOC(1, sizeof(*s), ui_session_t *);
  if (!s)
    return NULL;
  s->frame = frame_buffer_create(24, 80);
  s->media = frame_buffer_create(24, 80);
  s->logs = session_log_buffer_create();
  if (!s->frame || !s->media || !s->logs) {
    ui_session_destroy(s);
    return NULL;
  }
  s->size = (terminal_size_t){.cols = 80, .rows = 24};
  s->saved_volume = 1;
  s->dirty = true;
  s->splash.use_colors = true;
  animation_init(&s->animation, "session_splash",
                 (animation_config_t){
                     .type = ANIMATION_SPLASH_RAINBOW, .fps = 60, .speed = 1, .hidden_policy = ANIMATION_HIDDEN_PAUSE});
  return s;
}

void ui_session_destroy(ui_session_t *s) {
  if (!s)
    return;
  if (log_get_session_log_buffer() == s->logs)
    log_clear_session_log_buffer();
  animation_destroy(&s->animation);
  frame_buffer_destroy(s->frame);
  frame_buffer_destroy(s->media);
  session_log_buffer_destroy(s->logs);
  SAFE_FREE(s);
}

void ui_session_resize(ui_session_t *s, int cols, int rows) {
  if (!s || cols < 1 || rows < 1 || cols > 4096 || rows > 4096)
    return;
  if (cols != s->size.cols || rows != s->size.rows) {
    s->size = (terminal_size_t){.cols = cols, .rows = rows};
    s->dirty = true;
  }
}

void ui_session_loading(ui_session_t *s, bool loading, const char *message) {
  if (!s)
    return;
  if (loading != s->loading) {
    s->loading = loading;
    animation_reset(&s->animation);
    s->dirty = true;
  }
  if (message && strcmp(message, s->splash.connection_target)) {
    SAFE_STRNCPY(s->splash.connection_target, message, sizeof(s->splash.connection_target));
    s->dirty = true;
  }
}

void ui_session_log(ui_session_t *s, const char *message) {
  if (s && message) {
    session_log_buffer_append(s->logs, message);
    s->dirty = true;
  }
}

asciichat_error_t ui_session_media(ui_session_t *s, const char *data, size_t length) {
  if (!s || !data || length > UI_CONTROLLER_MAX_FRAME_BYTES)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session media frame");
  frame_buffer_reset(s->media);
  frame_buffer_append(s->media, data, length);
  s->dirty = true;
  return ASCIICHAT_OK;
}

ui_session_action_t ui_session_input(ui_session_t *s, keyboard_key_t key) {
  if (!s)
    return UI_ACTION_NONE;
  ui_session_action_t action = ui_session_key(key, &s->saved_volume);
  if (action == UI_ACTION_HELP) {
    s->help = !s->help;
  } else if (action == UI_ACTION_CLOSE && s->help) {
    s->help = false;
    action = UI_ACTION_HELP;
  }
  s->dirty |= action != UI_ACTION_NONE;
  return action;
}

const char *ui_session_tick(ui_session_t *s, uint64_t now_ns) {
  if (!s)
    return NULL;
  // Help covers splash; both cover media. Keep receiving media while covered.
  animation_set_visible(&s->animation, s->loading && !s->help);
  if (animation_update(&s->animation, now_ns, &s->splash.animation) != ASCIICHAT_OK)
    return NULL;
  bool animated = s->splash.animation.changed;
  if (!s->dirty && !(s->loading && !s->help && animated))
    return NULL;
  s->dirty = false;
  frame_buffer_reset(s->frame);
  frame_buffer_clear_screen(s->frame);
  if (s->help) {
    terminal_size_t minimum;
    char *text = ui_session_help_render(s->size, true, true, &minimum);
    if (text) {
      if (ui_too_small(s->size, minimum))
        ui_too_small_render(s->frame, s->size, minimum);
      else
        frame_buffer_append(s->frame, text, strlen(text));
      SAFE_FREE(text);
    }
  } else if (s->loading) {
    terminal_size_t minimum = {.cols = 52, .rows = 10};
    if (ui_too_small(s->size, minimum)) {
      ui_too_small_render(s->frame, s->size, minimum);
    } else {
      ui_session_splash_render(s->frame, s->size, &s->splash);
      size_t capacity = (size_t)(s->size.rows - 9);
      if (capacity > 128)
        capacity = 128;
      size_t count = session_log_buffer_get_recent(s->logs, s->recent, capacity);
      for (size_t i = 0; i < count; ++i)
        frame_buffer_printf(s->frame, "\033[%zu;1H%.*s\033[K", i + 9, s->size.cols - 1, s->recent[i].message);
    }
  } else {
    frame_buffer_append(s->frame, frame_buffer_get_content(s->media), frame_buffer_get_length(s->media));
    if (frame_buffer_get_length(s->media)) {
      s->presentation_times[s->presentation_index++ % 120] = now_ns;
      if (GET_OPTION(fps_counter)) {
        unsigned frames = 0;
        for (unsigned i = 0; i < 120; ++i)
          frames += s->presentation_times[i] && now_ns - s->presentation_times[i] < 1000000000ULL;
        frame_buffer_printf(s->frame, "\033[1;%dH\033[0mFPS: %3u", s->size.cols > 9 ? s->size.cols - 8 : 1, frames);
      }
    }
  }
  frame_buffer_append(s->frame, "\0", 1);
  return frame_buffer_get_content(s->frame);
}

void ui_session_clear(ui_session_t *s) {
  if (s) {
    frame_buffer_reset(s->media);
    s->help = false;
    s->dirty = true;
  }
}

void ui_session_redraw(ui_session_t *s) {
  if (s)
    s->dirty = true;
}

void ui_session_capture_logs(ui_session_t *s) {
  if (s)
    log_set_session_log_buffer(s->logs);
}
