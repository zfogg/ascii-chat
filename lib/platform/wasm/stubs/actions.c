/**
 * @file platform/wasm/stubs/actions.c
 * @brief Action function stubs for WASM (not needed for mirror mode)
 * @ingroup platform
 */

#include <ascii-chat/options/actions.h>
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>

// Action functions not supported in WASM mirror mode
void action_completions(const char *shell_name, const char *output_path) {
  (void)shell_name;
  (void)output_path;
  // No-op - shell completions not supported in WASM
}

void action_list_webcams(void) {
  // No-op - webcam listing not supported in WASM mirror mode
}

void action_list_microphones(void) {
  // No-op - microphone listing not supported in WASM mirror mode
}

void action_list_speakers(void) {
  // No-op - speaker listing not supported in WASM mirror mode
}

void action_create_config(const char *path) {
  (void)path;
  // No-op - config file creation not supported in WASM
}

void action_create_manpage(const char *path) {
  (void)path;
  // No-op - man page creation not supported in WASM
}

void actions_execute_deferred(void) {
  // No-op - no deferred actions in WASM mirror mode
}

void action_show_capabilities(void) {
  // No-op - capabilities not shown in WASM
}
