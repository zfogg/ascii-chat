#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/ui/frame_buffer.h>
typedef struct {
  size_t start, length;
  bool visible;
} ui_scrollbar_geometry_t;
ui_scrollbar_geometry_t ui_scrollbar_measure(size_t total, size_t visible, size_t offset, size_t height);
/** Format one track cell; caller supplies storage (no allocation or output). */
void ui_scrollbar_cell(char *output, size_t capacity, size_t row, size_t height, size_t total, size_t visible,
                       size_t offset);
void ui_scrollbar_render(frame_buffer_t *output, int row, int column, int height, size_t total, size_t visible,
                         size_t offset);
