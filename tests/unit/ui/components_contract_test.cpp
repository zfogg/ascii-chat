#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/ui/table.h>
#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/ui/layout.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/progress.h>
#include <ascii-chat/options/rcu.h>
#include <options/options.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>

#define terminal_should_use_utf8() terminal_should_use_utf8(static_cast<terminal_utf8_setting_t>(options_get()->force_utf8))
#define ui_viewport_key(view, count, rows, key) ui_viewport_key((view), (count), (rows), static_cast<keyboard_key_t>(key))
#define ui_input_edit_key(buffer, capacity, length, cursor, key) ui_input_edit_key((buffer), (capacity), (length), (cursor), static_cast<keyboard_key_t>(key))
#define ui_buttons_key(state, buttons, count, key) ui_buttons_key((state), (buttons), (count), static_cast<keyboard_key_t>(key))

static void setup(void) {
  options_state_init();
}
static void teardown(void) {
  options_state_destroy();
}
TEST_CASE("ui_components/explicit_utf8_overrides_detection_and_auto_uses_capabilities", "[ui_components] [unit]") {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  REQUIRE(!(terminal_should_use_utf8()));
  options.force_utf8 = UTF8_SETTING_TRUE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  REQUIRE(terminal_should_use_utf8());
  options.force_utf8 = UTF8_SETTING_AUTO;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  REQUIRE((terminal_should_use_utf8()) == (terminal_supports_utf8()));
}

TEST_CASE("ui_components/popup_borders_follow_utf8_policy_and_no_color_is_plain", "[ui_components] [unit]") {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  options.color_mode = TERM_COLOR_NONE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  const char *lines[] = {"body"};
  ui_popup_t popup = {.title = "Test", .lines = lines, .line_count = 1};
  frame_buffer_t *buffer = frame_buffer_create(24, 80);
  REQUIRE(ui_popup_render(buffer, (terminal_size_t){.rows = 24, .cols = 80}, &popup));
  REQUIRE((strchr(frame_buffer_get_content(buffer), '+')) != nullptr);
  REQUIRE((strstr(frame_buffer_get_content(buffer), "┌")) == nullptr);
  char keys[512];
  ui_help_keys(keys, sizeof(keys), "F12 / 0 | (Ctrl-U)");
  REQUIRE(std::strcmp((keys), ("F12 / 0 | (Ctrl-U)")) == 0);
  frame_buffer_reset(buffer);
  options.force_utf8 = UTF8_SETTING_TRUE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  REQUIRE(ui_popup_render(buffer, (terminal_size_t){.rows = 24, .cols = 80}, &popup));
  REQUIRE((strstr(frame_buffer_get_content(buffer), "┌")) != nullptr);
  frame_buffer_destroy(buffer);
}

