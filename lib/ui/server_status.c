/**
 * @file lib/ui/server_status.c
 * @brief Server status screen display with live log feed at FPS rate
 */

#include <ascii-chat/ui/server_status.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/log/interactive_grep.h>
#include "session/session_log_buffer.h"
#include <ascii-chat/log/grep.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/common.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

void server_status_log_init(void) {
  // Delegate to shared session log buffer
  (void)session_log_buffer_init();
}

void server_status_log_destroy(void) {
  // Delegate to shared session log buffer
  session_log_buffer_destroy();
}

void server_status_log_clear(void) {
  // Delegate to shared session log buffer
  session_log_buffer_clear();
}

void server_status_log_append(const char *message) {
  // Delegate to shared session log buffer
  session_log_buffer_append(message);
}

// ============================================================================
// Status Display
// ============================================================================

asciichat_error_t server_status_gather(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                                       const char *ipv6_address, uint16_t port, time_t start_time,
                                       const char *mode_name, bool session_is_mdns_only, server_status_t *out_status) {
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
  out_status->session_is_mdns_only = session_is_mdns_only;
  out_status->connected_count = tcp_server_get_client_count(server);

  return ASCIICHAT_OK;
}

/**
 * @brief Truncate text to fit within display_width
 *
 * Truncates a plain text string to fit within max_display_width, with ellipsis.
 * Tests progressively shorter substrings until one fits.
 */
/**
 * @brief Render server status header (callback for terminal_screen)
 *
 * Renders exactly 4 lines:
 * - Line 1: Top border
 * - Line 2: Title + client count + uptime
 * - Line 3: Session string + addresses
 * - Line 4: Bottom border
 *
 * Lines are truncated if they exceed terminal width.
 */
static void render_server_status_header(frame_buffer_t *buf, terminal_size_t term_size, void *user_data) {
  const server_status_t *status = (const server_status_t *)user_data;
  if (!status) {
    return;
  }

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / SEC_PER_HOUR;
  int uptime_mins = (uptime_secs % SEC_PER_HOUR) / SEC_PER_MIN;
  int uptime_secs_rem = uptime_secs % 60;

  // Line 1: Top border
  frame_buffer_printf(buf, "\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    frame_buffer_printf(buf, "━");
  }
  frame_buffer_printf(buf, "\033[0m\n");

  // Line 2: Title + Stats (centered)
  char status_line[512];
  char status_line_truncated[512];
  char status_line_colored[600];
  char uptime_str[12];
  format_uptime_hms(uptime_hours, uptime_mins, uptime_secs_rem, uptime_str, sizeof(uptime_str));
  snprintf(status_line, sizeof(status_line), "ascii-chat %s | 👥 %zu | ⏱️ %s", status->mode_name,
           status->connected_count, uptime_str);

  // Truncate if needed
  truncate_utf8_with_ellipsis(status_line, status_line_truncated, sizeof(status_line_truncated), term_size.cols - 2);
  snprintf(status_line_colored, sizeof(status_line_colored), "\033[1;36m%s\033[0m", status_line_truncated);

  int padding = display_center_horizontal(status_line_truncated, term_size.cols);
  for (int i = 0; i < padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", status_line_colored);

  // Line 3: Session + Addresses (centered)
  char addr_line[512];
  char addr_line_truncated[512];
  int pos = 0;
  if (status->session_string[0] != '\0') {
    pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, "🔗 %s", status->session_string);
  }
  if (status->ipv4_bound && pos < (int)sizeof(addr_line) - 30) {
    if (pos > 0) {
      pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, " | ");
    }
    // Extract IP and get type
    char ipv4_only[64];
    if (extract_ip_from_address(status->ipv4_address, ipv4_only, sizeof(ipv4_only)) == 0) {
      const char *type = get_ip_type_string(ipv4_only);
      pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, "%s (%s)", status->ipv4_address, type);
    } else {
      pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, "%s", status->ipv4_address);
    }
  }
  if (status->ipv6_bound && pos < (int)sizeof(addr_line) - 30) {
    if (pos > 0) {
      pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, " | ");
    }
    // Extract IP and get type
    char ipv6_only[64];
    if (extract_ip_from_address(status->ipv6_address, ipv6_only, sizeof(ipv6_only)) == 0) {
      const char *type = get_ip_type_string(ipv6_only);
      snprintf(addr_line + pos, sizeof(addr_line) - pos, "%s (%s)", status->ipv6_address, type);
    } else {
      snprintf(addr_line + pos, sizeof(addr_line) - pos, "%s", status->ipv6_address);
    }
  }

  // Truncate if needed
  truncate_utf8_with_ellipsis(addr_line, addr_line_truncated, sizeof(addr_line_truncated), term_size.cols - 2);

  int addr_padding = display_center_horizontal(addr_line_truncated, term_size.cols);
  for (int i = 0; i < addr_padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", addr_line_truncated);

  // Line 4: Bottom border
  frame_buffer_printf(buf, "\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    frame_buffer_printf(buf, "━");
  }
  frame_buffer_printf(buf, "\033[0m\n");
}

