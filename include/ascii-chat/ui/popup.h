#pragma once

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/log/log.h>

typedef enum { UI_POPUP_TOP, UI_POPUP_SEPARATOR, UI_POPUP_BOTTOM, UI_POPUP_RULE } ui_popup_edge_t;
/** Shared chrome for streaming notices and nested dialog panels. No newline. */
asciichat_error_t ui_popup_rule(frame_buffer_t *output, int width, ui_popup_edge_t edge, bool double_border,
                                bool unicode, log_color_t color);
asciichat_error_t ui_popup_frame(frame_buffer_t *output, int row, int column, int width, int height, log_color_t color);

/** A compact modal drawn over an already-rendered terminal frame. */
typedef struct {
  const char *title;
  const char *const *lines;
  size_t line_count;
  const char *footer;
  int minimum_width;
  int top_margin;
} ui_popup_t;

/** Draw a centered box without clearing the frame beneath it. */
bool ui_popup_render(frame_buffer_t *output, terminal_size_t size, const ui_popup_t *box);
