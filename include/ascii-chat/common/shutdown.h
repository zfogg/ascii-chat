/**
 * @defgroup shutdown Shutdown System
 * @ingroup module_core
 * @brief Clean shutdown detection without library accessing application state
 *
 * @file shutdown.h
 * @brief Shutdown check system for clean library/application separation
 * @ingroup shutdown
 * @addtogroup shutdown
 * @{
 *
 * Provides clean separation between library and application for shutdown
 * detection. Library code should never directly access application state.
 *
 * Usage:
 *   Application (server.c/client.c):
 *     shutdown_register_callback(my_shutdown_check_fn);
 *
 *   Library code (logging.c, debug/lock.c, etc.):
 *     if (shutdown_is_requested()) { return; }
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Shutdown check callback function type
 * @return true if shutdown has been requested, false otherwise
 * @ingroup shutdown
 */
typedef bool (*shutdown_check_fn)(void);

/**
 * @brief Register application's shutdown check function
 * @param callback Function to call to check if shutdown has been requested
 *
 * @note Call this from main() to register the application's shutdown detection function.
 *       Library code should use shutdown_is_requested() instead of accessing application state directly.
 *
 * @ingroup shutdown
 */
void shutdown_register_callback(shutdown_check_fn callback);

/**
 * @brief Check if shutdown has been requested
 * @return true if shutdown has been requested, false otherwise
 *
 * @note Use this in library code to check for shutdown requests without accessing
 *       application state directly. The callback must be registered first with
 *       shutdown_register_callback().
 *
 * @ingroup shutdown
 */
bool shutdown_is_requested(void);

/** @} */
