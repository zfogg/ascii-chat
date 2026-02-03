#pragma once

/**
 * @file session/server_status.h
 * @brief 📊 Server status screen display and management
 * @ingroup session
 *
 * Manages periodic display of server status information including:
 * - Session string (memorable 3-word string)
 * - Bind addresses (IPv4 and IPv6)
 * - Connected client/server count
 * - Server uptime
 *
 * The status screen is updated periodically from the TCP server accept loop
 * via callback and can be disabled with --no-status-screen flag.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <time.h>
#include "../common.h"
#include "../network/tcp/server.h"

/**
 * @brief Server status information
 *
 * Contains all data needed to display the server status screen.
 */
typedef struct {
  char session_string[64];   ///< Memorable session string (e.g., "happy-sunset-ocean")
  char ipv4_address[256];    ///< Formatted IPv4 bind address with port
  char ipv6_address[256];    ///< Formatted IPv6 bind address with port
  uint16_t port;             ///< TCP listen port
  size_t connected_count;    ///< Number of connected clients/servers
  bool ipv4_bound;           ///< Whether IPv4 socket is bound
  bool ipv6_bound;           ///< Whether IPv6 socket is bound
  bool session_is_mdns_only; ///< Whether session is mDNS-only or ACDS
  time_t start_time;         ///< Server start time (for uptime calculation)
  const char *mode_name;     ///< Mode name (e.g., "Server", "Discovery Service")
} server_status_t;

/**
 * @brief Gather current server status
 *
 * Collects current server status information including connected count,
 * bind addresses, and session string.
 *
 * @param server Initialized TCP server structure
 * @param session_string Memorable session string
 * @param ipv4_address IPv4 bind address (can be NULL or empty)
 * @param ipv6_address IPv6 bind address (can be NULL or empty)
 * @param port TCP listen port
 * @param start_time Server start time
 * @param mode_name Mode name for display (e.g., "Server")
 * @param session_is_mdns_only Whether session is mDNS-only or ACDS
 * @param[out] out_status Output status structure
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t server_status_gather(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                                       const char *ipv6_address, uint16_t port, time_t start_time,
                                       const char *mode_name, bool session_is_mdns_only, server_status_t *out_status);

/**
 * @brief Display server status screen
 *
 * Outputs formatted server status to stdout. Uses ANSI escape codes for
 * clean display that doesn't interfere with logging output.
 *
 * @param status Server status information
 */
void server_status_display(const server_status_t *status);

/**
 * @brief Periodically update server status display
 *
 * Gathers and displays server status if enough time has passed since
 * the last update (1-2 second intervals). Designed to be called from
 * the TCP server status update callback.
 *
 * @param server Initialized TCP server structure
 * @param session_string Memorable session string
 * @param ipv4_address IPv4 bind address (can be NULL or empty)
 * @param ipv6_address IPv6 bind address (can be NULL or empty)
 * @param port TCP listen port
 * @param start_time Server start time
 * @param mode_name Mode name for display (e.g., "Server")
 * @param session_is_mdns_only Whether session is mDNS-only or ACDS
 * @param[in,out] last_update Time of last update (updated if display occurs)
 */
void server_status_update(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                          const char *ipv6_address, uint16_t port, time_t start_time, const char *mode_name,
                          bool session_is_mdns_only, time_t *last_update);
