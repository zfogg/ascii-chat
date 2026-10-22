#pragma once

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <stdbool.h>
#include <stddef.h>

/** A compact modal drawn over an already-rendered terminal frame. */
typedef struct {
  const char *title;
  const char *const *lines;
  size_t line_count;
  const char *footer;
  int minimum_width;
} ui_overlay_box_t;

/** Draw a centered box without clearing the frame beneath it. */
bool ui_overlay_box_render(frame_buffer_t *output, terminal_size_t size, const ui_overlay_box_t *box);
