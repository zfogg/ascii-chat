#pragma once

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/platform/terminal.h>

/** Error notifications are an overlay, independent of the selected application screen. */
bool ui_error_popup_visible(void);
terminal_size_t ui_error_popup_minimum(void);
void ui_error_popup_render(frame_buffer_t *output, terminal_size_t size);
void ui_error_popup_key(keyboard_key_t key);
void ui_error_popup_reset(void);
/** Preserve pending notifications using cached terminal dimensions before queue retirement.
 * Callers own an interactive terminal and must prevent concurrent rendering.
 * This formatter performs no console queries and does not wait for input. */
void ui_error_popup_preserve(int fd, terminal_size_t size);
/** Write preserved terminal output after shutdown diagnostics, without allocating or using options. */
void ui_error_popup_flush(void);
