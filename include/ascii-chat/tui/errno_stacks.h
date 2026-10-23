#pragma once

#include <ascii-chat/platform/keyboard.h>
#include <stdbool.h>

/** Toggle the diagnostic errno-stack terminal UI. Available only in diagnostic builds. */
void ui_errno_stacks_toggle(void);
void ui_errno_stacks_hide(void);
bool ui_errno_stacks_visible(void);
/** Returns true when a visible errno-stack UI consumed the key. */
bool ui_errno_stacks_handle_key(keyboard_key_t key);
