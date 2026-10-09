#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>

/** Only display-safe text crosses this boundary; never pass an unmasked password. */
asciichat_error_t ui_prompt_present(const char *prompt, const char *visible, size_t cursor);
void ui_prompt_remove(void);
