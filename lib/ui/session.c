#include <ascii-chat/ui/session.h>
#include <ascii-chat/ui/status.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/ui/too_small.h>
#include <ascii-chat/ui/invitation.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/options/options.h>
#include <string.h>
#include <stdio.h>

struct ui_session {
  terminal_size_t size;
  frame_buffer_t *frame;
  frame_buffer_t *media;
  session_log_buffer_t *logs;
  animation_t animation;
  ui_splash_view_t splash;
  bool loading, help, show_logs, dirty, owns_logs;
  time_t started;
  int active;
  bool covered;
  terminal_size_t presented_size, minimum;
  uint64_t last_render_ns;
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
  s->logs = log_get_history_buffer();
  s->owns_logs = !s->logs;
  if (s->owns_logs)
    s->logs = session_log_buffer_create();
  if (!s->frame || !s->media || !s->logs) {
    ui_session_destroy(s);
    return NULL;
  }
  s->started = time(NULL);
  log_search_init();
  s->size = (terminal_size_t){.cols = 80, .rows = 24};
  s->active = -1;
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
  if (s->owns_logs)
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
  if (s->show_logs && ui_session_logs_key(key)) {
    s->dirty = true;
    return UI_ACTION_LOGS;
  }
  ui_session_action_t action = ui_session_key(key, &s->saved_volume);
  if (action == UI_ACTION_HELP) {
    s->help = !s->help;
    s->show_logs = false;
  } else if (action == UI_ACTION_LOGS) {
    s->show_logs = !s->show_logs;
    s->help = false;
  } else if (action == UI_ACTION_CLOSE && s->show_logs) {
    s->show_logs = false;
    action = UI_ACTION_LOGS;
  } else if (action == UI_ACTION_CLOSE && s->help) {
    s->help = false;
    action = UI_ACTION_HELP;
  }
  s->dirty |= action != UI_ACTION_NONE;
  return action;
}

int ui_session_select_screen(uint32_t visible_screens) {
  for (int screen = UI_SCREEN_COUNT - 1; screen >= 0; --screen)
    if (visible_screens & (1u << screen))
      return screen;
  return -1;
}

bool ui_session_is_covered(const ui_session_t *s) {
  return s && s->covered;
}

size_t ui_session_frame_length(const ui_session_t *s) {
  size_t length = s ? frame_buffer_get_length(s->frame) : 0;
  return length ? length - 1 : 0;
}

uint64_t ui_session_next_frame_ns(const ui_session_t *s) {
  return s && s->animation.started ? s->splash.animation.next_frame_ns : UINT64_MAX;
}

const char *ui_session_render(ui_session_t *s, const ui_session_view_t *view, uint64_t now_ns) {
  if (!s || !view)
    return NULL;
  bool changed = s->active != view->screen;
  s->active = view->screen;
  bool covered = view->screen >= 0 && ui_too_small(s->size, view->minimum);
  bool transition = changed || covered != s->covered || s->size.cols != s->presented_size.cols ||
                    s->size.rows != s->presented_size.rows || view->minimum.cols != s->minimum.cols ||
                    view->minimum.rows != s->minimum.rows;
  s->covered = covered;
  bool visible =
      (view->screen == UI_SCREEN_SPLASH || view->screen == UI_SCREEN_HELP || view->screen == UI_SCREEN_MDNS) &&
      !covered;
  if (visible != s->animation.visible) {
    // Account for the elapsed interval under the old visibility before changing
    // it, so resuming after an idle modal does not advance a paused animation.
    if (animation_update(&s->animation, now_ns, &s->splash.animation) != ASCIICHAT_OK)
      return NULL;
    animation_set_visible(&s->animation, visible);
  }
  if (animation_update(&s->animation, now_ns, &s->splash.animation) != ASCIICHAT_OK)
    return NULL;
  if (view->screen < 0 || !view->draw)
    return NULL;
  bool animated = visible && s->splash.animation.changed;
  bool refresh = view->refresh_ns && now_ns - s->last_render_ns >= view->refresh_ns;
  if (covered ? !(transition || s->dirty) : !(transition || s->dirty || view->dirty || animated || refresh))
    return NULL;
  s->dirty = false;
  s->presented_size = s->size;
  s->minimum = view->minimum;
  s->last_render_ns = now_ns;
  frame_buffer_reset(s->frame);
  if (transition || view->clear) {
    frame_buffer_append(s->frame, "\033[0m", 4);
    frame_buffer_clear_screen(s->frame);
  }
  if (covered)
    ui_too_small_render(s->frame, s->size, view->minimum);
  else {
    if (view->screen == UI_SCREEN_HELP) {
      int top = (s->size.rows - view->minimum.rows) / 2;
      terminal_color_mode_t mode = GET_OPTION(color_mode);
      bool colors = mode != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE && !GET_OPTION(strip_ansi) &&
                    (mode != TERM_COLOR_AUTO || detect_terminal_capabilities().color_level != TERM_COLOR_NONE);
      frame_buffer_printf(s->frame, "\033[%d;1H", top + 1);
      invitation_render_logo(s->frame, s->size.cols, (int)(s->splash.animation.seconds * 60), colors, false);
    }
    view->draw(s->frame, s->size, &s->splash.animation, view->context);
  }
  frame_buffer_append(s->frame, "\0", 1);
  return frame_buffer_get_content(s->frame);
}

