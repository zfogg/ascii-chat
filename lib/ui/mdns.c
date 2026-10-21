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
#include <ascii-chat/ui/invitation.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/time.h>
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
 * - Supports arrows/j/k, Enter, numeric entry, refresh and cancellation
 * - Keeps the selected row visible in a terminal-sized viewport
 *
 * @param servers Array of discovered servers
 * @param count Number of servers
 * @return 0-based index of selected server, or -1 to cancel
 */
typedef struct {
  int count;
  int selected;
  char input[32];
  ui_mdns_server_t servers[];
} mdns_snapshot_t;

static void render_mdns_selection(terminal_size_t size, const void *data) {
  const mdns_snapshot_t *snapshot = data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  frame_buffer_printf(buffer, "\033[0m\033[H\033[2J");
  bool compact = size.cols < 52 || size.rows < 10;
  int logo_rows = size.rows >= 6 ? (compact ? 1 : 4) : 0;
  int logo_height = logo_rows ? logo_rows + 1 : 0;
  int available = size.rows - logo_height - 3;
  if (available < 1 || size.cols < 2) {
    frame_buffer_flush(buffer);
    frame_buffer_destroy(buffer);
    return;
  }
  int shown = snapshot->count < available ? snapshot->count : available;
  if (!shown)
    shown = 1;
  int height = logo_height + shown + 2;
  int top = (size.rows - height) / 2 + 1;
  frame_buffer_printf(buffer, "\033[%d;1H", top);
  if (logo_rows) {
    const animation_sample_t *sample = ui_controller_animation_sample();
    int tick = sample ? (int)(sample->seconds * 60) : (int)((time_get_ns() / NS_PER_MS_INT / 16) % 100000);
    bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(STDERR_FILENO);
    invitation_render_logo(buffer, size.cols - 1, tick, colors, compact);
    frame_buffer_printf(buffer, "\r\n");
  }
  char heading[128];
  truncate_with_ellipsis("ascii-chat | LAN servers", heading, sizeof(heading), size.cols - 1);
  frame_buffer_render_centered(buffer, heading, size.cols - 1);
  frame_buffer_printf(buffer, "\r");
  if (snapshot->count == 0) {
    truncate_with_ellipsis("No servers found. Press r to refresh.", heading, sizeof(heading), size.cols - 1);
    frame_buffer_render_centered(buffer, heading, size.cols - 1);
    frame_buffer_printf(buffer, "\r");
  }
  int first = snapshot->selected >= available ? snapshot->selected - available + 1 : 0;
  for (int i = first; i < snapshot->count && i < first + available; ++i) {
    char line[512], clipped[512];
    const char *address = ui_mdns_get_best_address(&snapshot->servers[i]);
    snprintf(line, sizeof(line), "%s [%d] %s (%s%s%s:%u)", i == snapshot->selected ? ">" : " ", i + 1,
             snapshot->servers[i].name, strchr(address, ':') ? "[" : "", address, strchr(address, ':') ? "]" : "",
             snapshot->servers[i].port);
    truncate_with_ellipsis(line, clipped, sizeof(clipped), size.cols - 1);
    frame_buffer_render_centered(buffer, clipped, size.cols - 1);
    frame_buffer_printf(buffer, "\r");
  }
  char footer[128], clipped[128];
  snprintf(footer, sizeof(footer), "Arrows/j/k: move | Enter: select | r: refresh | Esc/q: cancel | 30s | %s",
           snapshot->input);
  truncate_with_ellipsis(footer, clipped, sizeof(clipped), size.cols - 1);
  frame_buffer_render_centered(buffer, clipped, size.cols - 1);
  frame_buffer_printf(buffer, "\033[K");
  frame_buffer_flush(buffer);
  frame_buffer_destroy(buffer);
}

int ui_mdns_select(const ui_mdns_server_t *servers, int count) {
  if (!servers || count <= 0)
    return -1;
  return ui_mdns_select_until(servers, count, ui_input_deadline(30));
}

int ui_mdns_select_until(const ui_mdns_server_t *servers, int count, uint64_t deadline) {
  if (count < 0 || (!servers && count > 0))
    return -1;
  if (!terminal_can_prompt_user())
    return ui_mdns_prompt_selection(servers, count);
  if (keyboard_init() != ASCIICHAT_OK)
    return -1;
  size_t bytes = sizeof(mdns_snapshot_t) + (size_t)count * sizeof(*servers);
  mdns_snapshot_t *snapshot = SAFE_CALLOC(1, bytes, mdns_snapshot_t *);
  if (!snapshot)
    return -1;
  snapshot->count = count;
  if (count > 0)
    memcpy(snapshot->servers, servers, (size_t)count * sizeof(*servers));
  bool logging = log_get_terminal_output();
  log_set_terminal_output(false);
  int selection = -1;
  size_t length = 0;
  bool timed_out = false;
  while (!shutdown_is_requested()) {
    if (ui_controller_submit(UI_SCREEN_MDNS, STDERR_FILENO, (terminal_size_t){.cols = 2, .rows = 4},
                             render_mdns_selection, snapshot, bytes) != ASCIICHAT_OK)
      break;
    keyboard_key_t key = ui_input_wait_key(UI_SCREEN_MDNS, 100);
    if (ui_input_expired(deadline)) {
      timed_out = true;
      break;
    }
    if (key == KEY_ESCAPE || key == 'q' || key == 3)
      break;
    if (key == 'r') {
      selection = UI_MDNS_REFRESH;
      break;
    }
    if (count && (key == KEY_UP || key == 'k' || key == KEY_DOWN || key == 'j')) {
      int delta = (key == KEY_UP || key == 'k') ? -1 : 1;
      snapshot->selected = (snapshot->selected + delta + count) % count;
      length = 0;
      snapshot->input[0] = '\0';
      continue;
    }
    if (key == '\r' || key == '\n') {
      if (!count)
        continue;
      if (!length) {
        selection = snapshot->selected;
        break;
      }
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
    ui_input_timeout_report(30u, "server selection cancelled; specify a server address for unattended use");
  return selection;
}

/**
 * @brief Get best address for a server
 */
const char *ui_mdns_get_best_address(const ui_mdns_server_t *server) {
  if (!server) {
    return "";
  }

  // The service instance name is a display label, not a resolvable hostname.
  if (server->ipv4[0] != '\0') {
    return server->ipv4;
  }
  if (server->ipv6[0] != '\0') {
    return server->ipv6;
  }

  return server->address; // Fallback to address field
}
