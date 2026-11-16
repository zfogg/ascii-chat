/**
 * @file platform/wasm/stubs/video.c
 * @brief Video utility stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>

// Image validation stub - returns ASCIICHAT_OK (0) on success
asciichat_error_t image_validate_dimensions(size_t width, size_t height) {
  if (width > 0 && height > 0 && width <= 10000 && height <= 10000) {
    return ASCIICHAT_OK;
  }
  return ERROR_INVALID_PARAM;
}
