#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/ui/table.h>
#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/ui/layout.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/progress.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>

#define terminal_should_use_utf8() terminal_should_use_utf8(static_cast<terminal_utf8_setting_t>(options_get()->force_utf8))
#define ui_viewport_key(view, count, rows, key) ui_viewport_key((view), (count), (rows), static_cast<keyboard_key_t>(key))
#define ui_input_edit_key(buffer, capacity, length, cursor, key) \
  ui_input_edit_key((buffer), (capacity), (length), (cursor), static_cast<keyboard_key_t>(key))
#define ui_buttons_key(state, buttons, count, key) ui_buttons_key((state), (buttons), (count), static_cast<keyboard_key_t>(key))

static void setup(void) {
  options_state_init();
}
static void teardown(void) {
  options_state_destroy();
}
TEST_CASE("ui_components/explicit_utf8_overrides_detection_and_auto_uses_capabilities", "[ui_components]") {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(terminal_should_use_utf8());
  options.force_utf8 = UTF8_SETTING_TRUE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(terminal_should_use_utf8());
  options.force_utf8 = UTF8_SETTING_AUTO;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(terminal_should_use_utf8(), terminal_supports_utf8());
}

TEST_CASE("ui_components/popup_borders_follow_utf8_policy_and_no_color_is_plain", "[ui_components]") {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  options.color_mode = TERM_COLOR_NONE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  const char *lines[] = {"body"};
  ui_popup_t popup = {.title = "Test", .lines = lines, .line_count = 1};
  frame_buffer_t *buffer = frame_buffer_create(24, 80);
  ASCIICHAT_REQUIRE(ui_popup_render(buffer, (terminal_size_t){.rows = 24, .cols = 80}, &popup));
  ASCIICHAT_REQUIRE_NOT_NULL(strchr(frame_buffer_get_content(buffer), '+'));
  ASCIICHAT_REQUIRE_NULL(strstr(frame_buffer_get_content(buffer), "┌"));
  char keys[512];
  ui_help_keys(keys, sizeof(keys), "F12 / 0 | (Ctrl-U)");
  ASCIICHAT_REQUIRE_STR_EQ(keys, "F12 / 0 | (Ctrl-U)");
  frame_buffer_reset(buffer);
  options.force_utf8 = UTF8_SETTING_TRUE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(ui_popup_render(buffer, (terminal_size_t){.rows = 24, .cols = 80}, &popup));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(frame_buffer_get_content(buffer), "┌"));
  frame_buffer_destroy(buffer);
}

