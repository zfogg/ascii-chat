/**
 * @file ui/mdns.c
 * @brief mDNS service discovery UI wrapper for interactive server selection
 *
 * Pure TUI wrapper that calls discovery_mdns_query() from discovery.c.
 * Provides interactive terminal UI for server selection and address resolution.
 */

#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/ui/mdns.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/network/mdns/discovery.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ============================================================================
 * Log Management
 * ========================================================================== */

void ui_mdns_log_init(void) {
  session_log_buffer_t *buf = terminal_screen_log_init();
  if (buf) {
    log_set_session_log_buffer(buf);
  }
}

void ui_mdns_log_destroy(void) {
  log_clear_session_log_buffer();
  terminal_screen_log_destroy();
}

void ui_mdns_log_clear(void) {
  terminal_screen_log_clear();
}

void ui_mdns_log_append(const char *message) {
  if (message) {
    log_info("%s", message);
  }
}

/**
 * @brief TUI wrapper around core mDNS discovery
 *
 * Calls discovery_mdns_query() from discovery.c with TUI-friendly configuration.
 */
ui_mdns_server_t *ui_mdns_query(const ui_mdns_config_t *config, int *out_count) {
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
void ui_mdns_free_results(ui_mdns_server_t *servers) {
  discovery_mdns_destroy(servers);
}

/**
 * @brief Interactive server selection
 */
int ui_mdns_prompt_selection(const ui_mdns_server_t *servers, int count) {
  if (terminal_can_prompt_user())
    return ui_mdns_select(servers, count);
  if (!servers || count <= 0) {
    return -1;
  }

  log_error("Cannot select a discovered server without an interactive terminal; specify a server address.");
  return -1;
}

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
typedef struct {
  int count;
  char input[32];
  ui_mdns_server_t servers[];
} mdns_snapshot_t;

static void render_mdns_selection(terminal_size_t size, const void *data) {
  const mdns_snapshot_t *snapshot = data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  frame_buffer_printf(buffer, "\033[H\033[2Jascii-chat Server Discovery\n\n");
  for (int i = 0; i < snapshot->count; ++i) {
    char line[512], clipped[512];
    snprintf(line, sizeof(line), "[%d] %s (%s:%u)", i + 1, snapshot->servers[i].name,
             ui_mdns_get_best_address(&snapshot->servers[i]), snapshot->servers[i].port);
    truncate_with_ellipsis(line, clipped, sizeof(clipped), size.cols - 1);
    frame_buffer_printf(buffer, "%s\n", clipped);
  }
  frame_buffer_printf(buffer, "\nTimeout: %us (cancel)\nSelect server: %s\033[K",
                      30u, snapshot->input);
  frame_buffer_flush(buffer);
  frame_buffer_destroy(buffer);
}

int ui_mdns_select(const ui_mdns_server_t *servers, int count) {
  if (!servers || count <= 0)
    return -1;
  if (!terminal_can_prompt_user())
    return ui_mdns_prompt_selection(servers, count);
  size_t bytes = sizeof(mdns_snapshot_t) + (size_t)count * sizeof(*servers);
  mdns_snapshot_t *snapshot = SAFE_CALLOC(1, bytes, mdns_snapshot_t *);
  if (!snapshot)
    return -1;
  snapshot->count = count;
  memcpy(snapshot->servers, servers, (size_t)count * sizeof(*servers));
  bool logging = log_get_terminal_output();
  log_set_terminal_output(false);
  int selection = -1;
  size_t length = 0;
  uint64_t deadline = ui_input_deadline(30);
  bool timed_out = false;
  while (!shutdown_is_requested()) {
    if (ui_controller_submit(UI_SCREEN_MDNS, STDOUT_FILENO, (terminal_size_t){.cols = 30, .rows = count + 6},
                             render_mdns_selection, snapshot, bytes) != ASCIICHAT_OK)
      break;
    keyboard_key_t key = ui_input_wait_key(UI_SCREEN_MDNS, 100);
    if (ui_input_expired(deadline)) {
      timed_out = true;
      break;
    }
    if (key == KEY_ESCAPE)
      break;
    if (key == '\r' || key == '\n') {
      if (!length)
        break;
      long value = strtol(snapshot->input, NULL, 10);
      if (value >= 1 && value <= count) {
        selection = (int)value - 1;
        break;
      }
      length = 0;
      snapshot->input[0] = '\0';
    } else if ((key == 8 || key == 127) && length) {
      snapshot->input[--length] = '\0';
    } else if (key >= '0' && key <= '9' && length < sizeof(snapshot->input) - 1) {
      snapshot->input[length++] = (char)key;
      snapshot->input[length] = '\0';
    }
  }
  ui_controller_remove(UI_SCREEN_MDNS);
  log_set_terminal_output(logging);
  SAFE_FREE(snapshot);
  if (timed_out)
    ui_input_timeout_report(30u,
                            "server selection cancelled; specify a server address for unattended use");
  return selection;
}

/**
 * @brief Get best address for a server
 */
const char *ui_mdns_get_best_address(const ui_mdns_server_t *server) {
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
