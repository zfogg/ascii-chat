#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/util/time.h>
#include <libplatform/system.h>
#include <string.h>

static const char *input;
static int next_byte(void) {
  return *input ? (unsigned char)*input++ : 0;
}

TEST_CASE("terminal_response/fragmented_reply_is_not_media_or_keyboard_input", "[terminal_response]") {
  keyboard_stream_reset();
  const char *reply = "\033]11;rgb:1234/5678/9abc\033\\";
  uint64_t before = keyboard_stream_terminal_reply_ns();
  for (size_t i = 0; i < strlen(reply); ++i) {
    char fragment[] = {reply[i], 0};
    input = fragment;
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
    if (i + 1 < strlen(reply))
      ASCIICHAT_REQUIRE_EQ(keyboard_stream_terminal_reply_ns(), before);
  }
  ASCIICHAT_REQUIRE_GT(keyboard_stream_terminal_reply_ns(), before);
  input = "o";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
}

TEST_CASE("terminal_response/bell_terminated_reply_preserves_the_next_shortcut", "[terminal_response]") {
  keyboard_stream_reset();
  uint64_t before = keyboard_stream_terminal_reply_ns();
  input = "\033]11;rgb:ffff/ffff/ffff\a-";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  ASCIICHAT_REQUIRE_GT(keyboard_stream_terminal_reply_ns(), before);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), '-');
}

TEST_CASE("terminal_response/unrelated_osc_does_not_acknowledge_a_frame", "[terminal_response]") {
  keyboard_stream_reset();
  uint64_t before = keyboard_stream_terminal_reply_ns();
  input = "\033]0;title with o and r and -\a>";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_terminal_reply_ns(), before);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), '>');
}

TEST_CASE("terminal_response/escape_before_reply_does_not_open_logs_or_search", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033\033]11;rgb:1717/1717/1717\033\\o";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
}

TEST_CASE("terminal_response/escape_inside_reply_is_not_reply_text", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033]11;rgb:1717/\0331717/1717\a>";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), '>');
}

TEST_CASE("terminal_response/slow_reply_does_not_expire_into_keyboard_input", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033]11;rgb:1717/";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  platform_sleep_ms(75);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  input = "1717/1717\033\\-";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), '-');
}

TEST_CASE("terminal_response/delayed_osc_reply_escape_does_not_become_keyboard_input", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  platform_sleep_ms(75);
  input = "]11;rgb:1717/1717/1717\033\\o";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
}

TEST_CASE("terminal_response/oversized_reply_is_discarded_through_terminator", "[terminal_response]") {
  keyboard_stream_reset();
  char reply[400];
  memset(reply, '/', sizeof(reply));
  memcpy(reply, "\033]11;rgb:", 9);
  memcpy(reply + sizeof(reply) - 4, "\ao\0", 3);
  input = reply;
  keyboard_key_t key;
  for (unsigned attempts = 0; attempts < 16; ++attempts) {
    key = keyboard_stream_read(next_byte);
    ASCIICHAT_REQUIRE(key == KEY_NONE || key == 'o');
    if (key == 'o')
      break;
  }
  ASCIICHAT_REQUIRE_EQ(key, 'o');
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
}

TEST_CASE("terminal_response/escape_before_arrow_preserves_both_keys", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033\033[Ao";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_UP);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
}

TEST_CASE("terminal_response/terminator_at_buffer_boundary_preserves_next_key", "[terminal_response]") {
  keyboard_stream_reset();
  char reply[130];
  memset(reply, '/', sizeof(reply));
  memcpy(reply, "\033]11;rgb:", 9);
  memcpy(reply + 127, "\ao", 3);
  input = reply;
  keyboard_key_t key = KEY_NONE;
  for (unsigned attempt = 0; attempt < 8 && key != 'o'; ++attempt) {
    key = keyboard_stream_read(next_byte);
    ASCIICHAT_REQUIRE(key == KEY_NONE || key == 'o');
  }
  ASCIICHAT_REQUIRE_EQ(key, 'o');
}

static const char *query_reply;
static bool send_test_query(void) {
  input = query_reply;
  return true;
}

TEST_CASE("terminal_response/background_queries_refresh_and_preserve_shortcuts", "[terminal_response]") {
  keyboard_stream_reset();
  const char *replies[] = {"o\033]11;rgb:ffff/ffff/ffff\033\\q", "o\033]11;rgb:00/00/00\aq", "o\033]11;rgb:f/f/f\aq",
                           "o\033]11;rgb:0000/0000/0000\aq"};
  for (unsigned i = 0; i < 4; ++i) {
    query_reply = replies[i];
    uint8_t r, g, b;
    ASCIICHAT_REQUIRE(keyboard_stream_query_background(next_byte, send_test_query, &r, &g, &b));
    ASCIICHAT_REQUIRE_EQ(r, i % 2 ? 0 : 255);
    ASCIICHAT_REQUIRE_EQ(g, r);
    ASCIICHAT_REQUIRE_EQ(b, r);
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'q');
  }
}

