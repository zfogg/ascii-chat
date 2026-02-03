/**
 * @file lib/session/discovery_status.c
 * @brief Discovery service status screen display implementation
 */

#include "../../include/ascii-chat/session/discovery_status.h"
#include "../../include/ascii-chat/common.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

asciichat_error_t discovery_status_gather(tcp_server_t *server, discovery_database_t *db, const char *ipv4_address,
                                          const char *ipv6_address, uint16_t port, time_t start_time,
                                          discovery_status_t *out_status) {
  if (!server || !db || !out_status) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for discovery_status_gather");
  }

  memset(out_status, 0, sizeof(*out_status));

  // Format IPv4 address
  out_status->ipv4_bound = (ipv4_address && ipv4_address[0] != '\0');
  if (out_status->ipv4_bound) {
    snprintf(out_status->ipv4_address, sizeof(out_status->ipv4_address), "%s:%u", ipv4_address, port);
  }

  // Format IPv6 address
  out_status->ipv6_bound = (ipv6_address && ipv6_address[0] != '\0');
  if (out_status->ipv6_bound) {
    snprintf(out_status->ipv6_address, sizeof(out_status->ipv6_address), "[%s]:%u", ipv6_address, port);
  }

  out_status->port = port;
  out_status->start_time = start_time;
  out_status->connected_servers = tcp_server_get_client_count(server);

  // Count active sessions in database
  // This would require a query function in discovery/database.h
  // For now, set to 0 - this will be implemented based on database API
  out_status->active_sessions = 0;

  return ASCIICHAT_OK;
}

void discovery_status_display(const discovery_status_t *status) {
  if (!status) {
    return;
  }

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / 3600;
  int uptime_mins = (uptime_secs % 3600) / 60;
  int uptime_secs_rem = uptime_secs % 60;

  // Clear screen and move to home position
  printf("\033[H\033[2J");

  // Header
  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  printf("\033[1;36mDiscovery Service Status\033[0m\n");
  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

  // Bind addresses
  if (status->ipv4_bound) {
    printf("📍 IPv4: %s\n", status->ipv4_address);
  }
  if (status->ipv6_bound) {
    printf("📍 IPv6: %s\n", status->ipv6_address);
  }

  // Connected servers
  printf("🖥️  Connected Servers: \033[1;33m%zu\033[0m\n", status->connected_servers);

  // Active sessions
  printf("🔗 Active Sessions: \033[1;33m%zu\033[0m\n", status->active_sessions);

  // Uptime
  printf("⏱️  Uptime: %dh %dm %ds\n", uptime_hours, uptime_mins, uptime_secs_rem);

  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  fflush(stdout);
}

void discovery_status_update(tcp_server_t *server, discovery_database_t *db, const char *ipv4_address,
                             const char *ipv6_address, uint16_t port, time_t start_time, time_t *last_update) {
  if (!server || !db || !last_update) {
    return;
  }

  // Only update every 1-2 seconds
  time_t now = time(NULL);
  if (now - *last_update < 1) {
    return;
  }

  discovery_status_t status;
  if (discovery_status_gather(server, db, ipv4_address, ipv6_address, port, start_time, &status) == ASCIICHAT_OK) {
    discovery_status_display(&status);
    *last_update = now;
  }
}
