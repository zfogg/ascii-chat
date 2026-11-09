#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/platform/keyboard.h>

/** Selection is an absolute item index; first_visible is relative to its page.
 * @brief For ui viewport, this is the live record that ties `selected`, `first_visible`, `page_size` together. The owning code creates it, updates it while work is active, and releases its handles and pointed-to storage before freeing the record.
 * @brief ui viewport range is intentionally a small, concrete record. Fields such as `page`, `pages`, `page_first`, `page_count` give callers the identity and current value they need. It is normally borrowed from, or owned by, the surrounding subsystem—not independently managed.
 * A zero page_size means one page containing the complete data set. */
typedef struct {
  size_t selected, first_visible, page_size;
} ui_viewport_t;
typedef struct {
  size_t page, pages, page_first, page_count, first, count;
} ui_viewport_range_t;
ui_viewport_range_t ui_viewport_update(ui_viewport_t *view, size_t count, size_t rows);
bool ui_viewport_key(ui_viewport_t *view, size_t count, size_t rows, keyboard_key_t key);
