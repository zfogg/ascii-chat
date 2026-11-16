/**
 * @file platform/wasm/init.c
 * @brief Platform initialization for WASM/Emscripten
 * @ingroup platform
 */

#include <ascii-chat/asciichat_errno.h>

int platform_init(void) {
  // No special initialization needed for WASM
  return ASCIICHAT_OK;
}

void platform_destroy(void) {
  // No cleanup needed for WASM
}
