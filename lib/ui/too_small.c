#include <ascii-chat/ui/too_small.h>
#include <stdio.h>
#include <string.h>

bool ui_too_small(terminal_size_t actual, terminal_size_t minimum) {
  return actual.cols > 0 && actual.rows > 0 && (actual.cols < minimum.cols || actual.rows < minimum.rows);
}

size_t ui_too_small_format(char *output, size_t capacity, terminal_size_t actual, terminal_size_t minimum) {
  if (!output || !capacity || actual.cols <= 0 || actual.rows <= 0)
    return 0;
  char dimensions[96];
  snprintf(dimensions, sizeof(dimensions), "%dx%d / need %dx%d", actual.cols, actual.rows, minimum.cols, minimum.rows);
  const char *lines[] = {"Terminal too small", dimensions, "Resize to continue"};
  int count = actual.rows >= 5 ? 3 : 1;
  int row = (actual.rows - count) / 2 + 1;
  int written = snprintf(output, capacity, "\033[0m\033[2J\033[?25l");
  size_t used = (size_t)written < capacity ? (size_t)written : capacity - 1;
  for (int i = 0; i < count; ++i) {
    int width = (int)strlen(lines[i]);
    // Leave the last column unused to avoid the terminal's auto-wrap state.
    int available = actual.cols > 1 ? actual.cols - 1 : 1;
    if (width > available)
      width = available;
    int col = (actual.cols - width) / 2 + 1;
    written = snprintf(output + used, capacity - used, "\033[%d;%dH%.*s", row + i, col, width, lines[i]);
    if (written > 0)
      used += (size_t)written < capacity - used ? (size_t)written : capacity - used - 1;
  }
  return used;
}

void ui_too_small_render(frame_buffer_t *buffer, terminal_size_t actual, terminal_size_t minimum) {
  char output[512];
  size_t length = ui_too_small_format(output, sizeof(output), actual, minimum);
  frame_buffer_append(buffer, output, length);
}
