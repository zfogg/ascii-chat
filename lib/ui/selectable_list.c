#include <ascii-chat/ui/viewport.h>
#include <ascii-chat/ui/selectable_list.h>

void ui_selectable_list_reset(ui_selectable_list_t *list) {
  if (list)
    *list = (ui_selectable_list_t){0};
}

void ui_selectable_list_keep_visible(ui_selectable_list_t *list, int count, int rows) {
  if (!list)
    return;
  ui_viewport_t viewport = {.selected = list->selected > 0 ? (size_t)list->selected : 0,
                            .first_visible = list->first_visible > 0 ? (size_t)list->first_visible : 0};
  ui_viewport_update(&viewport, count > 0 ? (size_t)count : 0, rows > 0 ? (size_t)rows : 1);
  list->selected = (int)viewport.selected;
  list->first_visible = (int)viewport.first_visible;
}

void ui_selectable_list_move(ui_selectable_list_t *list, int count, int rows, int delta) {
  if (!list || count <= 0)
    return;
  list->selected += delta;
  ui_selectable_list_keep_visible(list, count, rows);
}
