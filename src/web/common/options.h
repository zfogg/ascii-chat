/**
 * @file options.h
 * @brief WASM option snapshot and patch declarations
 * @ingroup web_common
 *
 * Declares the shared browser options interface.
 * Implementations in options.c are compiled into both mirror.wasm and client.wasm.
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>

#ifdef __cplusplus
extern "C" {
#endif

double browser_options_revision(void);
asciichat_error_t browser_options_patch(const char *json, double expected_revision);
char *browser_options_values(void);
void browser_options_release(char *json);

const char *get_help_text(int mode, const char *option_name);

#ifdef __cplusplus
}
#endif