void server_status_display(const server_status_t *status) {
  if (!status) {
    return;
  }

  // Only render the status screen in interactive mode
  // In non-interactive mode, logs flow to stdout/stderr normally
  if (!terminal_is_interactive()) {
    return;
  }

  // If --grep pattern was provided, enter interactive grep mode with it pre-populated
  // Only do this once (check if not already entering)
  static bool grep_mode_entered = false;
  if (!grep_mode_entered && grep_get_last_pattern() && grep_get_last_pattern()[0] != '\0') {
    interactive_grep_enter_mode();
    grep_mode_entered = true;
  }

  // Use terminal_screen abstraction for rendering
  terminal_screen_config_t config = {
      .fixed_header_lines = 4,
      .render_header = render_server_status_header,
      .user_data = (void *)status,
      .show_logs = true,
  };

  terminal_screen_render(&config);
}

/**
 * @brief Display status screen with keyboard input support
 * Returns true if status screen should continue, false if Escape was pressed to exit
 */
bool server_status_display_interactive(const server_status_t *status) {
  if (!status) {
    return true;
  }

  // Only render the status screen in interactive mode
  if (!terminal_is_interactive()) {
    return true;
  }

  // If --grep pattern was provided, enter interactive grep mode with it pre-populated
  static bool grep_mode_entered = false;
  if (!grep_mode_entered && grep_get_last_pattern() && grep_get_last_pattern()[0] != '\0') {
    interactive_grep_enter_mode();
    grep_mode_entered = true;
  }

  // Initialize keyboard for interactive grep
  bool keyboard_enabled = false;
  if (keyboard_init() == ASCIICHAT_OK) {
    keyboard_enabled = true;
  }

  // Use terminal_screen abstraction for rendering
  terminal_screen_config_t config = {
      .fixed_header_lines = 4,
      .render_header = render_server_status_header,
      .user_data = (void *)status,
      .show_logs = true,
  };

  terminal_screen_render(&config);

  // Poll keyboard for Escape to exit or for interactive grep
  bool should_exit_status = false;
  if (keyboard_enabled) {
    keyboard_key_t key = keyboard_read_nonblocking();
    if (key == KEY_ESCAPE) {
      // Escape key: cancel grep if active, otherwise exit status screen
      if (interactive_grep_is_active()) {
        interactive_grep_exit_mode(false); // Cancel grep without applying
      } else {
        should_exit_status = true; // Exit status screen
      }
    } else if (key != KEY_NONE && interactive_grep_should_handle(key)) {
      interactive_grep_handle_key(key);
    }
  }

  // Cleanup keyboard
  if (keyboard_enabled) {
    keyboard_destroy();
  }

  return !should_exit_status; // Return false if user wants to exit
}

void server_status_update(tcp_server_t *server, const char *session_string, const char *ipv4_address,
                          const char *ipv6_address, uint16_t port, time_t start_time, const char *mode_name,
                          bool session_is_mdns_only, uint64_t *last_update_ns) {
  if (!server || !last_update_ns) {
    return;
  }

  // Update at FPS rate (60 Hz = 16.67ms by default)
  uint32_t fps = GET_OPTION(fps);
  if (fps == 0) {
    fps = 60; // Default
  }

  // Calculate frame interval in microseconds
  uint64_t frame_interval_us = US_PER_SEC_INT / fps;

  // Get current time in microseconds using platform abstraction
  uint64_t now_us = platform_get_monotonic_time_us();

  // Check if enough time has passed
  if ((now_us - *last_update_ns) < frame_interval_us) {
    return; // Too soon, skip frame
  }

  server_status_t status;
  if (server_status_gather(server, session_string, ipv4_address, ipv6_address, port, start_time, mode_name,
                           session_is_mdns_only, &status) == ASCIICHAT_OK) {
    server_status_display(&status);
    *last_update_ns = now_us;
  }
}
