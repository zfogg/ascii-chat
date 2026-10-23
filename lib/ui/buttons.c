#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/display.h>

int ui_buttons_key(ui_buttons_t *s, const ui_button_t *b, size_t n, keyboard_key_t key) {
  if (!s || !b || !n) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Button state and entries are required");
    return -1;
  }
  if (s->selected >= n)
    s->selected = 0;
  if (key == KEY_LEFT || key == KEY_UP)
    s->selected = s->selected ? s->selected - 1 : n - 1;
  else if (key == KEY_RIGHT || key == KEY_DOWN || key == '\t')
    s->selected = (s->selected + 1) % n;
  else {
    for (size_t i = 0; i < n; ++i)
      if (!b[i].disabled && b[i].shortcut && key == b[i].shortcut)
        return (int)i;
    if ((key == '\r' || key == '\n' || key == ' ') && !b[s->selected].disabled)
      return (int)s->selected;
  }
  return -1;
}

void ui_buttons_render(frame_buffer_t *out, int row, int left, int width, const ui_button_t *b, size_t n,
                       size_t selected) {
  if (!out || !b || row < 1 || left < 1 || width < 1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid buttons output or geometry");
    return;
  }
  int total = n ? (int)(n - 1) * 3 : 0;
  for (size_t i = 0; i < n; ++i)
    total += display_width(b[i].label);
  bool stacked = total > width;
  int col = left + (width - total) / 2;
  for (size_t i = 0; i < n; ++i) {
    int length = display_width(b[i].label);
    int x = stacked ? left + (width - length) / 2 : col;
    const char *label = colored_string(b[i].disabled   ? LOG_COLOR_GREY
                                       : i == selected ? LOG_COLOR_WARN
                                                       : LOG_COLOR_FATAL,
                                       b[i].label);
    frame_buffer_printf(out, "\033[%d;%dH%s%s\033[0m", row + (stacked ? (int)i : 0), x, i == selected ? "\033[7m" : "",
                        label);
    col += length + 3;
  }
}
