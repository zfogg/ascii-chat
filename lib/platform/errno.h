#pragma once

/**
 * @file platform/errno.h
 * @brief Cross-platform error handling utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent error handling functions for:
 * - Clearing error state (Windows WSA, system errno)
 * - Error string retrieval
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

/**
 * Clear all error state on the current platform.
 *
 * Windows: Clears WSA errors via WSASetLastError(0).
 * POSIX: Clears system errno.
 */
void platform_clear_error_state(void);

/** @} */
