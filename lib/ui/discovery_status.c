/**
 * @file lib/ui/discovery_status.c
 * @brief Discovery service status screen display implementation
 */

#include <ascii-chat/ui/discovery_status.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/options/options.h>
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

/**
 * @brief Render discovery status header (callback for terminal_screen)
 *
 * Renders status information in a fixed header using frame buffer.
 * Lines are accumulated in the frame buffer and output atomically.
 */
static void render_discovery_status_header(frame_buffer_t *buf, terminal_size_t term_size, void *user_data) {
  const discovery_status_t *status = (const discovery_status_t *)user_data;
  if (!status) {
    return;
  }

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / SEC_PER_HOUR;
  int uptime_mins = (uptime_secs % SEC_PER_HOUR) / SEC_PER_MIN;
  int uptime_secs_rem = uptime_secs % 60;

  // Top border
  frame_buffer_printf(buf, "\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    frame_buffer_printf(buf, "━");
  }
  frame_buffer_printf(buf, "\033[0m\n");

  // Title line
  char title[] = "ascii-chat discovery-service Status";
  int title_padding = display_center_horizontal(title, term_size.cols - 2);
  for (int i = 0; i < title_padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "\033[1;36m%s\033[0m\n", title);

  // Status info line with servers and sessions
  char info_line[256];
  snprintf(info_line, sizeof(info_line), "🖥️  %zu Server | 🔗 %zu Session | ⏱️ ", status->connected_servers,
           status->active_sessions);

  char uptime_str[12];
  format_uptime_hms(uptime_hours, uptime_mins, uptime_secs_rem, uptime_str, sizeof(uptime_str));
  strncat(info_line, uptime_str, sizeof(info_line) - strlen(info_line) - 1);

  int info_padding = display_center_horizontal(info_line, term_size.cols - 2);
  for (int i = 0; i < info_padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", info_line);

  // Address info
  char addr_line[256];
  int pos = 0;

  if (status->ipv4_bound) {
    char ipv4_only[64];
    const char *type = "";
    if (extract_ip_from_address(status->ipv4_address, ipv4_only, sizeof(ipv4_only)) == 0) {
      type = get_ip_type_string(ipv4_only);
    }
    if (type[0] != '\0') {
      pos = snprintf(addr_line, sizeof(addr_line), "📍 IPv4: %s (%s)", status->ipv4_address, type);
    } else {
      pos = snprintf(addr_line, sizeof(addr_line), "📍 IPv4: %s", status->ipv4_address);
    }
  }

  if (status->ipv6_bound) {
    char ipv6_only[64];
    const char *type = "";
    if (extract_ip_from_address(status->ipv6_address, ipv6_only, sizeof(ipv6_only)) == 0) {
      type = get_ip_type_string(ipv6_only);
    }
    if (pos > 0) {
      pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, " | ");
    }
    if (type[0] != '\0') {
      snprintf(addr_line + pos, sizeof(addr_line) - pos, "📍 IPv6: %s (%s)", status->ipv6_address, type);
    } else {
      snprintf(addr_line + pos, sizeof(addr_line) - pos, "📍 IPv6: %s", status->ipv6_address);
    }
  }

  int addr_padding = display_center_horizontal(addr_line, term_size.cols - 2);
  for (int i = 0; i < addr_padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", addr_line);

  // Bottom border
  frame_buffer_printf(buf, "\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    frame_buffer_printf(buf, "━");
  }
  frame_buffer_printf(buf, "\033[0m\n");
}

void discovery_status_display(const discovery_status_t *status) {
  if (!status) {
    return;
  }

  // Only render if interactive AND status screen is enabled AND was explicitly set
  if (!terminal_is_interactive() || !GET_OPTION(status_screen) || !GET_OPTION(status_screen_explicitly_set)) {
    return;
  }

  // Use terminal_screen abstraction for rendering with frame buffer
  terminal_screen_config_t config = {
      .fixed_header_lines = 5,
      .render_header = render_discovery_status_header,
      .user_data = (void *)status,
      .show_logs = false, // Discovery status doesn't show logs, just header
  };

  terminal_screen_render(&config);
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
