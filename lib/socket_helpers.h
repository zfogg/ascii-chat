/**
 * @file socket_helpers.h
 * @ingroup socket_helpers
 * @brief 🔌 Socket configuration and setup helper functions
 *
 * Provides reusable utilities for socket configuration with standardized
 * buffer sizes and TCP options.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include "platform/abstraction.h"

/**
 * @defgroup socket_helpers Socket Configuration Helpers
 * @ingroup module_core
 * @brief Utilities for socket setup and configuration
 * @{
 */

/**
 * @brief Configure socket buffers and TCP options
 *
 * Sets standard socket options for optimal performance:
 * - SO_SNDBUF: 256KB send buffer
 * - SO_RCVBUF: 256KB receive buffer
 * - TCP_NODELAY: Disable Nagle's algorithm for low-latency communication
 *
 * Logs errors but does not fail - partially configured sockets still work.
 *
 * @param[in] sockfd Socket file descriptor to configure
 *
 * @return 0 on success, negative value if any option fails
 *
 * @par Example:
 * @code{.c}
 * socket_t client_sock = socket(AF_INET, SOCK_STREAM, 0);
 * if (client_sock == INVALID_SOCKET_VALUE) {
 *   return ERROR_NETWORK;
 * }
 *
 * if (socket_configure_buffers(client_sock) != 0) {
 *   log_warn("Some socket options could not be set");
 * }
 * @endcode
 *
 * @note Individual option failures are logged but don't prevent other options
 *       from being set or the socket from being used
 * @see SO_SNDBUF, SO_RCVBUF For buffer size constants
 * @see TCP_NODELAY For low-latency TCP option
 */
int socket_configure_buffers(socket_t sockfd);

/** @} */
