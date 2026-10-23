#include <criterion/criterion.h>
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

static void setup(void) {
  options_state_init();
}
static void teardown(void) {
  options_state_destroy();
}
TestSuite(ui_components, .init = setup, .fini = teardown);

Test(ui_components, explicit_utf8_overrides_detection_and_auto_uses_capabilities) {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  cr_assert_not(terminal_should_use_utf8());
  options.force_utf8 = UTF8_SETTING_TRUE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  cr_assert(terminal_should_use_utf8());
  options.force_utf8 = UTF8_SETTING_AUTO;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  cr_assert_eq(terminal_should_use_utf8(), terminal_supports_utf8());
}

Test(ui_components, popup_borders_follow_utf8_policy_and_no_color_is_plain) {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  options.color_mode = TERM_COLOR_NONE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  const char *lines[] = {"body"};
  ui_popup_t popup = {.title = "Test", .lines = lines, .line_count = 1};
  frame_buffer_t *buffer = frame_buffer_create(24, 80);
  cr_assert(ui_popup_render(buffer, (terminal_size_t){.rows = 24, .cols = 80}, &popup));
  cr_assert_not_null(strchr(frame_buffer_get_content(buffer), '+'));
  cr_assert_null(strstr(frame_buffer_get_content(buffer), "┌"));
  char keys[512];
  ui_help_keys(keys, sizeof(keys), "F12 / 0 | (Ctrl-U)");
  cr_assert_str_eq(keys, "F12 / 0 | (Ctrl-U)");
  frame_buffer_reset(buffer);
  options.force_utf8 = UTF8_SETTING_TRUE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  cr_assert(ui_popup_render(buffer, (terminal_size_t){.rows = 24, .cols = 80}, &popup));
  cr_assert_not_null(strstr(frame_buffer_get_content(buffer), "┌"));
  frame_buffer_destroy(buffer);
}

