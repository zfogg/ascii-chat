#include <ascii-chat/ui/input.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/ui/popup.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/tui/notice.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/string.h>
#include <string.h>
#include <ascii-chat/tui/invitation.h>
#include <ascii-chat/util/time.h>

typedef struct {
  size_t prompt_bytes;
  size_t cursor;
  size_t focus;
  bool password_dialog, incorrect, can_confirm;
  ui_notice_severity_t severity;
  bool color;
  bool unicode;
  char text[];
} prompt_snapshot_t;

static void media_center_line(frame_buffer_t *buffer, const char *text, int row, int cols) {
  int width = utf8_display_width(text);
  int col = (cols - width) / 2 + 1;
  frame_buffer_printf(buffer, "\033[%d;1H%*s", row, col > 1 ? col - 1 : 0, "");
  int used = 0;
  for (const char *p = text; *p;) {
    int bytes = utf8_next_char_bytes(p, strlen(p));
    if (bytes <= 0)
      break;
    int cells = utf8_display_width_n(p, (size_t)bytes);
    if (used + cells > cols - 2)
      break;
    frame_buffer_append(buffer, p, (size_t)bytes);
    used += cells;
    p += bytes;
  }
  frame_buffer_append(buffer, "\033[K", 3);
}

static void media_panel_clear_background(frame_buffer_t *buffer, terminal_size_t size, int top, int logo_rows,
                                         int shown) {
  for (int row = 1; row <= size.rows; ++row) {
    bool logo = row >= top && row < top + logo_rows;
    bool heading = row == top + logo_rows + 1 || row == top + logo_rows + 2;
    bool input = row >= top + logo_rows + 4 && row < top + logo_rows + 4 + shown;
    if (!logo && !heading && !input)
      frame_buffer_printf(buffer, "\033[%d;1H\033[K", row);
  }
}

void ui_media_panel_render(frame_buffer_t *buffer, terminal_size_t size, const char *title, const char *instruction,
                           const char *text, size_t cursor) {
  bool compact = size.cols < 52;
  int logo_rows = compact ? 1 : 4;
  int lines = 1;
  if (text) {
    for (size_t i = 0; text[i]; ++i) {
      if (text[i] == '\n') {
        ++lines;
      }
    }
  }
  int available = size.rows - logo_rows - 5;
  if (available < 1)
    available = 1;
  int shown = text ? (lines < available ? lines : available) : 0;
  int height = logo_rows + 3 + (text ? 1 + shown : 0);
  int top = (size.rows - height) / 2 + 1;
  if (top < 1)
    top = 1;
  bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(frame_buffer_get_screen_output_fd());
  frame_buffer_append(buffer, "\033[0m\033[H\033[?25l", 13);
  frame_buffer_printf(buffer, "\033[%d;1H", top);
  const animation_sample_t *sample = ui_controller_animation_sample();
  int tick = sample ? (int)(sample->seconds * 60) : (int)((time_get_ns() / NS_PER_MS_INT / 16) % 100000);
  invitation_render_logo(buffer, size.cols, tick, colors, compact);
  media_center_line(buffer, title, top + logo_rows + 1, size.cols);
  media_center_line(buffer, instruction, top + logo_rows + 2, size.cols);
  if (!text) {
    media_panel_clear_background(buffer, size, top, logo_rows, 0);
    return;
  }
  int width = size.cols - 6;
  if (width > 76)
    width = 76;
  int longest = 20;
  const char *line = text;
  for (const char *p = text;; ++p) {
    if (!*p || *p == '\n') {
      int n = utf8_display_width_n(line, (size_t)(p - line));
      if (n > longest)
        longest = n;
      if (!*p)
        break;
      line = p + 1;
    }
  }
  if (longest < width)
    width = longest;
  int col = (size.cols - (width + 2)) / 2 + 1;
  for (int i = 0; i < shown; ++i)
    frame_buffer_printf(buffer, "\033[%d;1H\033[K", top + logo_rows + 4 + i);
  ui_input_render(buffer, top + logo_rows + 4, col, width, shown, text, cursor, true);
  media_panel_clear_background(buffer, size, top, logo_rows, shown);
}