TEST_CASE("ui_components/pages_and_vertical_scroll_are_independent", "[ui_components]") {
  ui_viewport_t v = {.page_size = 100, .selected = 50};
  ui_viewport_range_t r = ui_viewport_update(&v, 250, 20);
  ASCIICHAT_REQUIRE_EQ(r.pages, 3);
  ASCIICHAT_REQUIRE_EQ(r.page, 0);
  ASCIICHAT_REQUIRE_EQ(r.first, 31);
  ASCIICHAT_REQUIRE_EQ(r.count, 20);
  ASCIICHAT_REQUIRE(ui_viewport_key(&v, 250, 20, KEY_RIGHT));
  r = ui_viewport_update(&v, 250, 20);
  ASCIICHAT_REQUIRE_EQ(v.selected, 150);
  ASCIICHAT_REQUIRE_EQ(r.page, 1);
  ASCIICHAT_REQUIRE_EQ(r.first, 131);
  ASCIICHAT_REQUIRE(ui_viewport_key(&v, 250, 20, KEY_END));
  r = ui_viewport_update(&v, 250, 20);
  ASCIICHAT_REQUIRE_EQ(r.page, 2);
  ASCIICHAT_REQUIRE_EQ(r.first, 230);
  ASCIICHAT_REQUIRE_EQ(r.count, 20);
  r = ui_viewport_update(&v, 3, 2);
  ASCIICHAT_REQUIRE_EQ(v.selected, 2);
  ASCIICHAT_REQUIRE_EQ(r.first, 1);
  ASCIICHAT_REQUIRE_EQ(r.count, 2);
}
TEST_CASE("ui_components/viewport_empty_resize_and_unknown_keys", "[ui_components]") {
  ui_viewport_t v = {.selected = 400, .first_visible = 99};
  ui_viewport_range_t r = ui_viewport_update(&v, 0, 0);
  ASCIICHAT_REQUIRE_EQ(r.count, 0);
  ASCIICHAT_REQUIRE_EQ(v.selected, 0);
  ASCIICHAT_REQUIRE_EQ(v.first_visible, 0);
  ASCIICHAT_REQUIRE_NOT(ui_viewport_key(&v, 10, 4, '?'));
  for (unsigned i = 0; i < 100; ++i)
    ui_viewport_key(&v, 10, 4, KEY_DOWN);
  ASCIICHAT_REQUIRE_EQ(v.selected, 9);
  ASCIICHAT_REQUIRE_EQ(v.first_visible, 6);
  ui_viewport_update(&v, 10, 20);
  ASCIICHAT_REQUIRE_EQ(v.first_visible, 0);
}
TEST_CASE("ui_components/scrollbar_thumb_is_proportional_and_reaches_both_ends", "[ui_components]") {
  ui_scrollbar_geometry_t g = ui_scrollbar_measure(100, 20, 0, 10);
  ASCIICHAT_REQUIRE(g.visible);
  ASCIICHAT_REQUIRE_EQ(g.start, 0);
  ASCIICHAT_REQUIRE_EQ(g.length, 2);
  g = ui_scrollbar_measure(100, 20, 80, 10);
  ASCIICHAT_REQUIRE_EQ(g.start, 8);
  g = ui_scrollbar_measure(100, 20, 999, 10);
  ASCIICHAT_REQUIRE_EQ(g.start, 8);
  g = ui_scrollbar_measure(20, 20, 0, 10);
  ASCIICHAT_REQUIRE_NOT(g.visible);
  g = ui_scrollbar_measure(SIZE_MAX, 1, SIZE_MAX - 1, 10);
  ASCIICHAT_REQUIRE_EQ(g.length, 1);
  ASCIICHAT_REQUIRE_EQ(g.start, 9);
}
TEST_CASE("ui_components/unicode_and_ansi_cells_keep_column_boundaries", "[ui_components]") {
  char text[256];
  ui_text_cell(text, sizeof(text), "\033[31m界abc\033[0m", 4, UI_ALIGN_RIGHT);
  ASCIICHAT_REQUIRE(utf8_is_valid(text));
  ASCIICHAT_REQUIRE_EQ(display_width(text), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "界ab"));
  ASCIICHAT_REQUIRE_NULL(strstr(text, "abc"));
  ui_text_cell(text, sizeof(text), "界", 1, UI_ALIGN_LEFT);
  ASCIICHAT_REQUIRE_STR_EQ(text, " ");
  ui_text_cell(text, sizeof(text), NULL, 3, UI_ALIGN_LEFT);
  ASCIICHAT_REQUIRE_STR_EQ(text, "   ");
}
TEST_CASE("ui_components/table_columns_align_and_flexible_width_absorbs_remainder", "[ui_components]") {
  const ui_table_column_t columns[] = {{"Name", 0, UI_ALIGN_LEFT}, {"Count", 5, UI_ALIGN_RIGHT}};
  const char *cells[] = {"alpha", "42"};
  char line[256];
  ui_table_row(line, sizeof(line), columns, cells, 2, 20, true, true);
  ASCIICHAT_REQUIRE_EQ(display_width(line), 20);
  ASCIICHAT_REQUIRE_EQ(line[0], '>');
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(line, "   42"));
}
TEST_CASE("ui_components/unicode_line_editor_deletes_whole_codepoints", "[ui_components]") {
  char text[64] = "a界b";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(cursor, 4);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ASCIICHAT_REQUIRE_STR_EQ(text, "ab");
  ASCIICHAT_REQUIRE_EQ(cursor, 1);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'X');
  ASCIICHAT_REQUIRE_STR_EQ(text, "aXb");
}
TEST_CASE("ui_components/buttons_navigate_wrap_and_disabled_actions_do_not_activate", "[ui_components]") {
  const ui_button_t b[] = {{"Update", static_cast<keyboard_key_t>('u'), false},
                           {"Unavailable", static_cast<keyboard_key_t>('x'), true},
                           {"Close", static_cast<keyboard_key_t>('c'), false}};
  ui_buttons_t s = {0};
  ASCIICHAT_REQUIRE_EQ(ui_buttons_key(&s, b, 3, KEY_LEFT), -1);
  ASCIICHAT_REQUIRE_EQ(s.selected, 2);
  ASCIICHAT_REQUIRE_EQ(ui_buttons_key(&s, b, 3, '\r'), 2);
  ASCIICHAT_REQUIRE_EQ(ui_buttons_key(&s, b, 3, '\t'), -1);
  ASCIICHAT_REQUIRE_EQ(s.selected, 0);
  ASCIICHAT_REQUIRE_EQ(ui_buttons_key(&s, b, 3, 'x'), -1);
  ASCIICHAT_REQUIRE_EQ(ui_buttons_key(&s, b, 3, 'c'), 2);
}
TEST_CASE("ui_components/layout_never_returns_a_negative_body", "[ui_components]") {
  ui_layout_t layout;
  ASCIICHAT_REQUIRE_EQ(ui_layout_measure((terminal_size_t){.rows = 24, .cols = 80}, 4, 3, 1, &layout), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(layout.body_row, 5);
  ASCIICHAT_REQUIRE_EQ(layout.body_rows, 16);
  ASCIICHAT_REQUIRE_EQ(layout.footer_row, 24);
  ASCIICHAT_REQUIRE_EQ(ui_layout_measure((terminal_size_t){1, 1}, 4, 3, 1, &layout), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(layout.body_rows, 0);
  ASCIICHAT_REQUIRE_EQ(ui_layout_measure((terminal_size_t){.rows = 24, .cols = 80}, -1, 0, 0, &layout), ERROR_INVALID_PARAM);
}
TEST_CASE("ui_components/invalid_widget_arguments_set_context", "[ui_components]") {
  char out[16];
  CLEAR_ERRNO();
  ui_text_cell(NULL, 0, "x", 1, UI_ALIGN_LEFT);
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_table_row(out, sizeof(out), NULL, NULL, 1, 20, false, false);
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_viewport_update(NULL, 0, 0);
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_buttons_key(NULL, NULL, 0, KEY_NONE);
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_progress_format(out, sizeof(out), 0.5, 100);
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_BUFFER_OVERFLOW));
}
TEST_CASE("ui_components/help_measure_includes_settings_and_popup_chrome", "[ui_components]") {
  const ui_help_entry_t entries[] = {{"Ctrl-U / Left", "Older logs"}, {"0", "Sync primitives"}};
  const char *settings[] = {"Current Settings:", "Volume : 100%", "Audio : enabled"};
  ui_help_menu_t m = {.title = "Help",
                      .entries = entries,
                      .count = 2,
                      .settings = settings,
                      .settings_count = 3,
                      .footer = "? closes",
                      .top_margin = 5};
  terminal_size_t size = ui_help_menu_measure(&m);
  ASCIICHAT_REQUIRE_GE(size.cols, 40);
  ASCIICHAT_REQUIRE_EQ(size.rows, 16);
}

