#pragma once

/**
 * @file session/discovery_status.h
 * @brief 📊 Discovery service status screen display and management
 * @ingroup session
 *
 * Manages periodic display of discovery service status information including:
 * - Bind addresses (IPv4 and IPv6)
 * - Connected server count
 * - Active session count
 * - Service uptime
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
#include "../discovery/database.h"

/**
 * @brief Discovery service status information
 *
 * Contains all data needed to display the discovery service status screen.
 */
typedef struct {
  char ipv4_address[256];   ///< Formatted IPv4 bind address with port
  char ipv6_address[256];   ///< Formatted IPv6 bind address with port
  uint16_t port;            ///< TCP listen port
  size_t connected_servers; ///< Number of connected servers
  size_t active_sessions;   ///< Number of active sessions
  bool ipv4_bound;          ///< Whether IPv4 socket is bound
  bool ipv6_bound;          ///< Whether IPv6 socket is bound
  time_t start_time;        ///< Service start time (for uptime calculation)
} discovery_status_t;

/**
 * @brief Gather current discovery service status
 *
 * Collects current discovery service status information including connected servers,
 * active sessions, and bind addresses.
 *
 * @param server Initialized TCP server structure
 * @param db Initialized discovery database
 * @param ipv4_address IPv4 bind address (can be NULL or empty)
 * @param ipv6_address IPv6 bind address (can be NULL or empty)
 * @param port TCP listen port
 * @param start_time Service start time
 * @param[out] out_status Output status structure
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t discovery_status_gather(tcp_server_t *server, discovery_database_t *db, const char *ipv4_address,
                                          const char *ipv6_address, uint16_t port, time_t start_time,
                                          discovery_status_t *out_status);

/**
 * @brief Display discovery service status screen
 *
 * Outputs formatted discovery service status to stdout. Uses ANSI escape codes for
 * clean display that doesn't interfere with logging output.
 *
 * @param status Discovery service status information
 */
void discovery_status_display(const discovery_status_t *status);

/**
 * @brief Periodically update discovery service status display
 *
 * Gathers and displays discovery service status if enough time has passed since
 * the last update (1-2 second intervals). Designed to be called from
 * the TCP server status update callback.
 *
 * @param server Initialized TCP server structure
 * @param db Initialized discovery database
 * @param ipv4_address IPv4 bind address (can be NULL or empty)
 * @param ipv6_address IPv6 bind address (can be NULL or empty)
 * @param port TCP listen port
 * @param start_time Service start time
 * @param[in,out] last_update Time of last update (updated if display occurs)
 */
void discovery_status_update(tcp_server_t *server, discovery_database_t *db, const char *ipv4_address,
                             const char *ipv6_address, uint16_t port, time_t start_time, time_t *last_update);
