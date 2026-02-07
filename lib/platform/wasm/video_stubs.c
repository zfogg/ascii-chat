/**
 * @file platform/wasm/video_stubs.c
 * @brief Video utility stubs for WASM
 */

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declaration for buffer pool type
typedef struct buffer_pool buffer_pool_t;

// Image validation stub
bool image_validate_dimensions(int width, int height) {
  return (width > 0 && height > 0 && width <= 10000 && height <= 10000);
}

// Buffer pool stub - not needed for WASM mirror mode
void buffer_pool_free(buffer_pool_t *pool, void *data, size_t size) {
  (void)pool;
  (void)data;
  (void)size;
  // No-op - WASM uses standard malloc/free
}
