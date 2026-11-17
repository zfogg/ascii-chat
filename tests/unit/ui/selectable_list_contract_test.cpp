#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/ui/selectable_list.h>

TEST_CASE("selectable_list_contract/reset_and_empty_lists_have_no_selected_viewport", "[selectable_list_contract] [unit]") {
  ui_selectable_list_t list = {.selected = 12, .first_visible = 8};
  ui_selectable_list_reset(&list);
  REQUIRE((list.selected) == (0));
  REQUIRE((list.first_visible) == (0));
  list = (ui_selectable_list_t){.selected = -9, .first_visible = 8};
  ui_selectable_list_keep_visible(&list, 0, 4);
  REQUIRE((list.selected) == (0));
  REQUIRE((list.first_visible) == (0));
  ui_selectable_list_reset(NULL);
  ui_selectable_list_keep_visible(NULL, 1, 1);
  ui_selectable_list_move(NULL, 1, 1, 1);
}
TEST_CASE("selectable_list_contract/selection_scrolls_at_edges_and_recovers_when_rows_or_items_shrink", "[selectable_list_contract] [unit]") {
  ui_selectable_list_t list = {0};
  ui_selectable_list_move(&list, 20, 5, 15);
  REQUIRE((list.selected) == (15));
  REQUIRE((list.first_visible) == (11));
  ui_selectable_list_move(&list, 20, 5, -13);
  REQUIRE((list.selected) == (2));
  REQUIRE((list.first_visible) == (2));
  ui_selectable_list_move(&list, 20, 5, 100);
  REQUIRE((list.selected) == (19));
  REQUIRE((list.first_visible) == (15));
  ui_selectable_list_keep_visible(&list, 3, 2);
  REQUIRE((list.selected) == (2));
  REQUIRE((list.first_visible) == (1));
  ui_selectable_list_keep_visible(&list, 3, 0);
  REQUIRE((list.selected) == (2));
  REQUIRE((list.first_visible) == (2));
  ui_selectable_list_move(&list, 3, 2, -100);
  REQUIRE((list.selected) == (0));
  REQUIRE((list.first_visible) == (0));
  list = (ui_selectable_list_t){.selected = -1, .first_visible = -1};
  ui_selectable_list_keep_visible(&list, 3, 10);
  REQUIRE((list.selected) == (0));
  REQUIRE((list.first_visible) == (0));
}
TEST_CASE("selectable_list_contract/repeated_navigation_keeps_selection_inside_the_available_viewport", "[selectable_list_contract] [unit]") {
  for (int count = 1; count <= 30; ++count) {
    for (int rows = 1; rows <= 35; ++rows) {
      ui_selectable_list_t list = {0};
      for (int step = 0; step < 100; ++step) {
        int delta = step < 50 ? 3 : -2;
        ui_selectable_list_move(&list, count, rows, delta);
        REQUIRE((list.selected) >= (0));
        REQUIRE((list.selected) < (count));
        REQUIRE((list.first_visible) >= (0));
        REQUIRE((list.first_visible) <= (list.selected));
        REQUIRE((list.selected) < (list.first_visible + rows));
        REQUIRE((list.first_visible) <= (count > rows ? count - rows : 0));
      }
    }
  }
}
