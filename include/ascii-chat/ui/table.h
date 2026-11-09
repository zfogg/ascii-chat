#pragma once
#include <ascii-chat/ui/text.h>
#include <ascii-chat/ui/viewport.h>
#include <ascii-chat/ui/scrollbar.h>
typedef struct {
  const char *heading;
  int width; /* Zero shares the remaining space. */
  ui_alignment_t alignment;
  bool ellipsis; /* Mark clipped content instead of silently cutting it. */
/**
 */
} ui_table_column_t;
/** Format a complete row without allocation. Styled cells are clipped by
 * @brief When code needs to work with ui table, it keeps `columns`, `column_count`, `row_count`, `viewport` here instead of scattering it across globals. Initialization establishes the handles and sizes; teardown must release any allocation, lock, thread, or callback context recorded in those fields.
 * terminal columns, never by bytes. The caller owns data and sorting. */
size_t ui_table_row(char *out, size_t capacity, const ui_table_column_t *columns, const char *const *cells,
                    size_t count, int width, bool selectable, bool selected);
typedef void (*ui_table_cells_fn)(size_t row, const char **cells, size_t columns, void *data);
/**
 * @brief The useful part of ui table column is in `heading`, `width`, `alignment`, `ellipsis`: together they describe what is pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent cleanup path.
 */
typedef struct {
  const ui_table_column_t *columns;
  size_t column_count, row_count;
  ui_viewport_t viewport;
  ui_table_cells_fn cells;
  void *data;
  bool selectable, header;
} ui_table_t;
/** Rectangular table with a fixed header and independent page/scroll state. */
void ui_table_render(frame_buffer_t *out, ui_table_t *table, int row, int column, int width, int height);
