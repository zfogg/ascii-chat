#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/platform/keyboard.h>
#include <string.h>

typedef struct {
  char text[32];
  size_t length, cursor;
  keyboard_line_edit_opts_t opts;
} editor_t;

static void start(editor_t *edit, const char *text, size_t cursor) {
  memset(edit, 0, sizeof(*edit));
  memcpy(edit->text, text, strlen(text) + 1);
  edit->length = strlen(text);
  edit->cursor = cursor;
  edit->opts = (keyboard_line_edit_opts_t){
      .buffer = edit->text, .max_len = sizeof(edit->text), .len = &edit->length, .cursor = &edit->cursor};
}

static keyboard_line_edit_result_t key(editor_t *edit, int input) {
  edit->opts.key = static_cast<keyboard_key_t>(input);
  return keyboard_apply_key(&edit->opts);
}

static void expect(editor_t *edit, const char *text, size_t cursor) {
  ASCIICHAT_REQUIRE_STR_EQ(edit->text, text);
  ASCIICHAT_REQUIRE_EQ(edit->length, strlen(text));
  ASCIICHAT_REQUIRE_EQ(edit->cursor, cursor);
}

TEST_CASE("keyboard_edit/insertion_navigation_and_forward_delete", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "ac", 1);
  ASCIICHAT_REQUIRE_EQ(key(&edit, 'b'), LINE_EDIT_CONTINUE);
  expect(&edit, "abc", 2);
  key(&edit, KEY_HOME);
  key(&edit, KEY_LEFT);
  expect(&edit, "abc", 0);
  key(&edit, KEY_DELETE);
  expect(&edit, "bc", 0);
  key(&edit, KEY_END);
  key(&edit, KEY_RIGHT);
  key(&edit, KEY_DELETE);
  expect(&edit, "bc", 2);
  key(&edit, KEY_LEFT);
  key(&edit, '\t');
  expect(&edit, "b\tc", 2);
  edit.opts.max_len = edit.length + 1;
  key(&edit, 'x');
  expect(&edit, "b\tc", 2);
}

TEST_CASE("keyboard_edit/backspace_removes_before_cursor_without_losing_suffix", "[keyboard_edit]") {
  const keyboard_key_t backspaces[] = {static_cast<keyboard_key_t>(8), static_cast<keyboard_key_t>(127)};
  for (size_t i = 0; i < 2; ++i) {
    editor_t edit;
    start(&edit, "abc", 2);
    ASCIICHAT_REQUIRE_EQ(key(&edit, backspaces[i]), LINE_EDIT_CONTINUE);
    expect(&edit, "ac", 1);
    key(&edit, backspaces[i]);
    expect(&edit, "c", 0);
    ASCIICHAT_REQUIRE_EQ(key(&edit, backspaces[i]), LINE_EDIT_CONTINUE);
    expect(&edit, "c", 0);
    key(&edit, KEY_DELETE);
    expect(&edit, "", 0);
    ASCIICHAT_REQUIRE_EQ(key(&edit, backspaces[i]), LINE_EDIT_CANCELLED);
  }
}

TEST_CASE("keyboard_edit/word_deletion_keeps_suffix_and_updates_cursor", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "one  two three", 9);
  key(&edit, 23);
  expect(&edit, "one  three", 5);
  key(&edit, 23);
  expect(&edit, "three", 0);
  ASCIICHAT_REQUIRE_EQ(key(&edit, 23), LINE_EDIT_CONTINUE);
  expect(&edit, "three", 0);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "", 0);
  ASCIICHAT_REQUIRE_EQ(key(&edit, 23), LINE_EDIT_CANCELLED);
  start(&edit, "one  two\tthree", 0);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "two\tthree", 0);
  key(&edit, KEY_RIGHT);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "tthree", 1);
  key(&edit, KEY_END);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "tthree", 6);
}

