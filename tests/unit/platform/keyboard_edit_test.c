#include <criterion/criterion.h>
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

static keyboard_line_edit_result_t key(editor_t *edit, keyboard_key_t input) {
  edit->opts.key = input;
  return keyboard_apply_key(&edit->opts);
}

static void expect(editor_t *edit, const char *text, size_t cursor) {
  cr_assert_str_eq(edit->text, text);
  cr_assert_eq(edit->length, strlen(text));
  cr_assert_eq(edit->cursor, cursor);
}

Test(keyboard_edit, insertion_navigation_and_forward_delete) {
  editor_t edit;
  start(&edit, "ac", 1);
  cr_assert_eq(key(&edit, 'b'), LINE_EDIT_CONTINUE);
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

Test(keyboard_edit, backspace_removes_before_cursor_without_losing_suffix) {
  const keyboard_key_t backspaces[] = {8, 127};
  for (size_t i = 0; i < 2; ++i) {
    editor_t edit;
    start(&edit, "abc", 2);
    cr_assert_eq(key(&edit, backspaces[i]), LINE_EDIT_CONTINUE);
    expect(&edit, "ac", 1);
    key(&edit, backspaces[i]);
    expect(&edit, "c", 0);
    cr_assert_eq(key(&edit, backspaces[i]), LINE_EDIT_CONTINUE);
    expect(&edit, "c", 0);
    key(&edit, KEY_DELETE);
    expect(&edit, "", 0);
    cr_assert_eq(key(&edit, backspaces[i]), LINE_EDIT_CANCELLED);
  }
}

Test(keyboard_edit, word_deletion_keeps_suffix_and_updates_cursor) {
  editor_t edit;
  start(&edit, "one  two three", 9);
  key(&edit, 23);
  expect(&edit, "one  three", 5);
  key(&edit, 23);
  expect(&edit, "three", 0);
  cr_assert_eq(key(&edit, 23), LINE_EDIT_CONTINUE);
  expect(&edit, "three", 0);
  key(&edit, KEY_CTRL_DELETE);
  expect(&edit, "", 0);
  cr_assert_eq(key(&edit, 23), LINE_EDIT_CANCELLED);
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

Test(keyboard_edit, completion_cancellation_and_ignored_keys_do_not_mutate_text) {
  editor_t edit;
  start(&edit, "keep", 2);
  cr_assert_eq(key(&edit, '\r'), LINE_EDIT_ACCEPTED);
  cr_assert_eq(key(&edit, '\n'), LINE_EDIT_ACCEPTED);
  cr_assert_eq(key(&edit, KEY_ESCAPE), LINE_EDIT_CANCELLED);
  cr_assert_eq(key(&edit, 3), LINE_EDIT_CANCELLED);
  cr_assert_eq(key(&edit, KEY_NONE), LINE_EDIT_NO_INPUT);
  cr_assert_eq(key(&edit, 1), LINE_EDIT_CONTINUE);
  cr_assert_eq(key(&edit, KEY_UP), LINE_EDIT_CONTINUE);
  cr_assert_eq(key(&edit, 0xe9), LINE_EDIT_CONTINUE);
  expect(&edit, "keep", 2);
}

Test(keyboard_edit, missing_state_is_rejected) {
  editor_t edit;
  start(&edit, "", 0);
  edit.opts.key = 'x';
  cr_assert_eq(keyboard_apply_key(NULL), LINE_EDIT_NO_INPUT);
  keyboard_line_edit_opts_t invalid = edit.opts;
  invalid.buffer = NULL;
  cr_assert_eq(keyboard_apply_key(&invalid), LINE_EDIT_NO_INPUT);
  invalid = edit.opts;
  invalid.len = NULL;
  cr_assert_eq(keyboard_apply_key(&invalid), LINE_EDIT_NO_INPUT);
  invalid = edit.opts;
  invalid.cursor = NULL;
  cr_assert_eq(keyboard_apply_key(&invalid), LINE_EDIT_NO_INPUT);
  expect(&edit, "", 0);
}
