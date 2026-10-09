#pragma once

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/ui/controller.h>

/** Input policy is separate from presentation. Covered controls are consumed and discarded. */
keyboard_key_t ui_input_read_key(ui_screen_t screen);
keyboard_key_t ui_input_wait_key(ui_screen_t screen, unsigned timeout_ms);
void ui_input_shutdown(void);

asciichat_error_t ui_input_prompt(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts);
