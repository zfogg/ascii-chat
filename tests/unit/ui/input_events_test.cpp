#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/tui/input.h>
#include <libplatform/keyboard.h>
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

TEST_CASE("input_events/bracketed_paste_preserves_unicode_and_shortcuts_as_text", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t start[] = {KEY_PASTE_BEGIN, static_cast<keyboard_key_t>('/'), static_cast<keyboard_key_t>(0xc3)};
  REQUIRE((read_keys(&state, start, 3)) == (KEY_NONE));
  REQUIRE(state.pasting);
  const keyboard_key_t end[] = {static_cast<keyboard_key_t>(0xa9), static_cast<keyboard_key_t>(0x1f600), static_cast<keyboard_key_t>('?'),
                                static_cast<keyboard_key_t>('o'),  static_cast<keyboard_key_t>('='),     static_cast<keyboard_key_t>('\r'),
                                static_cast<keyboard_key_t>('\n'), static_cast<keyboard_key_t>('\t'),    KEY_PASTE_END,
                                static_cast<keyboard_key_t>('q')};
  REQUIRE((read_keys(&state, end, sizeof(end) / sizeof(*end))) == (KEY_PASTE));
  REQUIRE(state.ready);
  REQUIRE(!(state.invalid));
  REQUIRE(std::strcmp((state.paste), ("/é😀?o=\r\n\t")) == 0);
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_NONE));
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == ('q'));
}

TEST_CASE("input_events/rejected_paste_is_drained_before_next_shortcut", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {
      KEY_PASTE_END, KEY_PASTE_BEGIN, static_cast<keyboard_key_t>('a'), KEY_UP, KEY_ESCAPE, static_cast<keyboard_key_t>(127), KEY_PASTE_END, static_cast<keyboard_key_t>('q')};
  REQUIRE((read_keys(&state, input, sizeof(input) / sizeof(*input))) == (KEY_PASTE));
  REQUIRE(state.invalid);
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == ('q'));
  const keyboard_key_t fresh[] = {KEY_PASTE_BEGIN, static_cast<keyboard_key_t>('b'), KEY_PASTE_END};
  REQUIRE((read_keys(&state, fresh, 3)) == (KEY_PASTE));
  REQUIRE(!(state.invalid));
  REQUIRE(std::strcmp((state.paste), ("b")) == 0);
}

TEST_CASE("input_events/text_bursts_round_trip_without_casting_unicode_to_bytes", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {static_cast<keyboard_key_t>('a'), static_cast<keyboard_key_t>(0x754c), KEY_LEFT, static_cast<keyboard_key_t>('b')};
  REQUIRE((read_keys(&state, input, 4)) == ('a'));
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (0x754c));
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_LEFT));
  REQUIRE((tui_input_events_read(&state, next_key, path)) == ('b'));
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_NONE));
}

TEST_CASE("input_events/application_classifies_unicode_path_bursts", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {static_cast<keyboard_key_t>('/'), static_cast<keyboard_key_t>(0x754c), static_cast<keyboard_key_t>('.'),  static_cast<keyboard_key_t>('m'),
                                  static_cast<keyboard_key_t>('p'), static_cast<keyboard_key_t>('4'),    static_cast<keyboard_key_t>('\r'), static_cast<keyboard_key_t>('\n')};
  REQUIRE((read_keys(&state, input, 8)) == (KEY_PASTE));
  REQUIRE(std::strcmp((state.paste), ("/界.mp4")) == 0);
  REQUIRE(!(state.invalid));
  state = (tui_input_events_t){0};
  keys = input;
  remaining = 8;
  REQUIRE((tui_input_events_read(&state, next_key, NULL)) == ('/'));
  REQUIRE((tui_input_events_read(&state, next_key, NULL)) == (0x754c));
}

TEST_CASE("input_events/a_boundary_in_a_burst_does_not_reorder_events", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {static_cast<keyboard_key_t>('x'), KEY_PASTE_BEGIN, static_cast<keyboard_key_t>('?'), KEY_PASTE_END, KEY_LEFT};
  REQUIRE((read_keys(&state, input, 5)) == ('x'));
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_PASTE));
  REQUIRE(std::strcmp((state.paste), ("?")) == 0);
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_LEFT));
}

TEST_CASE("input_events/full_payload_is_drained_and_never_overflows", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  keyboard_key_t input[9003];
  input[0] = KEY_PASTE_BEGIN;
  for (size_t i = 1; i < 9001; ++i)
    input[i] = static_cast<keyboard_key_t>('x');
  input[9001] = KEY_PASTE_END;
  input[9002] = static_cast<keyboard_key_t>('q');
  keys = input;
  remaining = 9003;
  keyboard_key_t result;
  do {
    result = tui_input_events_read(&state, next_key, path);
  } while (result == KEY_NONE && remaining);
  REQUIRE((result) == (KEY_PASTE));
  REQUIRE(state.invalid);
  REQUIRE((state.length) == (sizeof(state.paste) - 1));
  REQUIRE((state.paste[state.length]) == (0));
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == ('q'));
}

TEST_CASE("input_events/classified_path_keeps_the_following_bracketed_paste_framed", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {static_cast<keyboard_key_t>('/'),
                                  static_cast<keyboard_key_t>('x'),
                                  KEY_PASTE_BEGIN,
                                  static_cast<keyboard_key_t>('q'),
                                  static_cast<keyboard_key_t>('?'),
                                  static_cast<keyboard_key_t>('o'),
                                  static_cast<keyboard_key_t>('='),
                                  KEY_PASTE_END,
                                  KEY_LEFT};
  REQUIRE((read_keys(&state, input, sizeof(input) / sizeof(*input))) == (KEY_PASTE));
  REQUIRE(!(state.invalid));
  REQUIRE(std::strcmp((state.paste), ("/x")) == 0);
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_PASTE));
  REQUIRE(!(state.invalid));
  REQUIRE(std::strcmp((state.paste), ("q?o=")) == 0);
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == (KEY_LEFT));
}

TEST_CASE("input_events/classified_path_preserves_a_stray_end_boundary_without_losing_the_next_key", "[input_events] [unit]") {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {static_cast<keyboard_key_t>('/'), static_cast<keyboard_key_t>('x'), KEY_PASTE_END, static_cast<keyboard_key_t>('q')};
  REQUIRE((read_keys(&state, input, 4)) == (KEY_PASTE));
  REQUIRE(!(state.invalid));
  REQUIRE(std::strcmp((state.paste), ("/x")) == 0);
  state.ready = false;
  REQUIRE((tui_input_events_read(&state, next_key, path)) == ('q'));
}
