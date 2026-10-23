#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/ui/table.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/display.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

size_t ui_table_row(char *out, size_t cap, const ui_table_column_t *cols, const char *const *cells, size_t count,
                    int width, bool selectable, bool selected) {
  if (!out || !cap) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Table row requires output storage");
    return 0;
  }
  out[0] = '\0';
  if (!cols || !count || count > 32 || width <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid table columns or width");
    return 0;
  }
  int64_t remaining = (int64_t)width - (selectable ? 1 : 0) - (int)(count - 1);
  size_t flexible = 0;
  for (size_t i = 0; i < count; ++i) {
    if (cols[i].width < 0) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Table column width cannot be negative");
      return 0;
    }
    if (cols[i].width > 0)
      remaining -= cols[i].width;
    else
      ++flexible;
  }
  size_t used = 0;
  int available = width;
  if (selectable && cap > 1) {
    out[used++] = selected ? '>' : ' ';
    out[used] = '\0';
    --available;
  }
  for (size_t i = 0; i < count && available > 0; ++i) {
    if (i && used + 1 < cap) {
      out[used++] = ' ';
      out[used] = '\0';
      --available;
    }
    int cell_width = cols[i].width > 0 ? cols[i].width : flexible ? remaining / (int)flexible : 0;
    if (cell_width < 0)
      cell_width = 0;
    if (!cols[i].width && flexible) {
      remaining -= cell_width;
      --flexible;
    }
    if (cell_width > available)
      cell_width = available;
    const char *text = cells ? cells[i] : cols[i].heading;
    char clipped[8192];
    if (text && cols[i].ellipsis && display_width(text) > cell_width) {
      truncate_with_ellipsis_mode(text, clipped, sizeof(clipped), cell_width, terminal_should_use_utf8());
      text = clipped;
    }
    used += ui_text_cell(out + used, cap - used, text, cell_width, cols[i].alignment);
    available -= cell_width;
  }
  return used;
}

void ui_table_render(frame_buffer_t *out, ui_table_t *t, int row, int col, int width, int height) {
  if (!out || !t || !t->columns || !t->column_count || t->column_count > 32 || !t->cells || width < 2 || height < 1 ||
      row < 1 || col < 1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid table output, data source or geometry");
    return;
  }
  char line[8192];
  if (t->header) {
    ui_table_row(line, sizeof(line), t->columns, NULL, t->column_count, width - 1, t->selectable, false);
    frame_buffer_printf(out, "\033[%d;%dH%s%s%s", row++, col, ui_text_color_prefix(LOG_COLOR_DEBUG), line,
                        ui_text_color_prefix(LOG_COLOR_RESET));
    --height;
  }
  if (height < 1)
    return;
  ui_viewport_range_t range = ui_viewport_update(&t->viewport, t->row_count, (size_t)height);
  for (int i = 0; i < height; ++i) {
    const char *cells[32] = {0};
    if ((size_t)i < range.count) {
      t->cells(range.first + (size_t)i, cells, t->column_count, t->data);
      ui_table_row(line, sizeof(line), t->columns, cells, t->column_count, width - 1, t->selectable,
                   range.first + (size_t)i == t->viewport.selected);
    } else
      snprintf(line, sizeof(line), "%*s", width - 1, "");
    bool selected = t->selectable && (size_t)i < range.count && range.first + (size_t)i == t->viewport.selected;
    frame_buffer_printf(out, "\033[%d;%dH%s%s%s", row + i, col, selected ? "\033[7m" : "", line,
                        selected ? "\033[27m" : "");
  }
  ui_scrollbar_render(out, row, col + width - 1, height, range.page_count, (size_t)height, t->viewport.first_visible);
}
