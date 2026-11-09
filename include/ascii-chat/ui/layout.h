#pragma once
#include <ascii-chat/platform/terminal.h>
typedef struct {
  int body_row, body_rows, detail_row, footer_row;
/**
 * @brief The useful part of ui layout is in `body_row`, `body_rows`, `detail_row`, `footer_row`: together they describe what is pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent cleanup path.
 */
} ui_layout_t;
/** Fixed chrome with a bounded, possibly empty body viewport. */
asciichat_error_t ui_layout_measure(terminal_size_t size, int header_rows, int detail_rows, int footer_rows,
                                    ui_layout_t *out);
