#include <criterion/criterion.h>
#include <ascii-chat/ui/selectable_list.h>

Test(selectable_list_contract, reset_and_empty_lists_have_no_selected_viewport) {
  ui_selectable_list_t list = {.selected = 12, .first_visible = 8};
  ui_selectable_list_reset(&list);
  cr_assert_eq(list.selected, 0);
  cr_assert_eq(list.first_visible, 0);
  list = (ui_selectable_list_t){.selected = -9, .first_visible = 8};
  ui_selectable_list_keep_visible(&list, 0, 4);
  cr_assert_eq(list.selected, 0);
  cr_assert_eq(list.first_visible, 0);
  ui_selectable_list_reset(NULL);
  ui_selectable_list_keep_visible(NULL, 1, 1);
  ui_selectable_list_move(NULL, 1, 1, 1);
}
Test(selectable_list_contract, selection_scrolls_at_edges_and_recovers_when_rows_or_items_shrink) {
  ui_selectable_list_t list = {0};
  ui_selectable_list_move(&list, 20, 5, 15);
  cr_assert_eq(list.selected, 15);
  cr_assert_eq(list.first_visible, 11);
  ui_selectable_list_move(&list, 20, 5, -13);
  cr_assert_eq(list.selected, 2);
  cr_assert_eq(list.first_visible, 2);
  ui_selectable_list_move(&list, 20, 5, 100);
  cr_assert_eq(list.selected, 19);
  cr_assert_eq(list.first_visible, 15);
  ui_selectable_list_keep_visible(&list, 3, 2);
  cr_assert_eq(list.selected, 2);
  cr_assert_eq(list.first_visible, 1);
  ui_selectable_list_keep_visible(&list, 3, 0);
  cr_assert_eq(list.selected, 2);
  cr_assert_eq(list.first_visible, 2);
  ui_selectable_list_move(&list, 3, 2, -100);
  cr_assert_eq(list.selected, 0);
  cr_assert_eq(list.first_visible, 0);
  list = (ui_selectable_list_t){.selected = -1, .first_visible = -1};
  ui_selectable_list_keep_visible(&list, 3, 10);
  cr_assert_eq(list.selected, 0);
  cr_assert_eq(list.first_visible, 0);
}
Test(selectable_list_contract, repeated_navigation_keeps_selection_inside_the_available_viewport) {
  for (int count = 1; count <= 30; ++count) {
    for (int rows = 1; rows <= 35; ++rows) {
      ui_selectable_list_t list = {0};
      for (int step = 0; step < 100; ++step) {
        int delta = step < 50 ? 3 : -2;
        ui_selectable_list_move(&list, count, rows, delta);
        cr_assert_geq(list.selected, 0);
        cr_assert_lt(list.selected, count);
        cr_assert_geq(list.first_visible, 0);
        cr_assert_leq(list.first_visible, list.selected);
        cr_assert_lt(list.selected, list.first_visible + rows);
        cr_assert_leq(list.first_visible, count > rows ? count - rows : 0);
      }
    }
  }
}
