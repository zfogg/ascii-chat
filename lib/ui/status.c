/**
 * @file lib/ui/status.c
 * @brief Status screen display with live log feed at FPS rate
 */

#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/status.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/session/session_log_buffer.h>
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
#include <unistd.h>
#include <ascii-chat/atomic.h>

void ui_status_log_init(void) {
  // Initialize the shared terminal screen log buffer
  session_log_buffer_t *buf = terminal_screen_log_init();
  if (buf) {
    log_set_session_log_buffer(buf);
  }
}

void ui_status_log_destroy(void) {
  // Unregister from logger before destroying
  log_clear_session_log_buffer();
  // Cleanup the shared terminal screen log buffer
  terminal_screen_log_destroy();
}

void ui_status_log_clear(void) {
  // Delegate to terminal_screen log abstraction
  terminal_screen_log_clear();
}

// ============================================================================
// Status Display
// ============================================================================

asciichat_error_t ui_status_gather(network_server_t *server, const char *session_string, const char *ipv4_address,
                                   const char *ipv6_address, uint16_t port, time_t start_time, const char *mode_name,
                                   bool session_is_mdns_only, ui_status_t *out_status) {
  if (!server || !out_status || !mode_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
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
  out_status->connected_count = network_server_connection_count(server);

  return ASCIICHAT_OK;
}

void ui_status_display(const ui_status_t *status) {
  if (!status) {
    return;
  }

  // Only render the status screen if:
  // 1. Terminal is interactive (default behavior), OR
  // 2. User explicitly set --status-screen on command line (force it regardless of terminal)
  // In non-interactive mode without explicit flag, logs flow to stdout/stderr normally
  if (!terminal_is_interactive() && !GET_OPTION(status_screen_explicitly_set)) {
    return;
  }

  // Redirect status screen to stderr when stdout is piped (not a TTY).
  // This keeps stdout clean for frame data while status goes to stderr.
  // Uses platform_isatty() so non-desktop platforms (iOS, WASM) can override.
  if (!platform_isatty(STDOUT_FILENO)) {
    terminal_screen_set_output_fd(STDERR_FILENO);
  }

  // If --grep pattern was provided, enter interactive grep mode with it pre-populated
  // Only do this once (check if not already entering)
  static bool grep_mode_entered = false;
  if (!grep_mode_entered && grep_get_last_pattern() && grep_get_last_pattern()[0] != '\0') {
    log_search_enter_mode();
    grep_mode_entered = true;
  }

  // Use terminal_screen abstraction for rendering
  terminal_screen_config_t config = {
      .screen = UI_SCREEN_STATUS,
      .output_fd = platform_isatty(STDOUT_FILENO) ? STDOUT_FILENO : STDERR_FILENO,
      .user_data_size = sizeof(*status),
      .minimum_cols = 20,
      .fixed_header_lines = 4,
      .render_header = ui_status_render_header,
      .user_data = (void *)status,
      .show_logs = true,
  };

  terminal_screen_render(&config);
}

/**
 * @brief Display status screen with keyboard input support
 * Returns true if status screen should continue, false if Escape was pressed to exit
 */
bool ui_status_display_interactive(const ui_status_t *status) {
  if (!status) {
    return true;
  }

  // Only render the status screen if:
  // 1. Terminal is interactive (default behavior), OR
  // 2. User explicitly set --status-screen on command line (force it regardless of terminal)
  // In non-interactive mode without explicit flag, logs flow to stdout/stderr normally
  if (!terminal_is_interactive() && !GET_OPTION(status_screen_explicitly_set)) {
    return true;
  }

  // If --grep pattern was provided, enter interactive grep mode with it pre-populated
  static bool grep_mode_entered = false;
  if (!grep_mode_entered && grep_get_last_pattern() && grep_get_last_pattern()[0] != '\0') {
    log_search_enter_mode();
    grep_mode_entered = true;
  }

  // Status may use stderr when stdout is redirected. Check the descriptors
  // used by this screen instead of requiring stdout to be a terminal.
  int output_fd = platform_isatty(STDOUT_FILENO) ? STDOUT_FILENO : STDERR_FILENO;
  bool keyboard_enabled = false;
  if (platform_isatty(STDIN_FILENO) && platform_isatty(output_fd) && keyboard_init() == ASCIICHAT_OK) {
    keyboard_enabled = true;
  }

  // Use terminal_screen abstraction for rendering
  terminal_screen_config_t config = {
      .screen = UI_SCREEN_STATUS,
      .output_fd = output_fd,
      .user_data_size = sizeof(*status),
      .minimum_cols = 20,
      .fixed_header_lines = 4,
      .render_header = ui_status_render_header,
      .user_data = (void *)status,
      .show_logs = true,
  };

  terminal_screen_render(&config);

  // Poll keyboard for Escape to exit or for interactive grep
  bool should_exit_status = false;
  if (keyboard_enabled) {
    keyboard_key_t key = ui_input_read_key(UI_SCREEN_STATUS);
    if (key == KEY_ESCAPE) {
      // Escape key: cancel grep if active, otherwise exit status screen
      if (log_search_is_active()) {
        log_search_exit_mode(false); // Cancel grep without applying
      } else {
        should_exit_status = true; // Exit status screen
      }
    } else if (key != KEY_NONE && log_search_should_handle(key)) {
      log_search_handle_key(key);
    }
  }

  // Keyboard ownership lasts until session shutdown, including between status frames.

  return !should_exit_status; // Return false if user wants to exit
}

void ui_status_update(network_server_t *server, const char *session_string, const char *ipv4_address,
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

  ui_status_t status;
  if (ui_status_gather(server, session_string, ipv4_address, ipv6_address, port, start_time, mode_name,
                       session_is_mdns_only, &status) == ASCIICHAT_OK) {
    ui_status_display(&status);
    *last_update_ns = now_us;
  }
}
