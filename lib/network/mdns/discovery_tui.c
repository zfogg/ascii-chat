/**
 * @file discovery_tui.c
 * @brief TUI-based service discovery wrapper for interactive server selection
 *
 * Pure TUI wrapper that calls discovery_mdns_query() from discovery.c.
 * Provides interactive terminal UI for server selection and address resolution.
 */

#include <ascii-chat/network/mdns/discovery_tui.h>
#include <ascii-chat/network/mdns/discovery.h> // For discovery_mdns_query()
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/abstraction.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/**
 * @brief TUI wrapper around core mDNS discovery
 *
 * Calls discovery_mdns_query() from discovery.c with TUI-friendly configuration.
 */
discovery_tui_server_t *discovery_tui_query(const discovery_tui_config_t *config, int *out_count) {
  if (!out_count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "out_count pointer is NULL");
    return NULL;
  }

  *out_count = 0;

  // Apply defaults if needed
  int timeout_ms = (config && config->timeout_ms > 0) ? config->timeout_ms : 2000;
  int max_servers = (config && config->max_servers > 0) ? config->max_servers : 20;
  bool quiet = (config && config->quiet);

  // Call the core mDNS discovery function from discovery.c
  return discovery_mdns_query(timeout_ms, max_servers, quiet, out_count);
}

/**
 * @brief Free results from mDNS discovery
 */
void discovery_tui_free_results(discovery_tui_server_t *servers) {
  discovery_mdns_free(servers);
}

/**
 * @brief Interactive server selection
 */
int discovery_tui_prompt_selection(const discovery_tui_server_t *servers, int count) {
  if (!servers || count <= 0) {
    return -1;
  }

  // Display available servers
  printf("\nAvailable ascii-chat servers on LAN:\n");
  for (int i = 0; i < count; i++) {
    const discovery_tui_server_t *srv = &servers[i];
    const char *addr = discovery_tui_get_best_address(srv);
    printf("  %d. %s (%s:%u)\n", i + 1, srv->name, addr, srv->port);
  }

  // Prompt for selection
  printf("\nSelect server (1-%d) or press Enter to cancel: ", count);
  fflush(stdout);

  // Read user input
  char input[32];
  if (fgets(input, sizeof(input), stdin) == NULL) {
    printf("\n");
    return -1; // EOF or error
  }

  // Check for empty input (Enter pressed)
  if (input[0] == '\n' || input[0] == '\r' || input[0] == '\0') {
    return -1; // User cancelled
  }

  // Parse input as number
  char *endptr;
  long selection = strtol(input, &endptr, 10);

  // Validate input
  if (selection < 1 || selection > count) {
    printf("⚠️  Invalid selection. Please enter a number between 1 and %d\n", count);
    return discovery_tui_prompt_selection(servers, count); // Re-prompt
  }

  return (int)(selection - 1); // Convert to 0-based index
}

/**
 * @brief ANSI escape codes for TUI
 */
#define ANSI_CLEAR "\033[2J\033[H"   // Clear screen and move cursor to top
#define ANSI_BOLD "\033[1m"          // Bold text
#define ANSI_RESET "\033[0m"         // Reset formatting
#define ANSI_CYAN "\033[36m"         // Cyan text
#define ANSI_GREEN "\033[32m"        // Green text
#define ANSI_YELLOW "\033[33m"       // Yellow text
#define ANSI_HIDE_CURSOR "\033[?25l" // Hide cursor
#define ANSI_SHOW_CURSOR "\033[?25h" // Show cursor
#define ANSI_CLEAR_LINE "\033[K"     // Clear to end of line

/**
 * @brief TUI-based server selection with formatted display
 *
 * Displays discovered servers in a terminal UI with the following features:
 * - Clears terminal and displays formatted server list
 * - Shows "No results" message if no servers available
 * - Allows numeric input for selection
 * - Shows helpful prompts and icons
 *
 * @param servers Array of discovered servers
 * @param count Number of servers
 * @return 0-based index of selected server, or -1 to cancel
 */
int discovery_tui_select(const discovery_tui_server_t *servers, int count) {
  if (!servers || count <= 0) {
    // No servers found - return special code
    // Message will be printed at exit in client main
    return -1;
  }

  // Lock terminal to prevent concurrent logging from overwriting TUI
  bool prev_lock_state = log_lock_terminal();

  // Clear terminal
  log_plain("%s", ANSI_CLEAR);

  // Display header
  log_plain("\n");
  log_plain("%s╭─ 🔍 ascii-chat Server Discovery %s────────────╮%s\n", ANSI_BOLD, ANSI_RESET, ANSI_BOLD);
  log_plain("│%s\n", ANSI_RESET);
  log_plain("%s│%s Found %d server%s on your local network:%s\n", ANSI_BOLD, ANSI_GREEN, count, count == 1 ? "" : "s",
            ANSI_RESET);
  log_plain("%s│%s\n", ANSI_BOLD, ANSI_RESET);

  // Display server list with formatting
  for (int i = 0; i < count; i++) {
    const discovery_tui_server_t *srv = &servers[i];
    const char *addr = discovery_tui_get_best_address(srv);

    log_plain("%s│%s  ", ANSI_BOLD, ANSI_RESET);
    log_plain("%s[%d]%s %-30s %s%s:%u%s", ANSI_CYAN, i + 1, ANSI_RESET, srv->name, ANSI_YELLOW, addr, srv->port,
              ANSI_RESET);
    log_plain("\n");
  }

  log_plain("%s│%s\n", ANSI_BOLD, ANSI_RESET);
  log_plain("%s╰────────────────────────────────────────────╯%s\n", ANSI_BOLD, ANSI_RESET);

  // Prompt for selection
  log_plain("\n");
  log_plain("Enter server number (1-%d) or press Enter to cancel: ", count);
  fflush(stdout);

  // Unlock before waiting for input (user might take time)
  log_unlock_terminal(prev_lock_state);

  // Read user input
  char input[32];
  if (fgets(input, sizeof(input), stdin) == NULL) {
    return -1;
  }

  // Check for empty input (Enter pressed)
  if (input[0] == '\n' || input[0] == '\r' || input[0] == '\0') {
    return -1;
  }

  // Parse input as number
  char *endptr;
  long selection = strtol(input, &endptr, 10);

  // Validate input
  if (selection < 1 || selection > count) {
    printf("%sError:%s Please enter a number between 1 and %d\n\n", ANSI_YELLOW, ANSI_RESET, count);
    return discovery_tui_select(servers, count); // Re-prompt
  }

  // Lock terminal again for final output
  prev_lock_state = log_lock_terminal();

  // Clear screen and show connection status
  log_plain("%s", ANSI_CLEAR);
  log_plain("\n");
  log_plain("%s🔗 Connecting to %s...%s\n", ANSI_GREEN, servers[selection - 1].name, ANSI_RESET);
  log_plain("\n");
  fflush(stdout);

  // Brief delay so user can see the selection before logs overwrite it
  platform_sleep_ms(200);

  // Unlock terminal now that TUI is complete
  log_unlock_terminal(prev_lock_state);

  return (int)(selection - 1); // Convert to 0-based index
}

/**
 * @brief Get best address for a server
 */
const char *discovery_tui_get_best_address(const discovery_tui_server_t *server) {
  if (!server) {
    return "";
  }

  // Prefer IPv4 > name > IPv6
  if (server->ipv4[0] != '\0') {
    return server->ipv4;
  }
  if (server->name[0] != '\0') {
    return server->name;
  }
  if (server->ipv6[0] != '\0') {
    return server->ipv6;
  }

  return server->address; // Fallback to address field
}
