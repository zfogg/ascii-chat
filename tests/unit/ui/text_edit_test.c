#include <criterion/criterion.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/platform/keyboard.h>
#include <string.h>

Test(text_edit, insertion_is_atomic_and_keeps_valid_utf8) {
  char text[16] = "a界b";
  size_t length = strlen(text), cursor = 1;
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "é😀"), ASCIICHAT_OK);
  cr_assert_str_eq(text, "aé😀界b");
  cr_assert_eq(cursor, 7);
  cr_assert_eq(length, 11);
  const char *bad[] = {"\x80", "\xc3", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
    cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, bad[i]), ERROR_INVALID_PARAM);
    cr_assert_str_eq(text, "aé😀界b");
    cr_assert_eq(length, 11);
    cr_assert_eq(cursor, 7);
  }
  cr_assert_eq(ui_input_insert(text, 12, &length, &cursor, "é"), ERROR_BUFFER_OVERFLOW);
  cr_assert_str_eq(text, "aé😀界b");
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, ""), ASCIICHAT_OK);
}

Test(text_edit, invalid_storage_and_mid_character_cursors_are_rejected) {
  char text[8] = "é";
  size_t length = 2, cursor = 1;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_DELETE);
  cr_assert_str_eq(text, "é");
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cursor = 2;
  cr_assert_eq(ui_input_insert(NULL, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_input_insert(text, sizeof(text), NULL, &cursor, "x"), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, NULL, "x"), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_input_insert(text, 2, &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cursor = 3;
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cursor = 0;
  length = 1;
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  length = 2;
  text[0] = (char)0xc0;
  cr_assert_eq(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'x');
  text[0] = (char)0xc3;
  ui_input_edit_key(NULL, sizeof(text), &length, &cursor, 'x');
  ui_input_edit_key(text, 0, &length, &cursor, 'x');
  ui_input_edit_key(text, sizeof(text), NULL, &cursor, 'x');
  ui_input_edit_key(text, sizeof(text), &length, NULL, 'x');
  ui_input_edit_key(text, 2, &length, &cursor, 'x');
  cursor = 3;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'x');
  cursor = 0;
  length = 1;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'x');
  cr_assert_str_eq(text, "é");
}

Test(text_edit, boundaries_word_operations_and_ignored_keys) {
  char text[64] = "é  界 😀";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  cr_assert_eq(cursor, 8);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_RIGHT);
  cr_assert_eq(cursor, length);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_RIGHT);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_DELETE);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_HOME);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 8);
  cr_assert_eq(cursor, 0);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_CTRL_DELETE);
  cr_assert_str_eq(text, "界 😀");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_END);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_CTRL_DELETE);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
  cr_assert_str_eq(text, "界 ");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
  cr_assert_str_eq(text, "");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_DELETE);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_RIGHT);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_UP);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 1);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, (keyboard_key_t)0x80);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, (keyboard_key_t)(KEY_UNICODE_BASE + 0x1f600));
  cr_assert_str_eq(text, "😀");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, '\t');
  cr_assert_str_eq(text, "😀\t");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 8);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  cr_assert_str_eq(text, "");
}

Test(text_edit, combining_marks_and_emoji_sequences_preserve_codepoint_boundaries) {
  char text[64] = "e\xcc\x81👩‍💻";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  cr_assert_str_eq(text, "e\xcc\x81👩‍");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  cr_assert_str_eq(text, "e\xcc\x81👩");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  cr_assert_str_eq(text, "e");
}
