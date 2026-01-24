#pragma once

/**
 * @file platform/memory.h
 * @brief Cross-platform memory utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent memory utilities for debugging and allocation tracking.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>

/**
 * Get the allocated size of a malloc'd block.
 *
 * Returns the actual allocated size of a pointer, which may be larger than
 * the requested size due to alignment and overhead.
 *
 * This is primarily used for debugging/tracking of untracked allocations.
 *
 * @param ptr Pointer to query (from malloc/calloc/aligned_malloc)
 * @return Allocated size in bytes, or 0 if unable to determine
 */
size_t platform_malloc_size(void *ptr);

/** @} */
