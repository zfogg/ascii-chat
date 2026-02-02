#pragma once

/**
 * @file platform/memory.h
 * @brief Cross-platform memory allocation utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides unified memory allocation abstractions across Windows and POSIX platforms.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#ifdef _WIN32
#include <malloc.h>
/**
 * @brief Platform-specific stack allocation (Windows)
 *
 * On Windows, use _alloca for stack allocation
 */
#define platform_alloca(size) _alloca(size)
#else
#include <alloca.h>
/**
 * @brief Platform-specific stack allocation (POSIX)
 *
 * On POSIX, use standard alloca
 */
#define platform_alloca(size) alloca(size)
#endif

/** @} */