static void password_panel(frame_buffer_t *buffer, terminal_size_t size, const prompt_snapshot_t *s) {
  int width = size.cols - 4;
  if (width > 64)
    width = 64;
  int height = 13;
  int top = (size.rows - height) / 2 + 1;
  int logo_rows = size.cols >= 52 && top >= 6 ? 4 : 1;
  int logo_top = top - logo_rows - 1;
  frame_buffer_printf(buffer, "\033[0m\033[H\033[2J\033[?25l\033[%d;1H", logo_top);
  const animation_sample_t *sample = ui_controller_animation_sample();
  invitation_render_logo(buffer, size.cols, sample ? (int)(sample->seconds * 60) : 0, s->color, logo_rows == 1);
  media_center_line(buffer, "Video chat in your terminal", top - 1, size.cols);
  int left = (size.cols - width) / 2 + 1;
  ui_popup_frame(buffer, top, left, width, height, LOG_COLOR_RESET);
  frame_buffer_printf(buffer, "\033[%d;%dH\033[1mPassword required\033[0m", top + 1, left + 2);
  char target[256];
  snprintf(target, sizeof(target), "Connect to %.*s", width - 15, s->text);
  frame_buffer_printf(buffer, "\033[%d;%dH%s", top + 3, left + 2, target);
  frame_buffer_printf(buffer, "\033[%d;%dHThis server requires a password.", top + 4, left + 2);
  if (s->incorrect)
    frame_buffer_printf(buffer, "\033[%d;%dH%s", top + 5, left + 2,
                        colored_string(LOG_COLOR_WARN, "Incorrect password. Try again."));
  frame_buffer_printf(buffer, "\033[%d;%dHPassword (%d characters minimum)", top + 6, left + 2, MIN_PASSWORD_LENGTH);
  ui_popup_frame(buffer, top + 7, left + 2, width - 4, 3, LOG_COLOR_DEBUG);
  const ui_button_t buttons[] = {{"[ Confirm ]", 0, !s->can_confirm}, {"[ Cancel ]", 0, false}};
  ui_buttons_render(buffer, top + 10, left + 2, width - 4, buttons, 2, s->focus ? s->focus - 1 : SIZE_MAX);
  frame_buffer_printf(buffer, "\033[%d;%dHEnter OK  Tab focus  Esc cancel", top + 11, left + 2);
  ui_input_render(buffer, top + 8, left + 3, width - 8, 1, s->text + s->prompt_bytes, s->cursor, false);
  if (s->focus)
    frame_buffer_printf(buffer, "\033[?25l");
}

static void render_prompt(terminal_size_t size, const void *data) {
  const prompt_snapshot_t *snapshot = data;
  const char *visible = snapshot->text + snapshot->prompt_bytes;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  if (snapshot->password_dialog) {
    password_panel(buffer, size, snapshot);
    ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
    frame_buffer_destroy(buffer);
    return;
  }
  if (strncmp(snapshot->text, "Open media:", 11) == 0) {
    ui_media_panel_render(buffer, size, "Open media:", "Drop files / paste paths or URLs; Enter plays; Esc cancels",
                          visible, snapshot->cursor);
    ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
    frame_buffer_destroy(buffer);
    return;
  }
  frame_buffer_append(buffer, "\033[0m\033[H", 7);
  int row =
      ui_notice_render(buffer, snapshot->text, snapshot->severity, size.cols, snapshot->unicode, snapshot->color) + 2;
  frame_buffer_printf(buffer, "\033[%d;1H\033[2K", row);
  ui_input_render(buffer, row, 1, size.cols - 3, 1, visible, snapshot->cursor, false);
  ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  frame_buffer_destroy(buffer);
}

static int prompt_fd(void) {
  if (ui_controller_is_presenting())
    return STDERR_FILENO; // The controller inherits the active screen's retained terminal.
  return platform_isatty(STDERR_FILENO) ? STDERR_FILENO : STDOUT_FILENO;
}

