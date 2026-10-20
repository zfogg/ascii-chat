#include <criterion/criterion.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/system.h>
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

Test(terminal_response, escape_before_reply_does_not_open_logs_or_search) {
  keyboard_stream_reset();
  input = "\033\033]11;rgb:1717/1717/1717\033\\o";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_ESCAPE);
  cr_assert_eq(keyboard_stream_read(next_byte), 'o');
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
}

Test(terminal_response, escape_inside_reply_is_not_reply_text) {
  keyboard_stream_reset();
  input = "\033]11;rgb:1717/\0331717/1717\a>";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_ESCAPE);
  cr_assert_eq(keyboard_stream_read(next_byte), '>');
}

Test(terminal_response, slow_reply_does_not_expire_into_keyboard_input) {
  keyboard_stream_reset();
  input = "\033]11;rgb:1717/";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
  platform_sleep_ms(75);
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
  input = "1717/1717\033\\-";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
  cr_assert_eq(keyboard_stream_read(next_byte), '-');
}

Test(terminal_response, oversized_reply_is_discarded_through_terminator) {
  keyboard_stream_reset();
  char reply[400];
  memset(reply, '/', sizeof(reply));
  memcpy(reply, "\033]11;rgb:", 9);
  memcpy(reply + sizeof(reply) - 4, "\ao\0", 3);
  input = reply;
  keyboard_key_t key;
  for (unsigned attempts = 0; attempts < 16; ++attempts) {
    key = keyboard_stream_read(next_byte);
    cr_assert(key == KEY_NONE || key == 'o');
    if (key == 'o')
      break;
  }
  cr_assert_eq(key, 'o');
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_NONE);
}

Test(terminal_response, escape_before_arrow_preserves_both_keys) {
  keyboard_stream_reset();
  input = "\033\033[Ao";
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_ESCAPE);
  cr_assert_eq(keyboard_stream_read(next_byte), KEY_UP);
  cr_assert_eq(keyboard_stream_read(next_byte), 'o');
}

Test(terminal_response, terminator_at_buffer_boundary_preserves_next_key) {
  keyboard_stream_reset();
  char reply[130];
  memset(reply, '/', sizeof(reply));
  memcpy(reply, "\033]11;rgb:", 9);
  memcpy(reply + 127, "\ao", 3);
  input = reply;
  keyboard_key_t key = KEY_NONE;
  for (unsigned attempt = 0; attempt < 8 && key != 'o'; ++attempt) {
    key = keyboard_stream_read(next_byte);
    cr_assert(key == KEY_NONE || key == 'o');
  }
  cr_assert_eq(key, 'o');
}
