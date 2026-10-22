#include <ascii-chat/ui/selectable_list.h>

void ui_selectable_list_reset(ui_selectable_list_t *list) {
  if (list)
    *list = (ui_selectable_list_t){0};
}

void ui_selectable_list_keep_visible(ui_selectable_list_t *list, int count, int rows) {
  if (!list)
    return;
  if (count <= 0) {
    list->selected = 0;
    list->first_visible = 0;
    return;
  }
  if (rows < 1)
    rows = 1;
  if (list->selected < 0)
    list->selected = 0;
  if (list->selected >= count)
    list->selected = count - 1;
  if (list->first_visible < 0)
    list->first_visible = 0;
  if (list->selected < list->first_visible)
    list->first_visible = list->selected;
  if (list->selected >= list->first_visible + rows)
    list->first_visible = list->selected - rows + 1;
  int last_first = count > rows ? count - rows : 0;
  if (list->first_visible > last_first)
    list->first_visible = last_first;
}

void ui_selectable_list_move(ui_selectable_list_t *list, int count, int rows, int delta) {
  if (!list || count <= 0)
    return;
  list->selected += delta;
  ui_selectable_list_keep_visible(list, count, rows);
}
