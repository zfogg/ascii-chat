#pragma once
#include <ascii-chat/platform/keyboard.h>
/** Decode a nonblocking UTF-8 byte stream and terminal escape sequences. */
keyboard_key_t keyboard_stream_read(int (*read_byte)(void));
void keyboard_stream_reset(void);

/** Last complete OSC 11 reply received from the terminal frontend. */
uint64_t keyboard_stream_terminal_reply_ns(void);
