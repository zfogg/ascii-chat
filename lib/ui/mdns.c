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
#include <ascii-chat/atomic.h>
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

#define MDNS_LIVE_CAPACITY 128
#define MDNS_LIST_MAX_ROWS 12
typedef struct {
  mutex_t mutex;
  atomic_t stop;
  asciichat_thread_t thread;
  bool running;
  unsigned generation;
  int count;
  ui_mdns_server_t servers[MDNS_LIVE_CAPACITY];
} mdns_refresh_t;

static int compare_mdns_servers(const void *left, const void *right) {
  const ui_mdns_server_t *a = left, *b = right;
  int address = strcmp(ui_mdns_get_best_address(a), ui_mdns_get_best_address(b));
  return address ? address : (int)a->port - (int)b->port;
}

static void *refresh_mdns_servers(void *data) {
  mdns_refresh_t *refresh = data;
  ui_mdns_config_t config = {.timeout_ms = 500, .max_servers = MDNS_LIVE_CAPACITY, .quiet = true};
  while (!atomic_load_bool(&refresh->stop)) {
    int count = 0;
    ui_mdns_server_t *servers = ui_mdns_query(&config, &count);
    if (count > 1)
      qsort(servers, (size_t)count, sizeof(*servers), compare_mdns_servers);
    if (!atomic_load_bool(&refresh->stop)) {
      mutex_lock(&refresh->mutex);
      bool changed = count != refresh->count ||
                     (count && memcmp(refresh->servers, servers, (size_t)count * sizeof(*servers)) != 0);
      refresh->count = count;
      if (count)
        memcpy(refresh->servers, servers, (size_t)count * sizeof(*servers));
      if (changed)
        ++refresh->generation;
      mutex_unlock(&refresh->mutex);
    }
    ui_mdns_free_results(servers);
    for (int i = 0; i < 10 && !atomic_load_bool(&refresh->stop); ++i)
      platform_sleep_ms(100);
  }
  return NULL;
}

static void mdns_divider(frame_buffer_t *buffer, int columns, int width, bool unicode, bool colors) {
  frame_buffer_printf(buffer, "%*s", (columns - width) / 2, "");
  frame_buffer_render_divider(buffer, width, unicode, colors);
  frame_buffer_printf(buffer, "\r");
}

static void mdns_footer(frame_buffer_t *buffer, int columns, const char *input, bool colors) {
  const char *parts[] = {"Arrows", "/",           "j",   "/", "k", ": move | ",
                         "Enter",  ": select | ", "Esc", "/", "q", ": cancel | Auto-refresh | 30s",
                         input};
  bool keys[] = {true, false, true, false, true, false, true, false, true, false, true, false, false};
  int length = 0;
  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
    length += (int)strlen(parts[i]);
  int width = length < columns - 1 ? length : columns - 1;
  frame_buffer_printf(buffer, "%*s", (columns - 1 - width) / 2, "");
  int used = 0;
  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]) && used < width; ++i) {
    int shown = (int)strlen(parts[i]);
    if (shown > width - used)
      shown = width - used;
    frame_buffer_printf(buffer, "%s%.*s", colors ? log_level_color(keys[i] ? LOG_COLOR_DEV : LOG_COLOR_GREY) : "",
                        shown, parts[i]);
    used += shown;
  }
  frame_buffer_printf(buffer, "%s", colors ? "\033[0m" : "");
}

static void render_mdns_selection(terminal_size_t size, const void *data) {
  const mdns_snapshot_t *snapshot = data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  frame_buffer_printf(buffer, "\033[0m\033[H\033[2J");
  bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(STDERR_FILENO);
  utf8_setting_t utf8 = GET_OPTION(force_utf8);
  bool unicode = utf8 == UTF8_SETTING_TRUE || (utf8 == UTF8_SETTING_AUTO && terminal_supports_utf8());
  int panel_width = size.cols - 1 < 80 ? size.cols - 1 : 80;
  bool show_title = size.rows >= 5;
  int chrome = show_title ? 4 : 3;
  bool compact = size.cols < 52 || size.rows < 10;
  int logo_rows = size.rows >= 9 ? (compact ? 1 : 4) : 0;
  int logo_height = logo_rows ? logo_rows + 1 : 0;
  int available = size.rows - logo_height - chrome;
  if (available < 1 || size.cols < 2) {
    frame_buffer_flush(buffer);
    frame_buffer_destroy(buffer);
    return;
  }
  if (available > MDNS_LIST_MAX_ROWS)
    available = MDNS_LIST_MAX_ROWS;
  int shown = snapshot->count < available ? snapshot->count : available;
  if (!shown)
    shown = 1;
  int height = logo_height + shown + chrome;
  int top = (size.rows - height) / 2 + 1;
  frame_buffer_printf(buffer, "\033[%d;1H", top);
  if (logo_rows) {
    const animation_sample_t *sample = ui_controller_animation_sample();
    int tick = sample ? (int)(sample->seconds * 60) : (int)((time_get_ns() / NS_PER_MS_INT / 16) % 100000);
    invitation_render_logo(buffer, size.cols - 1, tick, colors, compact);
    frame_buffer_printf(buffer, "\r\n");
  }
  char heading[128];
  if (show_title) {
    truncate_with_ellipsis("LAN servers", heading, sizeof(heading), size.cols - 1);
    frame_buffer_render_centered(buffer, heading, size.cols - 1);
    frame_buffer_printf(buffer, "\r");
  }
  mdns_divider(buffer, size.cols - 1, panel_width, unicode, colors);
  if (snapshot->count == 0) {
    truncate_with_ellipsis("Searching for LAN servers...", heading, sizeof(heading), size.cols - 1);
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
    if (colors && i == snapshot->selected)
      frame_buffer_printf(buffer, "%s", log_level_color(LOG_COLOR_DEBUG));
    frame_buffer_render_centered(buffer, clipped, size.cols - 1);
    frame_buffer_printf(buffer, "\033[0m\r");
  }
  mdns_divider(buffer, size.cols - 1, panel_width, unicode, colors);
  mdns_footer(buffer, size.cols, snapshot->input, colors);
  frame_buffer_flush(buffer);
  frame_buffer_destroy(buffer);
}

