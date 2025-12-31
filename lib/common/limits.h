/**
 * @defgroup limits Application Limits
 * @ingroup module_core
 * @brief Limits for clients, frame rates, display names, and other constraints
 *
 * @file limits.h
 * @brief Application limits and constraints
 * @ingroup limits
 * @addtogroup limits
 * @{
 */

#pragma once

/* ============================================================================
 * Multi-Client Constants
 * ============================================================================ */

/** @brief Maximum display name length in characters */
#define MAX_DISPLAY_NAME_LEN 32

/** @brief Maximum possible clients (static array size) - actual runtime limit set by --max-clients (1-32) */
#define MAX_CLIENTS 32

/** @brief Default maximum frame rate (frames per second) */
#define DEFAULT_MAX_FPS 60

/** @brief Runtime configurable maximum frame rate (can be overridden via environment or command line) */
extern int g_max_fps;

/** @brief Maximum frame rate macro (uses g_max_fps if set, otherwise DEFAULT_MAX_FPS) */
#define MAX_FPS (g_max_fps > 0 ? g_max_fps : DEFAULT_MAX_FPS)

/** @brief Frame interval in milliseconds based on MAX_FPS */
#define FRAME_INTERVAL_MS (1000 / MAX_FPS)

/** @brief Frame buffer capacity based on MAX_FPS */
#define FRAME_BUFFER_CAPACITY (MAX_FPS / 4)

/** @} */
