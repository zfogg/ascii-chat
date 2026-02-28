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

/** @brief Maximum client ID length - format: "noun.N (transport:port)" e.g., "mountain.0 (tcp:48036)" */
#define MAX_CLIENT_ID_LEN 64

/** @brief Maximum possible clients (static array size) - actual runtime limit set by --max-clients (1-32) */
#define MAX_CLIENTS 32

/** @brief Default maximum frame rate (frames per second) */
#define DEFAULT_MAX_FPS 60

/** @brief Frame interval in milliseconds based on default FPS (compile-time constant) */
#define FRAME_INTERVAL_MS (1000 / DEFAULT_MAX_FPS)

/** @brief Frame buffer capacity based on default FPS (compile-time constant) */
#define FRAME_BUFFER_CAPACITY (DEFAULT_MAX_FPS / 4)

/** @} */
