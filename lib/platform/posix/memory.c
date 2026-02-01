/**
 * @file platform/posix/memory.c
 * @brief POSIX memory utilities implementation (macOS, Linux, BSD)
 */

#include <stdlib.h>
#include "../../common.h"

#ifdef __APPLE__
#include <malloc/malloc.h>

size_t platform_malloc_size(void *ptr) {
  if (!ptr) {
    return 0;
  }
  return malloc_size(ptr);
}

#else
/* Linux and other POSIX systems */
#include <malloc.h>

size_t platform_malloc_size(void *ptr) {
  if (!ptr) {
    return 0;
  }
  return malloc_usable_size(ptr);
}

#endif
