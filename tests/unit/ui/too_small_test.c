#include <criterion/criterion.h>
#include <ascii-chat/ui/too_small.h>
#include <string.h>

Test(ui_too_small, media_boundaries) {
  terminal_size_t minimum = {.cols = UI_MEDIA_MIN_COLS, .rows = UI_MEDIA_MIN_ROWS};
  cr_assert_not(ui_too_small(minimum, minimum));
  cr_assert(ui_too_small((terminal_size_t){.cols = 19, .rows = 10}, minimum));
  cr_assert(ui_too_small((terminal_size_t){.cols = 20, .rows = 9}, minimum));
  cr_assert_not(ui_too_small((terminal_size_t){.cols = 80, .rows = 24}, minimum));
  // Unknown measurements must not be interpreted as a real 0x0 terminal.
  cr_assert_not(ui_too_small((terminal_size_t){0}, minimum));
}

Test(ui_too_small, one_cell_has_no_newline_or_out_of_bounds_cursor) {
  frame_buffer_t *buffer = frame_buffer_create(1, 1);
  cr_assert_not_null(buffer);
  ui_too_small_render(buffer, (terminal_size_t){.cols = 1, .rows = 1}, (terminal_size_t){.cols = 20, .rows = 10});
  const char expected[] = "\033[0m\033[2J\033[?25l\033[1;1HT";
  cr_assert_eq(frame_buffer_get_length(buffer), sizeof(expected) - 1);
  cr_assert_eq(memcmp(frame_buffer_get_content(buffer), expected, sizeof(expected) - 1), 0);
  frame_buffer_destroy(buffer);
}

Test(ui_too_small, unknown_dimensions_emit_nothing) {
  frame_buffer_t *buffer = frame_buffer_create(1, 1);
  cr_assert_not_null(buffer);
  ui_too_small_render(buffer, (terminal_size_t){0}, (terminal_size_t){.cols = 20, .rows = 10});
  cr_assert_eq(frame_buffer_get_length(buffer), 0);
  frame_buffer_destroy(buffer);
}
