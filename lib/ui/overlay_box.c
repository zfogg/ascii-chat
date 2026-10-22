#include <ascii-chat/ui/overlay_box.h>
#include <ascii-chat/util/display.h>

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
  return true;
}
