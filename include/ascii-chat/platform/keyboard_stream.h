#pragma once
#include <ascii-chat/platform/keyboard.h>
/** Decode a nonblocking UTF-8 byte stream and terminal escape sequences. */
keyboard_key_t keyboard_stream_read(int (*read_byte)(void));
void keyboard_stream_reset(void);
