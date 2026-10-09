#pragma once

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/ui/controller.h>

/** Input policy is separate from presentation. Covered controls are consumed and discarded. */
keyboard_key_t ui_input_read_key(ui_screen_t screen);
keyboard_key_t ui_input_wait_key(ui_screen_t screen, unsigned timeout_ms);
void ui_input_shutdown(void);

/** Absolute monotonic deadline; user activity never extends it. */
unsigned ui_input_timeout_seconds(unsigned default_seconds);
uint64_t ui_input_deadline(unsigned default_seconds);
bool ui_input_expired(uint64_t deadline);
void ui_input_timeout_report(unsigned seconds, const char *consequence);

asciichat_error_t ui_input_prompt(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts);
