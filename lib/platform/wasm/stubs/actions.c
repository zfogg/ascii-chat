/**
 * @file platform/wasm/stubs/actions.c
 * @brief Action function stubs for WASM (not needed for mirror mode)
 * @ingroup platform
 */

#include <ascii-chat/options/actions.h>
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>
#include <string.h>

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

void action_show_capabilities_immediate(void) {
  // No-op - capabilities not shown in WASM
}

void action_check_update(void) {
  // No-op - update checking not supported in WASM
}

void action_check_update_immediate(void) {
  // No-op - update checking not supported in WASM
}

// Additional platform stubs
asciichat_error_t platform_enable_keepawake(void) {
  return ASCIICHAT_OK; // No-op in browser (browser manages power)
}

void platform_disable_keepawake(void) {
  // No-op in browser
}

// Keyboard input stubs
#include <ascii-chat/platform/keyboard.h>

keyboard_line_edit_result_t keyboard_read_line_interactive(keyboard_line_edit_opts_t *opts) {
  (void)opts;
  return LINE_EDIT_NO_INPUT; // No input available in WASM
}

keyboard_key_t keyboard_read_nonblocking(void) {
  return KEY_NONE; // No input available in WASM
}

void keyboard_destroy(void) {
  // No-op - no keyboard to destroy in WASM
}

option_action_fn options_action_callback(const char *name, bool immediate) {
  static const struct {
    const char *name;
    option_action_fn callback;
    option_action_fn early_callback;
  } actions[] = {
      {"list-webcams", action_list_webcams, action_list_webcams},
      {"list-microphones", action_list_microphones, action_list_microphones},
      {"list-speakers", action_list_speakers, action_list_speakers},
      {"show-capabilities", action_show_capabilities, action_show_capabilities_immediate},
      {"check-update", action_check_update, action_check_update_immediate},
  };
  if (!name)
    return NULL;
  for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
    if (strcmp(name, actions[i].name) == 0)
      return immediate ? actions[i].early_callback : actions[i].callback;
  }
  return NULL;
}
