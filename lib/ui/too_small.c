#include <ascii-chat/ui/too_small.h>
#include <stdio.h>
#include <string.h>

bool ui_too_small(terminal_size_t actual, terminal_size_t minimum) {
  return actual.cols > 0 && actual.rows > 0 && (actual.cols < minimum.cols || actual.rows < minimum.rows);
}

void ui_too_small_render(frame_buffer_t *buffer, terminal_size_t actual, terminal_size_t minimum) {
  if (actual.cols <= 0 || actual.rows <= 0)
    return;
  char dimensions[96];
  snprintf(dimensions, sizeof(dimensions), "%dx%d / need %dx%d", actual.cols, actual.rows, minimum.cols, minimum.rows);
  const char *lines[] = {"Terminal too small", dimensions, "Resize to continue"};
  int count = actual.rows >= 5 ? 3 : 1;
  int row = (actual.rows - count) / 2 + 1;
  frame_buffer_printf(buffer, "\033[0m\033[2J\033[?25l");
  for (int i = 0; i < count; ++i) {
    int width = (int)strlen(lines[i]);
    // Leave the last column unused to avoid the terminal's auto-wrap state.
    int available = actual.cols > 1 ? actual.cols - 1 : 1;
    if (width > available)
      width = available;
    int col = (actual.cols - width) / 2 + 1;
    frame_buffer_printf(buffer, "\033[%d;%dH%.*s", row + i, col, width, lines[i]);
  }
}
