/**
 * @file platform/wasm/init.c
 * @brief Platform initialization for WASM/Emscripten
 * @ingroup platform
 */

#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>

asciichat_error_t platform_init(void) {
  // No special initialization needed for WASM
  return ASCIICHAT_OK;
}

void platform_destroy(void) {
  // No cleanup needed for WASM
}
