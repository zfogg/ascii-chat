/**
 * @file platform/posix/memory.c
 * @ingroup platform
 * @brief POSIX memory management utilities
 */

#ifndef _WIN32

#include "../memory.h"

// POSIX-specific memory sizing functions
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

/**
 * @brief Get the size of an allocated memory block (POSIX implementation)
 */
size_t platform_malloc_size(const void *ptr) {
  if (ptr == NULL) {
    return 0;
  }

#ifdef __APPLE__
  return malloc_size(ptr);
#else
  return malloc_usable_size((void *)ptr);
#endif
}

#endif
