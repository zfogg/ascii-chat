#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/platform/keyboard.h>

/** Selection is an absolute item index; first_visible is relative to its page.
 * A zero page_size means one page containing the complete data set. */
typedef struct {
  size_t selected, first_visible, page_size;
} ui_viewport_t;
typedef struct {
  size_t page, pages, page_first, page_count, first, count;
} ui_viewport_range_t;
ui_viewport_range_t ui_viewport_update(ui_viewport_t *view, size_t count, size_t rows);
bool ui_viewport_key(ui_viewport_t *view, size_t count, size_t rows, keyboard_key_t key);
