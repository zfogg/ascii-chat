#pragma once
#include <ascii-chat/platform/keyboard.h>

/** Caller-serialized collection state; payloads never outlive this object. */
typedef struct {
  bool pasting, invalid, ready;
  char paste[8192];
  size_t length, head, count;
  keyboard_key_t keys[8192];
} tui_input_events_t;

/** Collect paste boundaries and optional application-classified text bursts.
 * Once ready, the caller consumes/clears paste before reading another event. */
keyboard_key_t tui_input_events_read(tui_input_events_t *state, keyboard_key_t (*read_key)(void),
                                     bool (*classify_paste)(const char *));