Test(ui_components, pages_and_vertical_scroll_are_independent) {
  ui_viewport_t v = {.page_size = 100, .selected = 50};
  ui_viewport_range_t r = ui_viewport_update(&v, 250, 20);
  cr_assert_eq(r.pages, 3);
  cr_assert_eq(r.page, 0);
  cr_assert_eq(r.first, 31);
  cr_assert_eq(r.count, 20);
  cr_assert(ui_viewport_key(&v, 250, 20, KEY_RIGHT));
  r = ui_viewport_update(&v, 250, 20);
  cr_assert_eq(v.selected, 150);
  cr_assert_eq(r.page, 1);
  cr_assert_eq(r.first, 131);
  cr_assert(ui_viewport_key(&v, 250, 20, KEY_END));
  r = ui_viewport_update(&v, 250, 20);
  cr_assert_eq(r.page, 2);
  cr_assert_eq(r.first, 230);
  cr_assert_eq(r.count, 20);
  r = ui_viewport_update(&v, 3, 2);
  cr_assert_eq(v.selected, 2);
  cr_assert_eq(r.first, 1);
  cr_assert_eq(r.count, 2);
}
Test(ui_components, viewport_empty_resize_and_unknown_keys) {
  ui_viewport_t v = {.selected = 400, .first_visible = 99};
  ui_viewport_range_t r = ui_viewport_update(&v, 0, 0);
  cr_assert_eq(r.count, 0);
  cr_assert_eq(v.selected, 0);
  cr_assert_eq(v.first_visible, 0);
  cr_assert_not(ui_viewport_key(&v, 10, 4, '?'));
  for (unsigned i = 0; i < 100; ++i)
    ui_viewport_key(&v, 10, 4, KEY_DOWN);
  cr_assert_eq(v.selected, 9);
  cr_assert_eq(v.first_visible, 6);
  ui_viewport_update(&v, 10, 20);
  cr_assert_eq(v.first_visible, 0);
}
Test(ui_components, scrollbar_thumb_is_proportional_and_reaches_both_ends) {
  ui_scrollbar_geometry_t g = ui_scrollbar_measure(100, 20, 0, 10);
  cr_assert(g.visible);
  cr_assert_eq(g.start, 0);
  cr_assert_eq(g.length, 2);
  g = ui_scrollbar_measure(100, 20, 80, 10);
  cr_assert_eq(g.start, 8);
  g = ui_scrollbar_measure(100, 20, 999, 10);
  cr_assert_eq(g.start, 8);
  g = ui_scrollbar_measure(20, 20, 0, 10);
  cr_assert_not(g.visible);
  g = ui_scrollbar_measure(SIZE_MAX, 1, SIZE_MAX - 1, 10);
  cr_assert_eq(g.length, 1);
  cr_assert_eq(g.start, 9);
}
Test(ui_components, unicode_and_ansi_cells_keep_column_boundaries) {
  char text[256];
  ui_text_cell(text, sizeof(text), "\033[31m界abc\033[0m", 4, UI_ALIGN_RIGHT);
  cr_assert(utf8_is_valid(text));
  cr_assert_eq(display_width(text), 4);
  cr_assert_not_null(strstr(text, "界ab"));
  cr_assert_null(strstr(text, "abc"));
  ui_text_cell(text, sizeof(text), "界", 1, UI_ALIGN_LEFT);
  cr_assert_str_eq(text, " ");
  ui_text_cell(text, sizeof(text), NULL, 3, UI_ALIGN_LEFT);
  cr_assert_str_eq(text, "   ");
}
Test(ui_components, table_columns_align_and_flexible_width_absorbs_remainder) {
  const ui_table_column_t columns[] = {{"Name", 0, UI_ALIGN_LEFT}, {"Count", 5, UI_ALIGN_RIGHT}};
  const char *cells[] = {"alpha", "42"};
  char line[256];
  ui_table_row(line, sizeof(line), columns, cells, 2, 20, true, true);
  cr_assert_eq(display_width(line), 20);
  cr_assert_eq(line[0], '>');
  cr_assert_not_null(strstr(line, "   42"));
}
Test(ui_components, unicode_line_editor_deletes_whole_codepoints) {
  char text[64] = "a界b";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  cr_assert_eq(cursor, 4);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  cr_assert_str_eq(text, "ab");
  cr_assert_eq(cursor, 1);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'X');
  cr_assert_str_eq(text, "aXb");
}
Test(ui_components, buttons_navigate_wrap_and_disabled_actions_do_not_activate) {
  const ui_button_t b[] = {{"Update", 'u', false}, {"Unavailable", 'x', true}, {"Close", 'c', false}};
  ui_buttons_t s = {0};
  cr_assert_eq(ui_buttons_key(&s, b, 3, KEY_LEFT), -1);
  cr_assert_eq(s.selected, 2);
  cr_assert_eq(ui_buttons_key(&s, b, 3, '\r'), 2);
  cr_assert_eq(ui_buttons_key(&s, b, 3, '\t'), -1);
  cr_assert_eq(s.selected, 0);
  cr_assert_eq(ui_buttons_key(&s, b, 3, 'x'), -1);
  cr_assert_eq(ui_buttons_key(&s, b, 3, 'c'), 2);
}
Test(ui_components, layout_never_returns_a_negative_body) {
  ui_layout_t layout;
  cr_assert_eq(ui_layout_measure((terminal_size_t){.rows = 24, .cols = 80}, 4, 3, 1, &layout), ASCIICHAT_OK);
  cr_assert_eq(layout.body_row, 5);
  cr_assert_eq(layout.body_rows, 16);
  cr_assert_eq(layout.footer_row, 24);
  cr_assert_eq(ui_layout_measure((terminal_size_t){1, 1}, 4, 3, 1, &layout), ASCIICHAT_OK);
  cr_assert_eq(layout.body_rows, 0);
  cr_assert_eq(ui_layout_measure((terminal_size_t){.rows = 24, .cols = 80}, -1, 0, 0, &layout), ERROR_INVALID_PARAM);
}
Test(ui_components, invalid_widget_arguments_set_context) {
  char out[16];
  CLEAR_ERRNO();
  ui_text_cell(NULL, 0, "x", 1, UI_ALIGN_LEFT);
  cr_assert(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_table_row(out, sizeof(out), NULL, NULL, 1, 20, false, false);
  cr_assert(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_viewport_update(NULL, 0, 0);
  cr_assert(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_buttons_key(NULL, NULL, 0, KEY_NONE);
  cr_assert(HAS_ERRNO_CODE(ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
  ui_progress_format(out, sizeof(out), 0.5, 100);
  cr_assert(HAS_ERRNO_CODE(ERROR_BUFFER_OVERFLOW));
}
Test(ui_components, help_measure_includes_settings_and_popup_chrome) {
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
  cr_assert_geq(size.cols, 40);
  cr_assert_eq(size.rows, 16);
}

Test(ui_components, help_keys_fit_small_plain_buffers) {
  options_t options = *options_get();
  options.color_mode = TERM_COLOR_NONE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  char keys[16];
  ui_help_keys(keys, sizeof(keys), "Esc / ?");
  cr_assert_str_eq(keys, "Esc / ?");
  CLEAR_ERRNO();
  ui_help_keys(keys, 2, "Escape");
  cr_assert(HAS_ERRNO_CODE(ERROR_BUFFER_OVERFLOW));
  cr_assert_str_eq(keys, "");
}

Test(ui_components, input_insertion_preserves_utf8_and_rejects_overflow_without_mutation) {
  char text[8] = "ab";
  size_t length = 2, cursor = 1;
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "界"), ASCIICHAT_OK);
  cr_assert_str_eq(text, "a界b");
  cr_assert_eq(cursor, 4);
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "long"), ERROR_BUFFER_OVERFLOW);
  cr_assert_str_eq(text, "a界b");
  cr_assert_eq(cursor, 4);
  frame_buffer_t *frame = frame_buffer_create(4, 20);
  cr_assert_eq(ui_input_render(frame, 2, 3, 6, 1, text, 2, false), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_input_render(frame, 2, 3, 6, 1, text, cursor, false), ASCIICHAT_OK);
  cr_assert(utf8_is_valid(frame_buffer_get_content(frame)));
  frame_buffer_destroy(frame);
}

Test(ui_components, table_ellipsis_follows_effective_utf8_setting) {
  options_t options = *options_get();
  options.force_utf8 = UTF8_SETTING_FALSE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  const ui_table_column_t column = {"Name", 8, UI_ALIGN_LEFT, true};
  const char *cells[] = {"abcdefghijk"};
  char line[128];
  ui_table_row(line, sizeof(line), &column, cells, 1, 8, false, false);
  cr_assert_not_null(strstr(line, "..."));
  cr_assert_eq(display_width(line), 8);
  options.force_utf8 = UTF8_SETTING_TRUE;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  ui_table_row(line, sizeof(line), &column, cells, 1, 8, false, false);
  cr_assert_not_null(strstr(line, "…"));
  cr_assert_eq(display_width(line), 8);
}
