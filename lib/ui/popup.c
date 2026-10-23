#include <ascii-chat/ui/popup.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <string.h>

asciichat_error_t ui_popup_rule(frame_buffer_t *out, int width, ui_popup_edge_t edge, bool double_border, bool unicode,
                                log_color_t color) {
  if (!out || width < 2 || edge < UI_POPUP_TOP || edge > UI_POPUP_RULE)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid popup rule output, width or edge");
  static const char *single[][2] = {{"┌", "┐"}, {"├", "┤"}, {"└", "┘"}, {"", ""}};
  static const char *dual[][2] = {{"╔", "╗"}, {"╠", "╣"}, {"╚", "╝"}, {"", ""}};
  const char *left = edge == UI_POPUP_RULE ? "" : !unicode ? "+" : double_border ? dual[edge][0] : single[edge][0];
  const char *right = edge == UI_POPUP_RULE ? "" : !unicode ? "+" : double_border ? dual[edge][1] : single[edge][1];
  const char *horizontal = !unicode ? "-" : double_border ? "═" : "─";
  frame_buffer_printf(out, "%s", colored_string(color, left));
  for (int i = 0; i < width - (edge == UI_POPUP_RULE ? 0 : 2); ++i)
    frame_buffer_printf(out, "%s", colored_string(color, horizontal));
  frame_buffer_printf(out, "%s", colored_string(color, right));
  return ASCIICHAT_OK;
}

asciichat_error_t ui_popup_frame(frame_buffer_t *out, int row, int col, int width, int height, log_color_t color) {
  if (!out || row < 1 || col < 1 || width < 2 || height < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid popup frame output or rectangle");
  frame_buffer_printf(out, "\033[%d;%dH", row, col);
  ui_popup_rule(out, width, UI_POPUP_TOP, false, terminal_should_use_utf8(), color);
  const char *vertical = terminal_should_use_utf8() ? "│" : "|";
  for (int i = 1; i < height - 1; ++i)
    frame_buffer_printf(out, "\033[%d;%dH%s%*s%s", row + i, col, colored_string(color, vertical), width - 2, "",
                        colored_string(color, vertical));
  frame_buffer_printf(out, "\033[%d;%dH", row + height - 1, col);
  return ui_popup_rule(out, width, UI_POPUP_BOTTOM, false, terminal_should_use_utf8(), color);
}

bool ui_popup_render(frame_buffer_t *output, terminal_size_t size, const ui_popup_t *box) {
  if (!output || !box || !box->title || (box->line_count && !box->lines) || box->top_margin < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid popup output, content or margins");
    return false;
  }
  if (size.cols < 8 || size.rows < 5)
    return false;
  int content_width = display_width(box->title);
  for (size_t i = 0; i < box->line_count; ++i) {
    int width = display_width(box->lines[i]);
    if (width > content_width)
      content_width = width;
  }
  if (box->footer && display_width(box->footer) > content_width)
    content_width = display_width(box->footer);
  int width = content_width + 4;
  if (width < box->minimum_width)
    width = box->minimum_width;
  if (width > size.cols - 2)
    width = size.cols - 2;
  int height = (int)box->line_count + (box->footer ? 5 : 4);
  if (height > size.rows - 2 - box->top_margin)
    return false;
  int left = (size.cols - width) / 2 + 1;
  int top = box->top_margin + (size.rows - box->top_margin - height) / 2 + 1;
  if (ui_popup_frame(output, top, left, width, height, LOG_COLOR_RESET) != ASCIICHAT_OK)
    return false;
  char rendered[1024];
  truncate_with_ellipsis(box->title, rendered, sizeof(rendered), width - 4);
  const char *colored_title = colored_string(LOG_COLOR_RESET, rendered);
  frame_buffer_printf(output, "\033[%d;%dH\033[1m%s\033[0m", top + 1, left + 2, colored_title);
  for (size_t i = 0; i < box->line_count; ++i) {
    truncate_with_ellipsis(box->lines[i], rendered, sizeof(rendered), width - 4);
    frame_buffer_printf(output, "\033[%d;%dH%s", top + 2 + (int)i, left + 2, rendered);
  }
  if (box->footer) {
    truncate_with_ellipsis(box->footer, rendered, sizeof(rendered), width - 4);
    frame_buffer_printf(output, "\033[%d;%dH\033[2m%s\033[0m", top + height - 2, left + 2, rendered);
  }
  return true;
}
