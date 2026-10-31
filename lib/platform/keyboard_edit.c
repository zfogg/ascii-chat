#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/ui/input.h>

keyboard_line_edit_result_t keyboard_apply_key(keyboard_line_edit_opts_t *opts) {
  if (!opts || !opts->buffer || !opts->len || !opts->cursor || !opts->max_len || *opts->len >= opts->max_len ||
      *opts->cursor > *opts->len || opts->key == KEY_NONE)
    return LINE_EDIT_NO_INPUT;
  keyboard_key_t key = opts->key;
  if (key == '\r' || key == '\n')
    return LINE_EDIT_ACCEPTED;
  if (key == KEY_ESCAPE || key == 3 || ((key == 8 || key == 127 || key == 23) && !*opts->len))
    return LINE_EDIT_CANCELLED;
  ui_input_edit_key(opts->buffer, opts->max_len, opts->len, opts->cursor, key);
  return LINE_EDIT_CONTINUE;
}
