/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>

// ============================================================================
// Initialization
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_init(int width, int height) {
  // Initialize platform layer
  asciichat_error_t err = platform_init();
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Create minimal argc/argv for options system
  char *argv[] = {"mirror", NULL};
  int argc = 1;

  // Initialize options (sets up RCU, defaults, etc.)
  err = options_init(argc, argv);
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Override dimensions with actual values from xterm.js
  options_set_int("width", width);
  options_set_int("height", height);

  return 0;
}

EMSCRIPTEN_KEEPALIVE
void mirror_cleanup(void) {
  options_cleanup();
  platform_cleanup();
}

// ============================================================================
// Settings API - Dimension Getters/Setters
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_width(int width) {
  if (width <= 0 || width > 1000) {
    return -1;
  }
  asciichat_error_t err = options_set_int("width", width);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_set_height(int height) {
  if (height <= 0 || height > 1000) {
    return -1;
  }
  asciichat_error_t err = options_set_int("height", height);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_width(void) {
  return GET_OPTION(width);
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_height(void) {
  return GET_OPTION(height);
}