TEST_CASE("terminal_response/background_query_timeout_does_not_reuse_a_previous_reply", "[terminal_response]") {
  keyboard_stream_reset();
  uint8_t r = 12, g = 34, b = 56;
  query_reply = "\033]11;rgb:ffff/ffff/ffff\a";
  ASCIICHAT_REQUIRE(keyboard_stream_query_background(next_byte, send_test_query, &r, &g, &b));
  query_reply = "o\033]11;rgb:invalid/0000/0000\a";
  r = 12;
  g = 34;
  b = 56;
  ASCIICHAT_REQUIRE_NOT(keyboard_stream_query_background(next_byte, send_test_query, &r, &g, &b));
  ASCIICHAT_REQUIRE_EQ(r, 12);
  ASCIICHAT_REQUIRE_EQ(g, 34);
  ASCIICHAT_REQUIRE_EQ(b, 56);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
}

TEST_CASE("terminal_response/background_snapshot_never_consumes_shortcuts", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033]11;rgb:12/34/56\ao";
  uint8_t r = 0, g = 0, b = 0;
  // Reading the snapshot cannot consume even a complete pending reply.
  keyboard_stream_background(&r, &g, &b);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
  ASCIICHAT_REQUIRE(keyboard_stream_background(&r, &g, &b));
  ASCIICHAT_REQUIRE_EQ(r, 0x12);
  ASCIICHAT_REQUIRE_EQ(g, 0x34);
  ASCIICHAT_REQUIRE_EQ(b, 0x56);
  input = "q";
  for (unsigned i = 0; i < 10; ++i)
    ASCIICHAT_REQUIRE(keyboard_stream_background(&r, &g, &b));
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'q');
}

TEST_CASE("terminal_response/shifted_enter_is_distinct_and_survives_fragmented_input", "[terminal_response]") {
  const char *sequences[] = {"\033[13;2u", "\033[27;2;13~"};
  for (size_t sequence = 0; sequence < 2; ++sequence) {
    keyboard_stream_reset();
    size_t length = strlen(sequences[sequence]);
    for (size_t i = 0; i < length; ++i) {
      char fragment[] = {sequences[sequence][i], 0};
      input = fragment;
      ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), i + 1 == length ? KEY_SHIFT_ENTER : KEY_NONE);
    }
    input = "\r";
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), '\r');
  }
}

TEST_CASE("terminal_response/extended_keyboard_controls_preserve_cancel_and_edit_keys", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033[27u\033[13u\033[9u\033[127u\033[99;5u\033[27;5;99~\033[118;5u\033[27;5;118~";
  const keyboard_key_t keys[] = {KEY_ESCAPE, 13, 9, 127, 3, 3, 22, 22};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), keys[i]);
}

TEST_CASE("terminal_response/utf8_fragments_readiness_and_reset", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\xf0";
  ASCIICHAT_REQUIRE_NOT(keyboard_stream_ready(next_byte));
  input = "\x9f\x98";
  ASCIICHAT_REQUIRE_NOT(keyboard_stream_ready(next_byte));
  input = "\x80";
  ASCIICHAT_REQUIRE(keyboard_stream_ready(next_byte));
  ASCIICHAT_REQUIRE(keyboard_stream_ready(next_byte));
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 0x1f600);
  input = "\xe2";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
  input = "\033[D";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_LEFT);
  input = "\xe2";
  keyboard_stream_read(next_byte);
  keyboard_stream_reset();
  input = "\x82\xacq";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'q');
}

TEST_CASE("terminal_response/unicode_extended_keys_and_shift_enter_do_not_alias_pageup", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\033[233;2u\033[30028u\033[128512u\033[13;2u\033[5~";
  const keyboard_key_t expected[] = {233, 30028, 128512, KEY_SHIFT_ENTER, KEY_PAGEUP};
  ASCIICHAT_REQUIRE_NE(KEY_SHIFT_ENTER, KEY_PAGEUP);
  for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); ++i)
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), expected[i]);
}

TEST_CASE("terminal_response/malformed_utf8_cannot_become_shortcuts", "[terminal_response]") {
  keyboard_stream_reset();
  input = "\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80o";
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), 'o');
  input = "\033[200~\xc3\xa9\033[201~";
  const keyboard_key_t expected[] = {KEY_PASTE_BEGIN, 0xc3, 0xa9, KEY_PASTE_END};
  for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); ++i)
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), expected[i]);
}

TEST_CASE("terminal_response/unicode_characters_in_the_old_special_key_range_remain_text", "[terminal_response]") {
  keyboard_stream_reset();
  input = "ĀāĂăĄąĆćĈĉĊċČčĎď";
  for (unsigned cp = 0x100; cp <= 0x10f; ++cp)
    ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), (keyboard_key_t)cp);
  ASCIICHAT_REQUIRE_GT(KEY_UP, 0x10ffff);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_read(next_byte), KEY_NONE);
}
