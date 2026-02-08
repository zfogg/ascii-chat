/**
 * @file platform/wasm/stubs/mode_defaults.c
 * @brief Mode-specific default stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/options/registry/mode_defaults.h>
#include <ascii-chat/options/options.h>

// WASM mirror mode doesn't need mode-specific defaults
const void *get_default_log_file(asciichat_mode_t mode) {
  (void)mode;
  static const char *default_log_file = NULL;
  return &default_log_file;
}

const void *get_default_port(asciichat_mode_t mode) {
  (void)mode;
  static const int default_port = 0;
  return &default_port;
}

const void *get_default_websocket_port(asciichat_mode_t mode) {
  (void)mode;
  static const int default_websocket_port = 0;
  return &default_websocket_port;
}

void apply_mode_specific_defaults(options_t *opts) {
  (void)opts;
  // No mode-specific defaults needed for WASM mirror mode
}