int ui_mdns_select(const ui_mdns_server_t *servers, int count) {
  if (!servers || count <= 0)
    return -1;
  return ui_mdns_select_until(servers, count, ui_input_deadline(30));
}

static int select_mdns_until(const ui_mdns_server_t *servers, int count, uint64_t deadline, mdns_refresh_t *refresh,
                             ui_mdns_server_t *chosen) {
  if (count < 0 || (!servers && count > 0))
    return -1;
  if (!terminal_can_prompt_user())
    return ui_mdns_prompt_selection(servers, count);
  if (keyboard_init() != ASCIICHAT_OK)
    return -1;
  size_t bytes = sizeof(mdns_snapshot_t) + (size_t)(refresh ? MDNS_LIVE_CAPACITY : count) * sizeof(*servers);
  mdns_snapshot_t *snapshot = SAFE_CALLOC(1, bytes, mdns_snapshot_t *);
  if (!snapshot)
    return -1;
  snapshot->count = count;
  if (count > 0)
    memcpy(snapshot->servers, servers, (size_t)count * sizeof(*servers));
  bool logging = log_get_terminal_output();
  log_set_terminal_output(false);
  if (refresh) {
    thread_init(&refresh->thread);
    refresh->running = acthread_create(&refresh->thread, "lan_refresh", refresh_mdns_servers, refresh) == 0;
    if (!refresh->running) {
      log_set_terminal_output(logging);
      SAFE_FREE(snapshot);
      return -1;
    }
  }
  unsigned generation = 0;
  int selection = -1;
  size_t length = 0;
  bool timed_out = false;
  while (!shutdown_is_requested()) {
    if (refresh) {
      mutex_lock(&refresh->mutex);
      if (generation != refresh->generation) {
        ui_mdns_server_t previous = {0};
        if (count)
          previous = snapshot->servers[snapshot->selected];
        count = refresh->count;
        snapshot->count = count;
        memcpy(snapshot->servers, refresh->servers, (size_t)count * sizeof(*servers));
        snapshot->selected = 0;
        for (int i = 0; i < count; ++i) {
          if (previous.port == snapshot->servers[i].port &&
              strcmp(ui_mdns_get_best_address(&previous), ui_mdns_get_best_address(&snapshot->servers[i])) == 0) {
            snapshot->selected = i;
            break;
          }
        }
        length = 0;
        snapshot->input[0] = '\0';
        generation = refresh->generation;
      }
      mutex_unlock(&refresh->mutex);
    }
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
  if (chosen && selection >= 0)
    *chosen = snapshot->servers[selection];
  if (refresh && refresh->running) {
    atomic_store_bool(&refresh->stop, true);
    thread_join(&refresh->thread, NULL);
  }
  ui_controller_remove(UI_SCREEN_MDNS);
  log_set_terminal_output(logging);
  SAFE_FREE(snapshot);
  if (timed_out)
    ui_input_timeout_report(30u, "server selection cancelled; specify a server address for unattended use");
  return selection;
}

int ui_mdns_select_until(const ui_mdns_server_t *servers, int count, uint64_t deadline) {
  return select_mdns_until(servers, count, deadline, NULL, NULL);
}

int ui_mdns_select_live(const ui_mdns_server_t *servers, int count, uint64_t deadline, ui_mdns_server_t *chosen) {
  if (!chosen || count < 0 || count > MDNS_LIVE_CAPACITY || (count && !servers))
    return -1;
  mdns_refresh_t refresh = {0};
  refresh.count = count;
  if (count)
    memcpy(refresh.servers, servers, (size_t)count * sizeof(*servers));
  if (mutex_init(&refresh.mutex, "lan_results") != 0)
    return -1;
  int selected = select_mdns_until(servers, count, deadline, &refresh, chosen);
  mutex_destroy(&refresh.mutex);
  return selected < 0 ? -1 : 0;
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
