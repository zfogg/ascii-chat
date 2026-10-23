#include <ascii-chat/ui/layout.h>
#include <limits.h>
asciichat_error_t ui_layout_measure(terminal_size_t size, int header, int detail, int footer, ui_layout_t *out) {
  if (!out || size.cols < 1 || size.rows < 1 || header < 0 || detail < 0 || footer < 0 || header > INT_MAX - detail ||
      header + detail > INT_MAX - footer)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid TUI geometry or fixed row counts");
  int chrome = header + detail + footer;
  *out = (ui_layout_t){.body_row = header + 1,
                       .body_rows = size.rows > chrome ? size.rows - chrome : 0,
                       .detail_row = size.rows - detail - footer + 1,
                       .footer_row = size.rows - footer + 1};
  if (out->detail_row < 1)
    out->detail_row = 1;
  if (out->footer_row < 1)
    out->footer_row = 1;
  return ASCIICHAT_OK;
}
