/**
 * @file lib/ui/discovery_status.c
 * @brief Discovery service status screen display implementation
 */

#include <ascii-chat/ui/discovery_status.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/common.h>
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

  // Get actual terminal dimensions
  terminal_size_t term_size;
  if (terminal_get_size(&term_size) != ASCIICHAT_OK) {
    // Fallback to standard dimensions
    term_size.cols = 80;
    term_size.rows = 24;
  }

  // Content is max 80 chars wide, centered in terminal
  const int CONTENT_WIDTH = 80;
  int box_padding = (term_size.cols > CONTENT_WIDTH) ? (term_size.cols - CONTENT_WIDTH) / 2 : 0;

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / 3600;
  int uptime_mins = (uptime_secs % 3600) / 60;
  int uptime_secs_rem = uptime_secs % 60;

  // Clear screen and move to home position
  printf("\033[H\033[2J");

  // Add vertical centering
  int vertical_padding = display_center_vertical(10, term_size.rows);
  for (int i = 0; i < vertical_padding; i++)
    printf("\n");

  // Print top border (80 chars wide, centered)
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  for (int i = 0; i < CONTENT_WIDTH; i++)
    printf("━");
  printf("\n");

  // Build and center the title
  char title[] = "ascii-chat discovery-service Status";

  char colored_title[256];
  snprintf(colored_title, sizeof(colored_title), "\033[1;36m%s\033[0m", title);

  // Calculate horizontal padding to center within the 80-char box
  int horizontal_padding = display_center_horizontal(title, CONTENT_WIDTH);

  // Print centered title (80 chars wide, centered)
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  for (int i = 0; i < horizontal_padding; i++)
    printf(" ");
  printf("%s\n", colored_title);

  // Print bottom border (80 chars wide, centered)
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  for (int i = 0; i < CONTENT_WIDTH; i++)
    printf("━");
  printf("\n");

  // Bind addresses with IP type
  if (status->ipv4_bound) {
    char ipv4_only[64];
    const char *type = "";
    if (extract_ip_from_address(status->ipv4_address, ipv4_only, sizeof(ipv4_only)) == 0) {
      type = get_ip_type_string(ipv4_only);
    }
    for (int i = 0; i < box_padding; i++)
      printf(" ");
    if (type[0] != '\0') {
      printf("📍 IPv4: %s (%s)\n", status->ipv4_address, type);
    } else {
      printf("📍 IPv4: %s\n", status->ipv4_address);
    }
  }
  if (status->ipv6_bound) {
    char ipv6_only[64];
    const char *type = "";
    if (extract_ip_from_address(status->ipv6_address, ipv6_only, sizeof(ipv6_only)) == 0) {
      type = get_ip_type_string(ipv6_only);
    }
    for (int i = 0; i < box_padding; i++)
      printf(" ");
    if (type[0] != '\0') {
      printf("📍 IPv6: %s (%s)\n", status->ipv6_address, type);
    } else {
      printf("📍 IPv6: %s\n", status->ipv6_address);
    }
  }

  // Connected servers
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  printf("🖥️  Connected Servers: \033[1;33m%zu\033[0m\n", status->connected_servers);

  // Active sessions
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  printf("🔗 Active Sessions: \033[1;33m%zu\033[0m\n", status->active_sessions);

  // Uptime
  char uptime_str[12];
  format_uptime_hms(uptime_hours, uptime_mins, uptime_secs_rem, uptime_str, sizeof(uptime_str));
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  printf("⏱️ %s\n", uptime_str);

  // Print bottom border (80 chars wide, centered)
  for (int i = 0; i < box_padding; i++)
    printf(" ");
  for (int i = 0; i < CONTENT_WIDTH; i++)
    printf("━");
  printf("\n");
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
