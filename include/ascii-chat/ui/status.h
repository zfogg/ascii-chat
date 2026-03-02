#pragma once

/**
 * @file ui/status.h
 * @brief 📊 Status screen display and management for server/discovery service modes
 * @ingroup session
 *
 * Manages periodic display of status information including:
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
 * @brief Status information for server/discovery service modes
 *
 * Contains all data needed to display the status screen.
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
} ui_status_t;

/**
 * @brief Gather current status information
 *
 * Collects current status information including connected count,
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
asciichat_error_t ui_status_gather(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                                       const char *ipv6_address, uint16_t port, time_t start_time,
                                       const char *mode_name, bool session_is_mdns_only, ui_status_t *out_status);

/**
 * @brief Display status screen
 *
 * Outputs formatted status to stdout. Uses ANSI escape codes for
 * clean display that doesn't interfere with logging output.
 *
 * @param status Status information
 */
void ui_status_display(const ui_status_t *status);

/**
 * @brief Display status screen with interactive keyboard support
 *
 * Renders status screen and handles keyboard input:
 * - Escape cancels grep if active, otherwise exits status screen
 * - Other keys handled by interactive grep if active
 *
 * @param status Status information
 * @return true if status screen should continue, false if user pressed Escape to exit
 */
bool ui_status_display_interactive(const ui_status_t *status);

/**
 * @brief Periodically update status display with live logs at FPS rate
 *
 * Gathers and displays status with live log feed if enough time has
 * passed since last update (based on GET_OPTION(fps)). Updates at 60 Hz by default.
 *
 * @param server Initialized TCP server structure
 * @param session_string Memorable session string
 * @param ipv4_address IPv4 bind address (can be NULL or empty)
 * @param ipv6_address IPv6 bind address (can be NULL or empty)
 * @param port TCP listen port
 * @param start_time Server start time
 * @param mode_name Mode name for display (e.g., "Server")
 * @param session_is_mdns_only Whether session is mDNS-only or ACDS
 * @param[in,out] last_update_ns Last update time in microseconds (from platform_get_monotonic_time_us)
 */
void ui_status_update(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                          const char *ipv6_address, uint16_t port, time_t start_time, const char *mode_name,
                          bool session_is_mdns_only, uint64_t *last_update_ns);

/**
 * @brief Initialize status screen log capture system
 *
 * Must be called before using status screen. Creates internal log buffer.
 */
void ui_status_log_init(void);

/**
 * @brief Cleanup status screen log capture system
 *
 * Call when shutting down. Frees internal log buffer.
 */
void ui_status_log_destroy(void);

/**
 * @brief Append a log message to status screen buffer
 *
 * Thread-safe. Called from logging system to capture messages.
 *
 * @param message Log message text (already formatted with colors)
 */
void ui_status_log_append(const char *message);

/**
 * @brief Clear all log messages from status screen buffer
 *
 * Thread-safe. Resets buffer to empty state, discarding all captured logs.
 * Used to clear initialization logs before status screen starts rendering.
 */
void ui_status_log_clear(void);