TEST_CASE("ui_components/pages_and_vertical_scroll_are_independent", "[ui_components] [unit]") {
  ui_viewport_t v = {.page_size = 100, .selected = 50};
  ui_viewport_range_t r = ui_viewport_update(&v, 250, 20);
  REQUIRE((r.pages) == (3));
  REQUIRE((r.page) == (0));
  REQUIRE((r.first) == (31));
  REQUIRE((r.count) == (20));
  REQUIRE(ui_viewport_key(&v, 250, 20, KEY_RIGHT));
  r = ui_viewport_update(&v, 250, 20);
  REQUIRE((v.selected) == (150));
  REQUIRE((r.page) == (1));
  REQUIRE((r.first) == (131));
  REQUIRE(ui_viewport_key(&v, 250, 20, KEY_END));
  r = ui_viewport_update(&v, 250, 20);
  REQUIRE((r.page) == (2));
  REQUIRE((r.first) == (230));
  REQUIRE((r.count) == (20));
  r = ui_viewport_update(&v, 3, 2);
  REQUIRE((v.selected) == (2));
  REQUIRE((r.first) == (1));
  REQUIRE((r.count) == (2));
}
TEST_CASE("ui_components/viewport_empty_resize_and_unknown_keys", "[ui_components] [unit]") {
  ui_viewport_t v = {.selected = 400, .first_visible = 99};
  ui_viewport_range_t r = ui_viewport_update(&v, 0, 0);
  REQUIRE((r.count) == (0));
  REQUIRE((v.selected) == (0));
  REQUIRE((v.first_visible) == (0));
  REQUIRE(!(ui_viewport_key(&v, 10, 4, '?')));
  for (unsigned i = 0; i < 100; ++i)
    ui_viewport_key(&v, 10, 4, KEY_DOWN);
  REQUIRE((v.selected) == (9));
  REQUIRE((v.first_visible) == (6));
  ui_viewport_update(&v, 10, 20);
  REQUIRE((v.first_visible) == (0));
}
TEST_CASE("ui_components/scrollbar_thumb_is_proportional_and_reaches_both_ends", "[ui_components] [unit]") {
  ui_scrollbar_geometry_t g = ui_scrollbar_measure(100, 20, 0, 10);
  REQUIRE(g.visible);
  REQUIRE((g.start) == (0));
  REQUIRE((g.length) == (2));
  g = ui_scrollbar_measure(100, 20, 80, 10);
  REQUIRE((g.start) == (8));
  g = ui_scrollbar_measure(100, 20, 999, 10);
  REQUIRE((g.start) == (8));
  g = ui_scrollbar_measure(20, 20, 0, 10);
  REQUIRE(!(g.visible));
  g = ui_scrollbar_measure(SIZE_MAX, 1, SIZE_MAX - 1, 10);
  REQUIRE((g.length) == (1));
  REQUIRE((g.start) == (9));
}
TEST_CASE("ui_components/unicode_and_ansi_cells_keep_column_boundaries", "[ui_components] [unit]") {
  char text[256];
  ui_text_cell(text, sizeof(text), "\033[31m界abc\033[0m", 4, UI_ALIGN_RIGHT);
  REQUIRE(utf8_is_valid(text));
  REQUIRE((display_width(text)) == (4));
  REQUIRE((strstr(text, "界ab")) != nullptr);
  REQUIRE((strstr(text, "abc")) == nullptr);
  ui_text_cell(text, sizeof(text), "界", 1, UI_ALIGN_LEFT);
  REQUIRE(std::strcmp((text), (" ")) == 0);
  ui_text_cell(text, sizeof(text), NULL, 3, UI_ALIGN_LEFT);
  REQUIRE(std::strcmp((text), ("   ")) == 0);
}
TEST_CASE("ui_components/table_columns_align_and_flexible_width_absorbs_remainder", "[ui_components] [unit]") {
  const ui_table_column_t columns[] = {{"Name", 0, UI_ALIGN_LEFT}, {"Count", 5, UI_ALIGN_RIGHT}};
  const char *cells[] = {"alpha", "42"};
  char line[256];
  ui_table_row(line, sizeof(line), columns, cells, 2, 20, true, true);
  REQUIRE((display_width(line)) == (20));
  REQUIRE((line[0]) == ('>'));
  REQUIRE((strstr(line, "   42")) != nullptr);
}
TEST_CASE("ui_components/unicode_line_editor_deletes_whole_codepoints", "[ui_components] [unit]") {
  char text[64] = "a界b";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  REQUIRE((cursor) == (4));
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  REQUIRE(std::strcmp((text), ("ab")) == 0);
  REQUIRE((cursor) == (1));
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'X');
  REQUIRE(std::strcmp((text), ("aXb")) == 0);
}
TEST_CASE("ui_components/buttons_navigate_wrap_and_disabled_actions_do_not_activate", "[ui_components] [unit]") {
  const ui_button_t b[] = {{"Update", static_cast<keyboard_key_t>('u'), false}, {"Unavailable", static_cast<keyboard_key_t>('x'), true}, {"Close", static_cast<keyboard_key_t>('c'), false}};
  ui_buttons_t s = {0};
  REQUIRE((ui_buttons_key(&s, b, 3, KEY_LEFT)) == (-1));
  REQUIRE((s.selected) == (2));
  REQUIRE((ui_buttons_key(&s, b, 3, '\r')) == (2));
  REQUIRE((ui_buttons_key(&s, b, 3, '\t')) == (-1));
  REQUIRE((s.selected) == (0));
  REQUIRE((ui_buttons_key(&s, b, 3, 'x')) == (-1));
  REQUIRE((ui_buttons_key(&s, b, 3, 'c')) == (2));
}
TEST_CASE("ui_components/layout_never_returns_a_negative_body", "[ui_components] [unit]") {
  ui_layout_t layout;
  REQUIRE((ui_layout_measure((terminal_size_t){.rows = 24, .cols = 80}, 4, 3, 1, &layout)) == (ASCIICHAT_OK));
  REQUIRE((layout.body_row) == (5));
  REQUIRE((layout.body_rows) == (16));
  REQUIRE((layout.footer_row) == (24));
  REQUIRE((ui_layout_measure((terminal_size_t){1, 1}, 4, 3, 1, &layout)) == (ASCIICHAT_OK));
  REQUIRE((layout.body_rows) == (0));
  REQUIRE((ui_layout_measure((terminal_size_t){.rows = 24, .cols = 80}, -1, 0, 0, &layout)) == (ERROR_INVALID_PARAM));
}
TEST_CASE("ui_components/invalid_widget_arguments_set_context", "[ui_components] [unit]") {
  char out[16];
  CLEAR_ERRNO();
  ui_text_cell(NULL, 0, "x", 1, UI_ALIGN_LEFT);
  REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_table_row(out, sizeof(out), NULL, NULL, 1, 20, false, false);
  REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_viewport_update(NULL, 0, 0);
  REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_buttons_key(NULL, NULL, 0, KEY_NONE);
  REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_progress_format(out, sizeof(out), 0.5, 100);
  REQUIRE(HAS_ERRNO_CODE(ERROR_BUFFER_OVERFLOW));
}
TEST_CASE("ui_components/help_measure_includes_settings_and_popup_chrome", "[ui_components] [unit]") {
  const ui_help_entry_t entries[] = {{"Ctrl-U / Left", "Older logs"}, {"0", "Sync primitives"}};
  const char *settings[] = {"Current Settings:", "Volume : 100%", "Audio : enabled"};
  ui_help_menu_t m = {.title = "Help", .entries = entries, .count = 2, .settings = settings, .settings_count = 3, .footer = "? closes", .top_margin = 5};
  terminal_size_t size = ui_help_menu_measure(&m);
  REQUIRE((size.cols) >= (40));
  REQUIRE((size.rows) == (16));
}

