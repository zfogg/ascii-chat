#include <ascii-chat/ui/layout.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/ui/table.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <ascii-chat/options/options.h>
#include <string.h>

#include <ascii-chat/tui/stats.h>
static size_t stats_body_width(const stats_view_t *view) {
  size_t width = 0;
  for (unsigned i = 0; i < view->count; ++i) {
    size_t length = strlen(view->lines[i]);
    if (length > width)
      width = length;
  }
  return width;
}

static void stats_footer(const stats_view_t *view, char *footer, size_t capacity) {
  snprintf(footer, capacity, "= / Esc close | Tab page | Up/Down select | %u rows", view->count);
}

terminal_size_t tui_stats_minimum(const stats_view_t *view) {
  char title[160], footer[160];
  snprintf(title, sizeof(title), "  ASCII-CHAT / %-20s  LIVE STATS", view->mode);
  stats_footer(view, footer, sizeof(footer));
  size_t width = stats_body_width(view) + 4;
  if (strlen(title) + 1 > width)
    width = strlen(title) + 1;
  if (strlen(footer) + 3 > width)
    width = strlen(footer) + 3;
  // Both subtitles are shorter than the title. Keep one spare row and column
  // to avoid terminal auto-wrap/scroll, in addition to the fixed header/footer.
  return (terminal_size_t){.cols = (int)width, .rows = 10};
}

typedef struct {
  const stats_view_t *view;
  char text[4096];
} stats_table_context_t;
static void stats_cells(size_t row, const char **cells, size_t count, void *data) {
  (void)count;
  stats_table_context_t *context = data;
  if (context->view->headings[row])
    snprintf(context->text, sizeof(context->text), "%s", colored_string(LOG_COLOR_DEBUG, context->view->lines[row]));
  else
    ui_text_values_style(context->text, sizeof(context->text), context->view->lines[row], LOG_COLOR_FATAL);
  cells[0] = context->text;
}

void tui_stats_render(terminal_size_t size, const void *data) {
  const stats_view_t *view = data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols + 64);
  if (!buffer)
    return;
  terminal_capabilities_t caps = detect_terminal_capabilities();
  terminal_color_mode_t mode = GET_OPTION(color_mode);
  bool color = mode != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE && !GET_OPTION(strip_ansi) &&
               (mode != TERM_COLOR_AUTO || caps.color_level != TERM_COLOR_NONE);
  const char *blue = color ? ui_text_color_prefix(LOG_COLOR_DEBUG) : "";
  frame_buffer_cursor_home(buffer);
  frame_buffer_printf(buffer, "\033[1m%s  ASCII-CHAT / %-20s  LIVE STATS\033[0m\033[K\n", blue, view->mode);
  frame_buffer_printf(buffer, "  ");
  frame_buffer_printf(buffer, "%s", blue);
  ui_text_render_values(buffer, view->page ? "CONNECTIONS & MODE DETAILS" : "OVERVIEW / rates over ~1 second");
  frame_buffer_printf(buffer, "\033[K\n");
  frame_buffer_render_border(buffer, size.cols, blue);
  ui_layout_t layout;
  if (ui_layout_measure(size, 3, 1, 2, &layout) != ASCIICHAT_OK) {
    frame_buffer_destroy(buffer);
    return;
  }
  int available = layout.body_rows > 0 ? layout.body_rows : 1;
  const ui_table_column_t column = {.heading = "Statistics", .width = size.cols - 4};
  stats_table_context_t context = {.view = view};
  ui_table_t table = {.columns = &column,
                      .column_count = 1,
                      .row_count = view->count,
                      .viewport = {.selected = view->selected, .first_visible = view->offset},
                      .cells = stats_cells,
                      .data = &context,
                      .selectable = true};
  ui_table_render(buffer, &table, 4, 2, size.cols - 2, available);
  frame_buffer_printf(buffer, "\033[%d;1H", size.rows - 2);
  frame_buffer_render_border(buffer, size.cols, blue);
  char footer[160];
  stats_footer(view, footer, sizeof(footer));
  frame_buffer_printf(buffer, "  ");
  ui_text_render_values(buffer, footer);
  frame_buffer_printf(buffer, "\033[K");
  ui_controller_write(ui_controller_active_fd(), frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  frame_buffer_destroy(buffer);
}
