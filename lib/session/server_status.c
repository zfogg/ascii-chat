/**
 * @file lib/session/server_status.c
 * @brief Server status screen display implementation
 */

#include "../../include/ascii-chat/session/server_status.h"
#include "../../include/ascii-chat/common.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

asciichat_error_t server_status_gather(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                                       const char *ipv6_address, uint16_t port, time_t start_time,
                                       const char *mode_name, server_status_t *out_status) {
  if (!server || !out_status || !mode_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for server_status_gather");
  }

  memset(out_status, 0, sizeof(*out_status));

  // Copy session string
  if (session_string) {
    SAFE_STRNCPY(out_status->session_string, session_string, sizeof(out_status->session_string));
  }

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
  out_status->mode_name = mode_name;
  out_status->connected_count = tcp_server_get_client_count(server);

  return ASCIICHAT_OK;
}

void server_status_display(const server_status_t *status) {
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
  printf("\033[1;36m%s Status\033[0m\n", status->mode_name);
  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

  // Session information
  if (status->session_string[0] != '\0') {
    printf("🔗 \033[1;32mSession:\033[0m %s\n", status->session_string);
  }

  // Bind addresses
  if (status->ipv4_bound) {
    printf("📍 IPv4: %s\n", status->ipv4_address);
  }
  if (status->ipv6_bound) {
    printf("📍 IPv6: %s\n", status->ipv6_address);
  }

  // Connected count
  printf("👥 Connected: \033[1;33m%zu\033[0m\n", status->connected_count);

  // Uptime
  printf("⏱️  Uptime: %dh %dm %ds\n", uptime_hours, uptime_mins, uptime_secs_rem);

  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  fflush(stdout);
}

void server_status_update(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                          const char *ipv6_address, uint16_t port, time_t start_time, const char *mode_name,
                          time_t *last_update) {
  if (!server || !last_update) {
    return;
  }

  // Only update every 1-2 seconds
  time_t now = time(NULL);
  if (now - *last_update < 1) {
    return;
  }

  server_status_t status;
  if (server_status_gather(server, session_string, ipv4_address, ipv6_address, port, start_time, mode_name, &status) ==
      ASCIICHAT_OK) {
    server_status_display(&status);
    *last_update = now;
  }
}
