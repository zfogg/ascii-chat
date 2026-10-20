#include <criterion/criterion.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static const char *input;
static int next_byte(void) {
  return *input ? (unsigned char)*input++ : 0;
}

Test(terminal_response, fragmented_reply_is_not_media_or_keyboard_input) {
  keyboard_stream_reset();
  const char *reply = "\033]11;rgb:1234/5678/9abc\033\\";
  uint64_t before = keyboard_stream_terminal_reply_ns();
  for (size_t i = 0; i < strlen(reply); ++i) {
    char fragment[] = {reply[i], 0};
    input = fragment;
    cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
    if (i + 1 < strlen(reply))
      cr_assert_eq(keyboard_stream_terminal_reply_ns(), before);
  }
  cr_assert_gt(keyboard_stream_terminal_reply_ns(), before);
  input = "o";
  cr_assert_eq(keyboard_stream_read(next_byte), 'o');
}

Test(terminal_response, bell_terminated_reply_preserves_the_next_shortcut) {
  keyboard_stream_reset();
  uint64_t before = keyboard_stream_terminal_reply_ns();
  input = "\033]11;rgb:ffff/ffff/ffff\a-";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
  cr_assert_gt(keyboard_stream_terminal_reply_ns(), before);
  cr_assert_eq(keyboard_stream_read(next_byte), '-');
}

Test(terminal_response, unrelated_osc_does_not_acknowledge_a_frame) {
  keyboard_stream_reset();
  uint64_t before = keyboard_stream_terminal_reply_ns();
  input = "\033]0;title with o and r and -\a>";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
  cr_assert_eq(keyboard_stream_terminal_reply_ns(), before);
  cr_assert_eq(keyboard_stream_read(next_byte), '>');
}
