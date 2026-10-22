#include <ascii-chat/ui/overlay_box.h>
<<<<<<< HEAD
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <string.h>
=======
#include <ascii-chat/util/display.h>
>>>>>>> 6bbb358ef (refactor: share terminal update overlay)

bool ui_overlay_box_render(frame_buffer_t *output, terminal_size_t size, const ui_overlay_box_t *box) {
  if (!output || !box || !box->title || size.cols < 8 || size.rows < 5)
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
  if (height > size.rows - 2)
    return false;
  int left = (size.cols - width) / 2 + 1;
  int top = (size.rows - height) / 2 + 1;
<<<<<<< HEAD
  utf8_setting_t utf8 = GET_OPTION(force_utf8);
  bool unicode = utf8 == UTF8_SETTING_TRUE || (utf8 == UTF8_SETTING_AUTO && terminal_supports_utf8());
  const char *top_left = unicode ? "┌" : "+";
  const char *top_right = unicode ? "┐" : "+";
  const char *bottom_left = unicode ? "└" : "+";
  const char *bottom_right = unicode ? "┘" : "+";
  const char *horizontal = unicode ? "─" : "-";
  const char *vertical = unicode ? "│" : "|";
  frame_buffer_printf(output, "\033[0m\033[%d;%dH%s", top, left, top_left);
  for (int i = 0; i < width - 2; ++i)
    frame_buffer_append(output, horizontal, strlen(horizontal));
  frame_buffer_append(output, top_right, strlen(top_right));
  for (int row = 1; row < height - 1; ++row)
    frame_buffer_printf(output, "\033[%d;%dH%s%*s%s", top + row, left, vertical, width - 2, "", vertical);
  frame_buffer_printf(output, "\033[%d;%dH%s", top + height - 1, left, bottom_left);
  for (int i = 0; i < width - 2; ++i)
    frame_buffer_append(output, horizontal, strlen(horizontal));
  frame_buffer_append(output, bottom_right, strlen(bottom_right));
  char rendered[1024];
  truncate_with_ellipsis(box->title, rendered, sizeof(rendered), width - 4);
  const char *colored_title = colored_string(LOG_COLOR_DEBUG, rendered);
  frame_buffer_printf(output, "\033[%d;%dH\033[1m%s\033[0m", top + 1, left + 2, colored_title);
  for (size_t i = 0; i < box->line_count; ++i) {
    truncate_with_ellipsis(box->lines[i], rendered, sizeof(rendered), width - 4);
    frame_buffer_printf(output, "\033[%d;%dH%s", top + 2 + (int)i, left + 2, rendered);
  }
  if (box->footer) {
    truncate_with_ellipsis(box->footer, rendered, sizeof(rendered), width - 4);
    frame_buffer_printf(output, "\033[%d;%dH\033[2m%s\033[0m", top + height - 2, left + 2, rendered);
  }
=======
  frame_buffer_printf(output, "\033[0m\033[%d;%dH┌", top, left);
  for (int i = 0; i < width - 2; ++i)
    frame_buffer_append(output, "─", 3);
  frame_buffer_append(output, "┐", 3);
  for (int row = 1; row < height - 1; ++row)
    frame_buffer_printf(output, "\033[%d;%dH│%*s│", top + row, left, width - 2, "");
  frame_buffer_printf(output, "\033[%d;%dH└", top + height - 1, left);
  for (int i = 0; i < width - 2; ++i)
    frame_buffer_append(output, "─", 3);
  frame_buffer_append(output, "┘", 3);
  frame_buffer_printf(output, "\033[%d;%dH\033[1m%.*s\033[0m", top + 1, left + 2, width - 4, box->title);
  for (size_t i = 0; i < box->line_count; ++i)
    frame_buffer_printf(output, "\033[%d;%dH%.*s", top + 2 + (int)i, left + 2, width - 4, box->lines[i]);
  if (box->footer)
    frame_buffer_printf(output, "\033[%d;%dH\033[2m%.*s\033[0m", top + height - 2, left + 2, width - 4, box->footer);
>>>>>>> 6bbb358ef (refactor: share terminal update overlay)
  return true;
}
