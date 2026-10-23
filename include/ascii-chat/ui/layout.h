#pragma once
#include <ascii-chat/platform/terminal.h>
typedef struct {
  int body_row, body_rows, detail_row, footer_row;
} ui_layout_t;
/** Fixed chrome with a bounded, possibly empty body viewport. */
asciichat_error_t ui_layout_measure(terminal_size_t size, int header_rows, int detail_rows, int footer_rows,
                                    ui_layout_t *out);
