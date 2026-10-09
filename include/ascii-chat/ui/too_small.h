#pragma once
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/ui/frame_buffer.h>

#define UI_MEDIA_MIN_COLS 20
#define UI_MEDIA_MIN_ROWS 10

bool ui_too_small(terminal_size_t actual, terminal_size_t minimum);
/** Never writes outside actual dimensions, including a 1x1 terminal. */
void ui_too_small_render(frame_buffer_t *buffer, terminal_size_t actual, terminal_size_t minimum);