asciichat_error_t ui_prompt_present(const char *prompt, const char *visible, size_t cursor) {
  size_t prompt_bytes = strlen(prompt) + 1;
  size_t visible_bytes = strlen(visible) + 1;
  prompt_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot) + prompt_bytes + visible_bytes, prompt_snapshot_t *);
  if (!snapshot)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate prompt presentation");
  snapshot->password_dialog = snapshot->incorrect = snapshot->can_confirm = false;
  snapshot->focus = 0;
  snapshot->prompt_bytes = prompt_bytes;
  snapshot->cursor = cursor;
  snapshot->severity = ui_notice_prompt_severity();
  bool media = strncmp(prompt, "Open media:", 11) == 0;
  snapshot->color = !media && !GET_OPTION(strip_ansi) && terminal_should_color_output(prompt_fd());
  snapshot->unicode = !media && terminal_should_use_utf8();
  memcpy(snapshot->text, prompt, prompt_bytes);
  memcpy(snapshot->text + prompt_bytes, visible, visible_bytes);
  // Height is measured at the minimum width, so every larger terminal fits.
  terminal_size_t minimum = {.cols = 40,
                             .rows = ui_notice_render(NULL, prompt, snapshot->severity, 40, false, false) + 3};
  if (media)
    minimum.rows = 8;
  asciichat_error_t result = ui_controller_submit(UI_SCREEN_PROMPT, prompt_fd(), minimum, render_prompt, snapshot,
                                                  sizeof(*snapshot) + prompt_bytes + visible_bytes);
  SAFE_FREE(snapshot);
  return result;
}

void ui_prompt_remove(void) {
  ui_controller_remove(UI_SCREEN_PROMPT);
  if (ui_controller_state().screen < 0)
    ui_controller_write(prompt_fd(), "\r\n", 2);
}

int ui_prompt_required_rows(const void *data, int cols) {
  const prompt_snapshot_t *snapshot = data;
  if (snapshot->password_dialog)
    return 19;
  if (strncmp(snapshot->text, "Open media:", 11) == 0)
    return cols < 52 ? 8 : 11;
  return ui_notice_render(NULL, snapshot->text, snapshot->severity, cols, false, false) + 3;
}

static void render_media_loading(terminal_size_t size, const void *data) {
  (void)data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  ui_media_panel_render(buffer, size, "Loading media...", "Esc cancels; current playback continues.", NULL, 0);
  ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  frame_buffer_destroy(buffer);
}

asciichat_error_t ui_media_loading_present(void) {
  return ui_controller_submit(UI_SCREEN_RENDER_PROGRESS, STDERR_FILENO, (terminal_size_t){.cols = 40, .rows = 8},
                              render_media_loading, "", 1);
}

asciichat_error_t ui_password_prompt_present(const char *target, const char *masked, size_t cursor, size_t focus,
                                             bool incorrect, bool can_confirm) {
  if (!target || !masked || focus > 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid password presentation");
  size_t target_bytes = strlen(target) + 1, masked_bytes = strlen(masked) + 1;
  size_t bytes = sizeof(prompt_snapshot_t) + target_bytes + masked_bytes;
  prompt_snapshot_t *s = SAFE_CALLOC(1, bytes, prompt_snapshot_t *);
  if (!s)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate password presentation");
  s->prompt_bytes = target_bytes;
  s->cursor = cursor;
  s->focus = focus;
  s->password_dialog = true;
  s->incorrect = incorrect;
  s->can_confirm = can_confirm;
  s->color = !GET_OPTION(strip_ansi) && terminal_should_color_output(prompt_fd());
  memcpy(s->text, target, target_bytes);
  memcpy(s->text + target_bytes, masked, masked_bytes);
  asciichat_error_t result = ui_controller_submit(UI_SCREEN_PROMPT, prompt_fd(),
                                                  (terminal_size_t){.cols = 40, .rows = 19}, render_prompt, s, bytes);
  SAFE_FREE(s);
  return result;
}
