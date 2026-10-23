#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/ui/viewport.h>

ui_viewport_range_t ui_viewport_update(ui_viewport_t *v, size_t count, size_t rows) {
  ui_viewport_range_t r = {.pages = 1};
  if (!v) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Viewport state is NULL");
    return r;
  }
  if (!count) {
    v->selected = v->first_visible = 0;
    return r;
  }
  if (!rows)
    rows = 1;
  if (v->selected >= count)
    v->selected = count - 1;
  size_t page_size = v->page_size ? v->page_size : count;
  r.page = v->selected / page_size;
  r.pages = 1 + (count - 1) / page_size;
  r.page_first = r.page * page_size;
  r.page_count = count - r.page_first < page_size ? count - r.page_first : page_size;
  size_t selected = v->selected - r.page_first;
  size_t last = r.page_count > rows ? r.page_count - rows : 0;
  if (v->first_visible > last)
    v->first_visible = last;
  if (selected < v->first_visible)
    v->first_visible = selected;
  if (selected - v->first_visible >= rows)
    v->first_visible = selected - rows + 1;
  r.first = r.page_first + v->first_visible;
  r.count = r.page_count - v->first_visible;
  if (r.count > rows)
    r.count = rows;
  return r;
}

bool ui_viewport_key(ui_viewport_t *v, size_t count, size_t rows, keyboard_key_t key) {
  if (!v) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Viewport state is NULL");
    return false;
  }
  ui_viewport_range_t r = ui_viewport_update(v, count, rows);
  size_t step = v->page_size ? v->page_size : (rows ? rows : 1);
  switch ((int)key) {
  case KEY_UP:
    if (v->selected)
      --v->selected;
    break;
  case KEY_DOWN:
    if (count && v->selected < count - 1)
      ++v->selected;
    break;
  case KEY_HOME:
    v->selected = v->first_visible = 0;
    break;
  case KEY_END:
    v->selected = count ? count - 1 : 0;
    break;
  case KEY_LEFT:
    if (v->page_size) {
      if (r.page)
        v->selected -= step;
    } else
      v->selected = v->selected > step ? v->selected - step : 0;
    break;
  case KEY_RIGHT:
    if (count && (!v->page_size || r.page + 1 < r.pages))
      v->selected = step < count - v->selected ? v->selected + step : count - 1;
    break;
  default:
    return false;
  }
  ui_viewport_update(v, count, rows);
  return true;
}
