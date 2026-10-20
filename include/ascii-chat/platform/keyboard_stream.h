#pragma once
#include <ascii-chat/platform/keyboard.h>
/** Decode a nonblocking UTF-8 byte stream and terminal escape sequences. */
keyboard_key_t keyboard_stream_read(int (*read_byte)(void));
void keyboard_stream_reset(void);

/** Last complete OSC 11 reply received from the terminal frontend. */
uint64_t keyboard_stream_terminal_reply_ns(void);

/** Query through the normal input decoder, preserving unrelated keyboard input. */
bool keyboard_stream_query_background(int (*read_byte)(void), bool (*send_query)(void), uint8_t *r, uint8_t *g,
                                      uint8_t *b);

/** Most recently decoded OSC 11 reply, without reading or waiting for input. */
bool keyboard_stream_background(uint8_t *r, uint8_t *g, uint8_t *b);