TEST_CASE("ui_components/help_keys_fit_small_plain_buffers", "[ui_components]") {
  options_t options = *options_get();
  options.color_mode = TERM_COLOR_NONE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  char keys[16];
  ui_help_keys(keys, sizeof(keys), "Esc / ?");
  ASCIICHAT_REQUIRE_STR_EQ(keys, "Esc / ?");
  CLEAR_ERRNO();
  ui_help_keys(keys, 2, "Escape");
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_BUFFER_OVERFLOW));
  ASCIICHAT_REQUIRE_STR_EQ(keys, "");
}

TEST_CASE("ui_components/input_insertion_preserves_utf8_and_rejects_overflow_without_mutation", "[ui_components]") {
  char text[8] = "ab";
  size_t length = 2, cursor = 1;
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "界"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(text, "a界b");
  ASCIICHAT_REQUIRE_EQ(cursor, 4);
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "long"), ERROR_BUFFER_OVERFLOW);
  ASCIICHAT_REQUIRE_STR_EQ(text, "a界b");
  ASCIICHAT_REQUIRE_EQ(cursor, 4);
  frame_buffer_t *frame = frame_buffer_create(4, 20);
  ASCIICHAT_REQUIRE_EQ(ui_input_render(frame, 2, 3, 6, 1, text, 2, false), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_input_render(frame, 2, 3, 6, 1, text, cursor, false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(utf8_is_valid(frame_buffer_get_content(frame)));
  frame_buffer_destroy(frame);
}

TEST_CASE("ui_components/table_ellipsis_follows_effective_utf8_setting", "[ui_components]") {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  const ui_table_column_t column = {"Name", 8, UI_ALIGN_LEFT, true};
  const char *cells[] = {"abcdefghijk"};
  char line[128];
  ui_table_row(line, sizeof(line), &column, cells, 1, 8, false, false);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(line, "..."));
  ASCIICHAT_REQUIRE_EQ(display_width(line), 8);
  options.force_utf8 = UTF8_SETTING_TRUE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ui_table_row(line, sizeof(line), &column, cells, 1, 8, false, false);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(line, "…"));
  ASCIICHAT_REQUIRE_EQ(display_width(line), 8);
}
