#pragma once
#include <ascii-chat/platform/keyboard.h>

/** Incremental UTF-8 decoder. Zero-initialize; incomplete input never becomes text. */
typedef struct {
  uint8_t bytes[4];
  unsigned length, expected;
} keyboard_text_decoder_t;

/** Feed a byte or native special key. Invalid sequences are discarded; a new
 * starter or special key interrupts incomplete text and is processed normally. */
keyboard_key_t keyboard_text_decode(keyboard_text_decoder_t *state, int input);

/** Encode a text key to NUL-terminated UTF-8. Returns 0 for non-text keys. */
size_t keyboard_text_encode(keyboard_key_t key, char text[5]);

/** Decode native UTF-16 console text. An unmatched surrogate is discarded;
 * a following ordinary character is preserved. Zero-initialize pending_high. */
keyboard_key_t keyboard_text_utf16(uint16_t *pending_high, uint16_t unit);
