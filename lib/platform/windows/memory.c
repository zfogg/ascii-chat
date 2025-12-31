/**
 * @file platform/windows/memory.c
 * @ingroup platform
 * @brief Windows memory management utilities
 */

#ifdef _WIN32

#include "memory.h"
#include <malloc.h>

/**
 * @brief Get the size of an allocated memory block (Windows implementation)
 */
size_t platform_malloc_size(const void *ptr) {
  if (ptr == NULL) {
    return 0;
  }
  return _msize((void *)ptr);
}

#endif
