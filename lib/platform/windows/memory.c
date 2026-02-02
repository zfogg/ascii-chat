/**
 * @file platform/windows/memory.c
 * @brief Windows memory utilities implementation
 */

#include <malloc.h>
#include <ascii-chat/common.h>

size_t platform_malloc_size(void *ptr) {
  if (!ptr) {
    return 0;
  }
  /* Try _msize for regular malloc allocations */
  return _msize(ptr);
}
