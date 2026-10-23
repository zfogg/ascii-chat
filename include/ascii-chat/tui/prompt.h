#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>

/** Only display-safe text crosses this boundary; never pass an unmasked password. */
asciichat_error_t ui_prompt_present(const char *prompt, const char *visible, size_t cursor, bool secret);
void ui_prompt_remove(void);

const char *ui_prompt_title(const void *snapshot);

int ui_prompt_required_rows(const void *snapshot, int cols);

#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/platform/terminal.h>
/** Shared centered media panel with the animated splash logo. NULL text shows loading. */
void ui_media_panel_render(frame_buffer_t *buffer, terminal_size_t size, const char *title, const char *instruction,
                           const char *text, size_t cursor);

/** Present the media-loading screen while capture resolves its next source. */
asciichat_error_t ui_media_loading_present(void);

/** The snapshot contains masked text only. Selection: confirm=0, cancel=1. */
asciichat_error_t ui_password_prompt_present(const char *target, const char *masked, size_t cursor,
                                             size_t selected_button, bool incorrect, bool can_confirm,
                                             bool newline_limit);
