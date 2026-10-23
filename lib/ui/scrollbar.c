#include <ascii-chat/ui/scrollbar.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/string.h>
#include <stdio.h>

ui_scrollbar_geometry_t ui_scrollbar_measure(size_t total, size_t visible, size_t offset, size_t height) {
  ui_scrollbar_geometry_t g = {0};
  if (!height || !visible || total <= visible)
    return g;
  g.visible = true;
  g.length = (size_t)((long double)visible * height / total);
  if (!g.length)
    g.length = 1;
  if (g.length > height)
    g.length = height;
  if (offset > total - visible)
    offset = total - visible;
  g.start = (size_t)((long double)offset * (height - g.length) / (total - visible));
  return g;
}

void ui_scrollbar_cell(char *out, size_t cap, size_t row, size_t height, size_t total, size_t visible, size_t offset) {
  if (!out || !cap) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Scrollbar cell requires output storage");
    return;
  }
  ui_scrollbar_geometry_t g = ui_scrollbar_measure(total, visible, offset, height);
  bool thumb = g.visible && row >= g.start && row - g.start < g.length;
  const char *glyph = !g.visible ? " " : terminal_should_use_utf8() ? (thumb ? "█" : "│") : (thumb ? "#" : "|");
  snprintf(out, cap, "%s", colored_string(thumb ? LOG_COLOR_DEBUG : LOG_COLOR_GREY, glyph));
}

void ui_scrollbar_render(frame_buffer_t *out, int row, int col, int height, size_t total, size_t visible,
                         size_t offset) {
  if (!out || row < 1 || col < 1 || height < 1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid scrollbar output or geometry");
    return;
  }
  frame_buffer_append(out, "\0337", 2);
  for (int i = 0; i < height; ++i) {
    char cell[128];
    ui_scrollbar_cell(cell, sizeof(cell), (size_t)i, (size_t)height, total, visible, offset);
    frame_buffer_printf(out, "\033[%d;%dH%s", row + i, col, cell);
  }
  frame_buffer_append(out, "\0338", 2);
}