static void draw_help(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation,
                      void *context) {
  (void)size;
  (void)animation;
  const char *text = context;
  frame_buffer_append(output, text, strlen(text));
}

static void draw_splash(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation,
                        void *context) {
  ui_session_t *s = context;
  s->splash.animation = *animation;
  ui_session_splash_render(output, size, &s->splash);
}

void ui_session_logs_render(frame_buffer_t *output, terminal_size_t size, ui_session_t *s) {
  const char *mode = GET_OPTION(detected_mode) == MODE_MIRROR ? "Mirror" : "Client";
  char detail[192];
  size_t offset = terminal_screen_logs_offset();
  snprintf(detail, sizeof(detail), "] / Esc: close | /: search logs | ?: shortcuts%s%zu rows from tail",
           offset ? " | " : "", offset);
  ui_status_t status = {.mode_name = mode, .start_time = s->started, .detail = detail};
  terminal_screen_config_t config = {
      .fixed_header_lines = 4, .render_header = ui_status_render_header, .user_data = &status, .show_logs = true};
  terminal_screen_compose(output, size, &config, s->logs);
}

bool ui_session_logs_key(keyboard_key_t key) {
  if (key == KEY_ESCAPE && log_search_is_active()) {
    log_search_exit_mode(false);
    return true;
  }
  if (log_search_should_handle(key)) {
    log_search_handle_key(key);
    return true;
  }
  if (terminal_screen_logs_handle_key(key))
    return true;
  return false;
}

static void draw_logs(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation,
                      void *context) {
  (void)animation;
  ui_session_logs_render(output, size, context);
}

static void draw_media(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation,
                       void *context) {
  (void)animation;
  ui_session_t *s = context;
  frame_buffer_append(output, frame_buffer_get_content(s->media), frame_buffer_get_length(s->media));
  if (frame_buffer_get_length(s->media) && GET_OPTION(fps_counter)) {
    unsigned frames = 0;
    for (unsigned i = 0; i < 120; ++i)
      frames += s->presentation_times[i] && s->last_render_ns - s->presentation_times[i] < 1000000000ULL;
    frame_buffer_printf(output, "\033[1;%dH\033[0mFPS: %3u", size.cols > 9 ? size.cols - 8 : 1, frames);
  }
}

const char *ui_session_tick(ui_session_t *s, uint64_t now_ns) {
  if (!s)
    return NULL;
  if (!s->dirty && !s->show_logs && !s->help && !s->loading && s->active == UI_SCREEN_MEDIA)
    return NULL;
  uint32_t visible = 1u << UI_SCREEN_MEDIA;
  if (s->loading)
    visible |= 1u << UI_SCREEN_SPLASH;
  if (s->show_logs)
    visible |= 1u << UI_SCREEN_LOGS;
  if (s->help)
    visible |= 1u << UI_SCREEN_HELP;
  ui_session_view_t view = {
      .screen = ui_session_select_screen(visible), .minimum = {1, 1}, .clear = !s->help, .context = s};
  char *help = NULL;
  if (view.screen == UI_SCREEN_HELP) {
    help = ui_session_help_render(s->size, true, true, &view.minimum);
    if (!help)
      return NULL;
    view.draw = draw_help;
    view.context = help;
  } else if (view.screen == UI_SCREEN_LOGS) {
    view.minimum = (terminal_size_t){.cols = 40, .rows = 6};
    view.refresh_ns = 100000000;
    view.draw = draw_logs;
  } else if (view.screen == UI_SCREEN_SPLASH) {
    view.minimum = (terminal_size_t){.cols = 52, .rows = 10};
    view.draw = draw_splash;
  } else {
    view.draw = draw_media;
    if (s->dirty && frame_buffer_get_length(s->media))
      s->presentation_times[s->presentation_index++ % 120] = now_ns;
  }
  const char *frame = ui_session_render(s, &view, now_ns);
  SAFE_FREE(help);
  return frame;
}

void ui_session_clear(ui_session_t *s) {
  if (s) {
    frame_buffer_reset(s->media);
    s->help = false;
    s->show_logs = false;
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
