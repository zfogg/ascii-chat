#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tui/too_small.h>
#include <string.h>

TEST_CASE("ui_too_small/media_boundaries", "[ui_too_small]") {
  terminal_size_t minimum = {.cols = UI_MEDIA_MIN_COLS, .rows = UI_MEDIA_MIN_ROWS};
  ASCIICHAT_REQUIRE_NOT(ui_too_small(minimum, minimum));
  ASCIICHAT_REQUIRE(ui_too_small((terminal_size_t){.cols = 19, .rows = 10}, minimum));
  ASCIICHAT_REQUIRE(ui_too_small((terminal_size_t){.cols = 20, .rows = 9}, minimum));
  ASCIICHAT_REQUIRE_NOT(ui_too_small((terminal_size_t){.cols = 80, .rows = 24}, minimum));
  // Unknown measurements must not be interpreted as a real 0x0 terminal.
  ASCIICHAT_REQUIRE_NOT(ui_too_small((terminal_size_t){0}, minimum));
}

TEST_CASE("ui_too_small/one_cell_has_no_newline_or_out_of_bounds_cursor", "[ui_too_small]") {
  frame_buffer_t *buffer = frame_buffer_create(1, 1);
  ASCIICHAT_REQUIRE_NOT_NULL(buffer);
  ui_too_small_render(buffer, (terminal_size_t){.cols = 1, .rows = 1}, (terminal_size_t){.cols = 20, .rows = 10});
  const char expected[] = "\033[0m\033[2J\033[?25l\033[1;1HT";
  ASCIICHAT_REQUIRE_EQ(frame_buffer_get_length(buffer), sizeof(expected) - 1);
  ASCIICHAT_REQUIRE_EQ(memcmp(frame_buffer_get_content(buffer), expected, sizeof(expected) - 1), 0);
  frame_buffer_destroy(buffer);
}

TEST_CASE("ui_too_small/unknown_dimensions_emit_nothing", "[ui_too_small]") {
  frame_buffer_t *buffer = frame_buffer_create(1, 1);
  ASCIICHAT_REQUIRE_NOT_NULL(buffer);
  ui_too_small_render(buffer, (terminal_size_t){0}, (terminal_size_t){.cols = 20, .rows = 10});
  ASCIICHAT_REQUIRE_EQ(frame_buffer_get_length(buffer), 0);
  frame_buffer_destroy(buffer);
}
