#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/platform/keyboard.h>
#include <string.h>

static const keyboard_key_t *keys;
static size_t remaining;
static keyboard_key_t next_key(void) {
  if (!remaining)
    return KEY_NONE;
  --remaining;
  return *keys++;
}
static bool path(const char *text) {
  return text[0] == '/';
}
static keyboard_key_t read_keys(tui_input_events_t *s, const keyboard_key_t *input, size_t count) {
  keys = input;
  remaining = count;
  return tui_input_events_read(s, next_key, path);
}

TEST_CASE("input_events/bracketed_paste_preserves_unicode_and_shortcuts_as_text", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t start[] = {KEY_PASTE_BEGIN, '/', 0xc3};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, start, 3), KEY_NONE);
  ASCIICHAT_REQUIRE(state.pasting);
  const keyboard_key_t end[] = {0xa9, (keyboard_key_t)(0x1f600), '?', 'o', '=', '\r', '\n', '\t', KEY_PASTE_END, 'q'};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, end, sizeof(end) / sizeof(*end)), KEY_PASTE);
  ASCIICHAT_REQUIRE(state.ready);
  ASCIICHAT_REQUIRE_NOT(state.invalid);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "/é😀?o=\r\n\t");
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_NONE);
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), 'q');
}

TEST_CASE("input_events/rejected_paste_is_drained_before_next_shortcut", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {KEY_PASTE_END, KEY_PASTE_BEGIN, 'a', KEY_UP, KEY_ESCAPE, 127, KEY_PASTE_END, 'q'};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, input, sizeof(input) / sizeof(*input)), KEY_PASTE);
  ASCIICHAT_REQUIRE(state.invalid);
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), 'q');
  const keyboard_key_t fresh[] = {KEY_PASTE_BEGIN, 'b', KEY_PASTE_END};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, fresh, 3), KEY_PASTE);
  ASCIICHAT_REQUIRE_NOT(state.invalid);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "b");
}

TEST_CASE("input_events/text_bursts_round_trip_without_casting_unicode_to_bytes", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'a', (keyboard_key_t)(0x754c), KEY_LEFT, 'b'};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, input, 4), 'a');
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), 0x754c);
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), 'b');
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_NONE);
}

TEST_CASE("input_events/application_classifies_unicode_path_bursts", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'/', (keyboard_key_t)(0x754c), '.', 'm', 'p', '4', '\r', '\n'};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, input, 8), KEY_PASTE);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "/界.mp4");
  ASCIICHAT_REQUIRE_NOT(state.invalid);
  state = (tui_input_events_t){0};
  keys = input;
  remaining = 8;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, NULL), '/');
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, NULL), 0x754c);
}

TEST_CASE("input_events/a_boundary_in_a_burst_does_not_reorder_events", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'x', KEY_PASTE_BEGIN, '?', KEY_PASTE_END, KEY_LEFT};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, input, 5), 'x');
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_PASTE);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "?");
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_LEFT);
}

TEST_CASE("input_events/full_payload_is_drained_and_never_overflows", "[input_events]") {
  tui_input_events_t state = {0};
  keyboard_key_t input[9003];
  input[0] = KEY_PASTE_BEGIN;
  for (size_t i = 1; i < 9001; ++i)
    input[i] = 'x';
  input[9001] = KEY_PASTE_END;
  input[9002] = 'q';
  keys = input;
  remaining = 9003;
  keyboard_key_t result;
  do {
    result = tui_input_events_read(&state, next_key, path);
  } while (result == KEY_NONE && remaining);
  ASCIICHAT_REQUIRE_EQ(result, KEY_PASTE);
  ASCIICHAT_REQUIRE(state.invalid);
  ASCIICHAT_REQUIRE_EQ(state.length, sizeof(state.paste) - 1);
  ASCIICHAT_REQUIRE_EQ(state.paste[state.length], 0);
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), 'q');
}

TEST_CASE("input_events/classified_path_keeps_the_following_bracketed_paste_framed", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'/', 'x', KEY_PASTE_BEGIN, 'q', '?', 'o', '=', KEY_PASTE_END, KEY_LEFT};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, input, sizeof(input) / sizeof(*input)), KEY_PASTE);
  ASCIICHAT_REQUIRE_NOT(state.invalid);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "/x");
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_PASTE);
  ASCIICHAT_REQUIRE_NOT(state.invalid);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "q?o=");
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), KEY_LEFT);
}

TEST_CASE("input_events/classified_path_preserves_a_stray_end_boundary_without_losing_the_next_key", "[input_events]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'/', 'x', KEY_PASTE_END, 'q'};
  ASCIICHAT_REQUIRE_EQ(read_keys(&state, input, 4), KEY_PASTE);
  ASCIICHAT_REQUIRE_NOT(state.invalid);
  ASCIICHAT_REQUIRE_STR_EQ(state.paste, "/x");
  state.ready = false;
  ASCIICHAT_REQUIRE_EQ(tui_input_events_read(&state, next_key, path), 'q');
}