TEST_CASE("keyboard_edit/completion_cancellation_and_ignored_keys_do_not_mutate_text", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "keep", 2);
  ASCIICHAT_REQUIRE_EQ(key(&edit, '\r'), LINE_EDIT_ACCEPTED);
  ASCIICHAT_REQUIRE_EQ(key(&edit, '\n'), LINE_EDIT_ACCEPTED);
  ASCIICHAT_REQUIRE_EQ(key(&edit, KEY_ESCAPE), LINE_EDIT_CANCELLED);
  ASCIICHAT_REQUIRE_EQ(key(&edit, 3), LINE_EDIT_CANCELLED);
  ASCIICHAT_REQUIRE_EQ(key(&edit, KEY_NONE), LINE_EDIT_NO_INPUT);
  ASCIICHAT_REQUIRE_EQ(key(&edit, 1), LINE_EDIT_CONTINUE);
  ASCIICHAT_REQUIRE_EQ(key(&edit, KEY_UP), LINE_EDIT_CONTINUE);
  expect(&edit, "keep", 2);
}

TEST_CASE("keyboard_edit/missing_state_is_rejected", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "", 0);
  edit.opts.key = static_cast<keyboard_key_t>('x');
  ASCIICHAT_REQUIRE_EQ(keyboard_apply_key(NULL), LINE_EDIT_NO_INPUT);
  keyboard_line_edit_opts_t invalid = edit.opts;
  invalid.buffer = NULL;
  ASCIICHAT_REQUIRE_EQ(keyboard_apply_key(&invalid), LINE_EDIT_NO_INPUT);
  invalid = edit.opts;
  invalid.len = NULL;
  ASCIICHAT_REQUIRE_EQ(keyboard_apply_key(&invalid), LINE_EDIT_NO_INPUT);
  invalid = edit.opts;
  invalid.cursor = NULL;
  ASCIICHAT_REQUIRE_EQ(keyboard_apply_key(&invalid), LINE_EDIT_NO_INPUT);
  expect(&edit, "", 0);
}

TEST_CASE("keyboard_edit/unicode_movement_deletion_and_insertion_share_ui_editor", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "a界😀b", 8);
  key(&edit, KEY_LEFT);
  expect(&edit, "a界😀b", 4);
  key(&edit, 127);
  expect(&edit, "a😀b", 1);
  key(&edit, KEY_DELETE);
  expect(&edit, "ab", 1);
  key(&edit, (keyboard_key_t)(0xe9));
  expect(&edit, "aéb", 3);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "aé", 3);
  key(&edit, 23);
  expect(&edit, "", 0);
  start(&edit, "界 😀 tail", 0);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "😀 tail", 0);
  key(&edit, KEY_END);
  key(&edit, 23);
  expect(&edit, "😀 ", 5);
  key(&edit, 23);
  expect(&edit, "", 0);
}

TEST_CASE("keyboard_edit/invalid_storage_is_not_dereferenced_or_modified", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "x", 1);
  edit.opts.max_len = 0;
  ASCIICHAT_REQUIRE_EQ(key(&edit, 'y'), LINE_EDIT_NO_INPUT);
  edit.opts.max_len = 1;
  ASCIICHAT_REQUIRE_EQ(key(&edit, 'y'), LINE_EDIT_NO_INPUT);
  edit.opts.max_len = sizeof(edit.text);
  edit.cursor = 2;
  ASCIICHAT_REQUIRE_EQ(key(&edit, 'y'), LINE_EDIT_NO_INPUT);
  ASCIICHAT_REQUIRE_STR_EQ(edit.text, "x");
}

TEST_CASE("keyboard_edit/unicode_codepoints_do_not_execute_old_special_keys", "[keyboard_edit]") {
  editor_t edit;
  start(&edit, "ab", 1);
  key(&edit, (keyboard_key_t)0x100);
  expect(&edit, "aĀb", 3);
  key(&edit, KEY_LEFT);
  key(&edit, (keyboard_key_t)0x104);
  expect(&edit, "aĄĀb", 3);
}