TEST_CASE("ui_components/help_keys_fit_small_plain_buffers", "[ui_components] [unit]") {
  options_t options = *options_get();
  options.color_mode = TERM_COLOR_NONE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  char keys[16];
  ui_help_keys(keys, sizeof(keys), "Esc / ?");
  REQUIRE(std::strcmp((keys), ("Esc / ?")) == 0);
  CLEAR_ERRNO();
  ui_help_keys(keys, 2, "Escape");
  REQUIRE(HAS_ERRNO_CODE(ERROR_BUFFER_OVERFLOW));
  REQUIRE(std::strcmp((keys), ("")) == 0);
}

TEST_CASE("ui_components/input_insertion_preserves_utf8_and_rejects_overflow_without_mutation", "[ui_components] [unit]") {
  char text[8] = "ab";
  size_t length = 2, cursor = 1;
  REQUIRE((ui_input_insert(text, sizeof(text), &length, &cursor, "界")) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((text), ("a界b")) == 0);
  REQUIRE((cursor) == (4));
  REQUIRE((ui_input_insert(text, sizeof(text), &length, &cursor, "long")) == (ERROR_BUFFER_OVERFLOW));
  REQUIRE(std::strcmp((text), ("a界b")) == 0);
  REQUIRE((cursor) == (4));
  frame_buffer_t *frame = frame_buffer_create(4, 20);
  REQUIRE((ui_input_render(frame, 2, 3, 6, 1, text, 2, false)) == (ERROR_INVALID_PARAM));
  REQUIRE((ui_input_render(frame, 2, 3, 6, 1, text, cursor, false)) == (ASCIICHAT_OK));
  REQUIRE(utf8_is_valid(frame_buffer_get_content(frame)));
  frame_buffer_destroy(frame);
}

TEST_CASE("ui_components/table_ellipsis_follows_effective_utf8_setting", "[ui_components] [unit]") {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  const ui_table_column_t column = {"Name", 8, UI_ALIGN_LEFT, true};
  const char *cells[] = {"abcdefghijk"};
  char line[128];
  ui_table_row(line, sizeof(line), &column, cells, 1, 8, false, false);
  REQUIRE((strstr(line, "...")) != nullptr);
  REQUIRE((display_width(line)) == (8));
  options.force_utf8 = UTF8_SETTING_TRUE;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  ui_table_row(line, sizeof(line), &column, cells, 1, 8, false, false);
  REQUIRE((strstr(line, "…")) != nullptr);
  REQUIRE((display_width(line)) == (8));
}
