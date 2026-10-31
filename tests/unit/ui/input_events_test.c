#include <criterion/criterion.h>
#include <ascii-chat/tui/input_events.h>
#include <ascii-chat/platform/keyboard_text.h>
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

Test(input_events, bracketed_paste_preserves_unicode_and_shortcuts_as_text) {
  tui_input_events_t state = {0};
  const keyboard_key_t start[] = {KEY_PASTE_BEGIN, '/', 0xc3};
  cr_assert_eq(read_keys(&state, start, 3), KEY_NONE);
  cr_assert(state.pasting);
  const keyboard_key_t end[] = {
      0xa9, (keyboard_key_t)(KEY_UNICODE_BASE + 0x1f600), '?', 'o', '=', '\r', '\n', '\t', KEY_PASTE_END, 'q'};
  cr_assert_eq(read_keys(&state, end, sizeof(end) / sizeof(*end)), KEY_PASTE);
  cr_assert(state.ready);
  cr_assert_not(state.invalid);
  cr_assert_str_eq(state.paste, "/é😀?o=\r\n\t");
  cr_assert_eq(tui_input_events_read(&state, next_key, path), KEY_NONE);
  state.ready = false;
  cr_assert_eq(tui_input_events_read(&state, next_key, path), 'q');
}

Test(input_events, rejected_paste_is_drained_before_next_shortcut) {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {KEY_PASTE_END, KEY_PASTE_BEGIN, 'a', KEY_UP, KEY_ESCAPE, 127, KEY_PASTE_END, 'q'};
  cr_assert_eq(read_keys(&state, input, sizeof(input) / sizeof(*input)), KEY_PASTE);
  cr_assert(state.invalid);
  state.ready = false;
  cr_assert_eq(tui_input_events_read(&state, next_key, path), 'q');
  const keyboard_key_t fresh[] = {KEY_PASTE_BEGIN, 'b', KEY_PASTE_END};
  cr_assert_eq(read_keys(&state, fresh, 3), KEY_PASTE);
  cr_assert_not(state.invalid);
  cr_assert_str_eq(state.paste, "b");
}

Test(input_events, text_bursts_round_trip_without_casting_unicode_to_bytes) {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'a', (keyboard_key_t)(KEY_UNICODE_BASE + 0x754c), KEY_LEFT, 'b'};
  cr_assert_eq(read_keys(&state, input, 4), 'a');
  cr_assert_eq(tui_input_events_read(&state, next_key, path), KEY_UNICODE_BASE + 0x754c);
  cr_assert_eq(tui_input_events_read(&state, next_key, path), KEY_LEFT);
  cr_assert_eq(tui_input_events_read(&state, next_key, path), 'b');
  cr_assert_eq(tui_input_events_read(&state, next_key, path), KEY_NONE);
}

Test(input_events, application_classifies_unicode_path_bursts) {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'/', (keyboard_key_t)(KEY_UNICODE_BASE + 0x754c), '.', 'm', 'p', '4', '\r', '\n'};
  cr_assert_eq(read_keys(&state, input, 8), KEY_PASTE);
  cr_assert_str_eq(state.paste, "/界.mp4");
  cr_assert_not(state.invalid);
  state = (tui_input_events_t){0};
  keys = input;
  remaining = 8;
  cr_assert_eq(tui_input_events_read(&state, next_key, NULL), '/');
  cr_assert_eq(tui_input_events_read(&state, next_key, NULL), KEY_UNICODE_BASE + 0x754c);
}

Test(input_events, a_boundary_in_a_burst_does_not_reorder_events) {
  tui_input_events_t state = {0};
  const keyboard_key_t input[] = {'x', KEY_PASTE_BEGIN, '?', KEY_PASTE_END, KEY_LEFT};
  cr_assert_eq(read_keys(&state, input, 5), 'x');
  cr_assert_eq(tui_input_events_read(&state, next_key, path), KEY_PASTE);
  cr_assert_str_eq(state.paste, "?");
  state.ready = false;
  cr_assert_eq(tui_input_events_read(&state, next_key, path), KEY_LEFT);
}

Test(input_events, full_payload_is_drained_and_never_overflows) {
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
  cr_assert_eq(result, KEY_PASTE);
  cr_assert(state.invalid);
  cr_assert_eq(state.length, sizeof(state.paste) - 1);
  cr_assert_eq(state.paste[state.length], 0);
  state.ready = false;
  cr_assert_eq(tui_input_events_read(&state, next_key, path), 'q');
}
