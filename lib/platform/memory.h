#pragma once

/**
 * @file platform/memory.h
 * @brief Cross-platform memory management utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent memory functions including querying allocated
 * block sizes for memory debugging and leak tracking.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the size of an allocated memory block
 *
 * Returns the size in bytes of the memory block pointed to by ptr.
 * The block must have been allocated with malloc(), calloc(), or realloc().
 *
 * Platform-specific implementations:
 *   - Windows: _msize()
 *   - macOS: malloc_size()
 *   - Linux: malloc_usable_size()
 *
 * @param ptr Pointer to allocated memory block
 * @return Size of the block in bytes, or 0 if ptr is NULL
 *
 * @note This function requires the block to be allocated with the standard
 *       memory allocation functions. Behavior is undefined for invalid pointers.
 * @note The returned size may be larger than the requested allocation due to
 *       allocator padding and alignment requirements.
 *
 * @ingroup platform
 */
size_t platform_malloc_size(const void *ptr);

#ifdef __cplusplus
}
#endif

/** @} */
