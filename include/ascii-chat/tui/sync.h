#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/terminal.h>

/** Debug-only service. Collection and input never wait on application locks. */
asciichat_error_t ui_sync_start(int fd);
void ui_sync_stop(void);
bool ui_sync_is_visible(void);
keyboard_key_t ui_sync_handle_key(keyboard_key_t key);
/** Called only by the presentation thread; uses already-owned snapshots. */
void ui_sync_render(terminal_size_t size);
int ui_sync_output_fd(void);
