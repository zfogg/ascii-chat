#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/ui/viewport.h>
#include <ascii-chat/tui/stats.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <ascii-chat/options/options.h>
#include <string.h>

struct stats_peer {
  stats_scope_t *scope;
  stats_sampler_t *sampler;
  char transport[16];
  uint64_t id;
};

static stats_scope_t *g_scope;
static stats_sampler_t *g_sampler;
static mutex_t g_peers_mutex, g_provider_mutex;
static stats_peer_t *g_peers[64];
static uint64_t g_peer_id, g_peer_count;
static stats_provider_fn g_provider;
static void *g_provider_data;
static char g_mode[32], g_state[64];
static unsigned g_interval;
static atomic_t g_stop, g_active;
static asciichat_thread_t g_worker;
static bool g_started, g_keyboard;
static int g_fd;
static atomic_t g_media_width, g_media_height, g_media_position, g_media_duration;
void stats_runtime_media(unsigned width, unsigned height, double position, double duration) {
  if (!g_scope)
    return;
  atomic_store_u64_impl(&g_media_width, width);
  atomic_store_u64_impl(&g_media_height, height);
  atomic_store_u64_impl(&g_media_position,
                        isfinite(position) && position >= 0 && position < 1e12 ? (uint64_t)(position * 1000) + 1 : 0);
  atomic_store_u64_impl(&g_media_duration,
                        isfinite(duration) && duration > 0 && duration < 1e12 ? (uint64_t)(duration * 1000) + 1 : 0);
}

stats_scope_t *stats_runtime_scope(void) {
  return g_scope;
}
stats_scope_t *stats_runtime_peer_scope(stats_peer_t *peer) {
  return peer ? peer->scope : NULL;
}
bool stats_runtime_active(void) {
  return atomic_load_bool_impl(&g_active) &&
         (ui_controller_interactive_screen() == UI_SCREEN_STATS ||
          (ui_controller_interactive_screen() == UI_SCREEN_HELP && keyboard_help_parent_screen() == UI_SCREEN_STATS));
}

void stats_runtime_toggle(void) {
  if (g_started && g_keyboard) {
    bool active = !stats_runtime_active();
    atomic_store_bool_impl(&g_active, active);
    ui_controller_select_interactive(active ? UI_SCREEN_STATS : -1);
    log_debug("Statistics TUI requested: %s", active ? "open" : "closed");
  }
}

static void stats_view_vadd(stats_view_t *view, bool heading, const char *format, va_list args) {
  if (view->count >= STATS_VIEW_LINES)
    return;
  vsnprintf(view->lines[view->count], sizeof(view->lines[0]), format, args);
  // Provider labels must never inject terminal controls.
  for (char *p = view->lines[view->count]; *p; ++p)
    if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
      *p = '?';
  view->headings[view->count++] = heading;
}

void stats_view_add(stats_view_t *view, const char *format, ...) {
  va_list args;
  va_start(args, format);
  stats_view_vadd(view, false, format, args);
  va_end(args);
}

void stats_view_add_header(stats_view_t *view, const char *format, ...) {
  va_list args;
  va_start(args, format);
  stats_view_vadd(view, true, format, args);
  va_end(args);
}

void stats_runtime_set_provider(stats_provider_fn provider, void *data) {
  if (!g_scope)
    return;
  mutex_lock(&g_provider_mutex);
  g_provider = provider;
  g_provider_data = data;
  mutex_unlock(&g_provider_mutex);
}

void stats_runtime_connection_state(const char *state) {
  if (!g_scope)
    return;
  mutex_lock(&g_provider_mutex);
  snprintf(g_state, sizeof(g_state), "%s", state);
  mutex_unlock(&g_provider_mutex);
}

static stats_capabilities_t network_capabilities(void) {
  stats_capabilities_t caps = {0};
  for (int i = STATS_COUNTER_PACKETS_SENT; i <= STATS_COUNTER_RECEIVE_ERRORS; ++i)
    caps.counters[i] = true;
  caps.counters[STATS_COUNTER_FRAMES_SENT] = true;
  caps.counters[STATS_COUNTER_FRAMES_RECEIVED] = true;
  return caps;
}

stats_peer_t *stats_runtime_peer_open(const char *transport) {
  if (!g_scope)
    return NULL;
  stats_peer_t *peer = SAFE_CALLOC(1, sizeof(*peer), stats_peer_t *);
  if (!peer)
    return NULL;
  stats_capabilities_t caps = network_capabilities();
  stats_sampler_config_t config = {NS_PER_SEC_INT, 250 * NS_PER_MS_INT};
  if (stats_scope_create(&caps, &peer->scope) != ASCIICHAT_OK ||
      stats_sampler_create(&config, &peer->sampler) != ASCIICHAT_OK) {
    stats_scope_destroy(peer->scope);
    SAFE_FREE(peer);
    return NULL;
  }
  snprintf(peer->transport, sizeof(peer->transport), "%s", transport);
  mutex_lock(&g_peers_mutex);
  peer->id = ++g_peer_id;
  ++g_peer_count;
  ascii_otel_metric(ASCII_OTEL_METRIC_CONNECTIONS, (double)g_peer_count);
  stats_gauge_set(g_scope, STATS_GAUGE_CONNECTIONS_ACTIVE, g_peer_count);
  for (unsigned i = 0; i < 64; ++i) {
    if (!g_peers[i]) {
      g_peers[i] = peer;
      break;
    }
  }
  mutex_unlock(&g_peers_mutex);
  return peer;
}

void stats_runtime_peer_close(stats_peer_t *peer) {
  if (!peer)
    return;
  mutex_lock(&g_peers_mutex);
  for (unsigned i = 0; i < 64; ++i)
    if (g_peers[i] == peer)
      g_peers[i] = NULL;
  --g_peer_count;
  ascii_otel_metric(ASCII_OTEL_METRIC_CONNECTIONS, (double)g_peer_count);
  stats_gauge_set(g_scope, STATS_GAUGE_CONNECTIONS_ACTIVE, g_peer_count);
  mutex_unlock(&g_peers_mutex);
  stats_sampler_destroy(peer->sampler);
  stats_scope_destroy(peer->scope);
  SAFE_FREE(peer);
}

void stats_runtime_packet(stats_peer_t *peer, unsigned type, size_t payload_bytes, bool sent, bool success) {
  if (success) {
    ascii_otel_metric(sent ? ASCII_OTEL_METRIC_NET_TX : ASCII_OTEL_METRIC_NET_RX,
                      (double)(sizeof(packet_header_t) + payload_bytes));
    ascii_otel_metric(sent ? ASCII_OTEL_METRIC_NET_TX_SIZE : ASCII_OTEL_METRIC_NET_RX_SIZE, (double)payload_bytes);
  } else
    ascii_otel_metric(sent ? ASCII_OTEL_METRIC_NET_TX_ERRORS : ASCII_OTEL_METRIC_NET_RX_ERRORS, 1);
  if (sent && type == PACKET_TYPE_ERROR_MESSAGE)
    stats_counter_add(g_scope, STATS_COUNTER_REQUEST_FAILURES, 1);
  stats_scope_t *scopes[] = {g_scope, peer ? peer->scope : NULL};
  for (unsigned i = 0; i < 2; ++i) {
    if (!success) {
      stats_counter_add(scopes[i], sent ? STATS_COUNTER_SEND_ERRORS : STATS_COUNTER_RECEIVE_ERRORS, 1);
      continue;
    }
    stats_counter_add(scopes[i], sent ? STATS_COUNTER_PACKETS_SENT : STATS_COUNTER_PACKETS_RECEIVED, 1);
    stats_counter_add(scopes[i], sent ? STATS_COUNTER_BYTES_SENT : STATS_COUNTER_BYTES_RECEIVED,
                      sizeof(packet_header_t) + payload_bytes);
    if (type == PACKET_TYPE_ASCII_FRAME || type == PACKET_TYPE_IMAGE_FRAME || type == PACKET_TYPE_IMAGE_FRAME_H265 ||
        type == PACKET_TYPE_IMAGE_FRAME_H264)
      stats_counter_add(scopes[i], sent ? STATS_COUNTER_FRAMES_SENT : STATS_COUNTER_FRAMES_RECEIVED, 1);
  }
}

static void build_view(stats_view_t *view, stats_snapshot_t *snapshot, stats_rates_t *rates) {
  snprintf(view->mode, sizeof(view->mode), "%s", g_mode);
  view->sampled_ns = snapshot->sampled_ns;
  if (!view->page) {
    stats_view_add(view, "Uptime %.1fs", (double)(snapshot->sampled_ns - snapshot->started_ns) / 1e9);
    stats_view_add_header(view, "%-28s %14s %14s", "PIPELINE / NETWORK", "TOTAL", "PER SECOND");
    for (int i = 0; i < STATS_COUNTER_COUNT; ++i) {
      if (!snapshot->capabilities.counters[i])
        continue;
      const stats_descriptor_t *descriptor = stats_counter_descriptor(i);
      if (rates->ready)
        stats_view_add(view, "%-28s %14llu %14.1f", descriptor->name, (unsigned long long)snapshot->counters[i],
                       rates->counters_per_second[i]);
      else
        stats_view_add(view, "%-28s %14llu %14s", descriptor->name, (unsigned long long)snapshot->counters[i],
                       "warming up");
    }
    stats_view_add(view, "");
    stats_view_add_header(view, "%-28s %14s %14s", "TIMINGS", "MEAN/LAST ms", "LIFETIME MAX");
    for (int i = 0; i < STATS_DURATION_COUNT; ++i) {
      if (!snapshot->capabilities.durations[i])
        continue;
      if (i == STATS_DURATION_ASCII_CONVERT && strcmp(g_mode, "client") == 0 && !snapshot->durations[i].observations)
        stats_view_add(view, "%-28s %14s %14s", "ascii_convert", "n/a (server)", "n/a");
      else if (i == STATS_DURATION_CONNECTION_SETUP && snapshot->durations[i].observations)
        stats_view_add(view, "%-28s %14.3f %14.3f", "connection_setup (last)",
                       (double)snapshot->durations[i].last_ns / 1e6, (double)snapshot->durations[i].max_ns / 1e6);
      else if (rates->duration_valid[i])
        stats_view_add(view, "%-28s %14.3f %14.3f", stats_duration_descriptor(i)->name,
                       rates->duration_mean_ns[i] / 1e6, (double)snapshot->durations[i].max_ns / 1e6);
      else if (snapshot->durations[i].observations)
        stats_view_add(view, "%-28s %14s %14.3f", stats_duration_descriptor(i)->name, "idle",
                       (double)snapshot->durations[i].max_ns / 1e6);
      else
        stats_view_add(view, "%-28s %14s %14s", stats_duration_descriptor(i)->name, "not sampled", "not sampled");
    }
    stats_view_add(view, "");
    for (int i = 0; i < STATS_GAUGE_COUNT; ++i)
      if (snapshot->capabilities.gauges[i])
        stats_view_add(view, "%s: %llu", stats_gauge_descriptor(i)->name, (unsigned long long)snapshot->gauges[i]);
    uint64_t source_width = atomic_load_u64_impl(&g_media_width);
    if (source_width) {
      stats_view_add(view, "Source: %llux%llu   Output: %ux%u   Target: %d fps", (unsigned long long)source_width,
                     (unsigned long long)atomic_load_u64_impl(&g_media_height), GET_OPTION(width), GET_OPTION(height),
                     GET_OPTION(fps));
      uint64_t position = atomic_load_u64_impl(&g_media_position), duration = atomic_load_u64_impl(&g_media_duration);
      if (position && duration)
        stats_view_add(view, "Media position: %.1fs / %.1fs", (double)(position - 1) / 1000,
                       (double)(duration - 1) / 1000);
      else
        stats_view_add(view, "Media position: unavailable (live source)");
    }
    if (strcmp(g_mode, "discovery-service") != 0)
      stats_view_add(view, "Audio: %s   Recording: %s", GET_OPTION(audio_enabled) ? "enabled" : "disabled",
                     GET_OPTION(render_file)[0] ? "enabled" : "disabled");
    if (strcmp(g_mode, "mirror") != 0) {
      stats_view_add(view, "Bytes: ACIP header + payload, excluding transport/encryption overhead.");
      stats_view_add(view, "RTT: unavailable (no uniquely matched probe in this protocol).");
    }
  }
  mutex_lock(&g_peers_mutex);
  unsigned peers = 0;
  if (view->page)
    stats_view_add_header(view, "%-5s %-12s %12s %12s %10s %10s", "ID", "TRANSPORT", "TX bytes/s", "RX bytes/s",
                          "TX fps", "RX fps");
  for (unsigned i = 0; i < 64; ++i) {
    stats_peer_t *peer = g_peers[i];
    if (!peer)
      continue;
    peers++;
    stats_snapshot_t ps;
    stats_rates_t pr;
    stats_scope_snapshot(peer->scope, &ps);
    stats_sampler_update(peer->sampler, &ps, &pr);
    if (view->page)
      stats_view_add(
          view, "%-5llu %-12s %12.0f %12.0f %10.1f %10.1f", (unsigned long long)peer->id, peer->transport,
          pr.counters_per_second[STATS_COUNTER_BYTES_SENT], pr.counters_per_second[STATS_COUNTER_BYTES_RECEIVED],
          pr.counters_per_second[STATS_COUNTER_FRAMES_SENT], pr.counters_per_second[STATS_COUNTER_FRAMES_RECEIVED]);
  }
  stats_gauge_set(g_scope, STATS_GAUGE_CONNECTIONS_ACTIVE, g_peer_count);
  mutex_unlock(&g_peers_mutex);
  if (view->page)
    stats_view_add(view, "%u transport handles shown (up to 64); process totals include all peers.", peers);
  mutex_lock(&g_provider_mutex);
  if (g_state[0])
    stats_view_add(view, "State: %s", g_state);
  if (g_provider)
    g_provider(view, g_provider_data);
  mutex_unlock(&g_provider_mutex);
}

static void print_summary(const stats_snapshot_t *snapshot, const stats_rates_t *rates) {
  char output[8192];
  int used = snprintf(output, sizeof(output), "stats mode=%s uptime_s=%.3f", g_mode,
                      (double)(snapshot->sampled_ns - snapshot->started_ns) / 1e9);
  for (int i = 0; i < STATS_COUNTER_COUNT; ++i)
    if (snapshot->capabilities.counters[i])
      used += snprintf(output + used, sizeof(output) - (size_t)used, " %s=%llu %s_per_s=%.3f",
                       stats_counter_descriptor(i)->name, (unsigned long long)snapshot->counters[i],
                       stats_counter_descriptor(i)->name, rates->counters_per_second[i]);
  for (int i = 0; i < STATS_GAUGE_COUNT; ++i)
    if (snapshot->capabilities.gauges[i])
      used += snprintf(output + used, sizeof(output) - (size_t)used, " %s=%llu", stats_gauge_descriptor(i)->name,
                       (unsigned long long)snapshot->gauges[i]);
  for (int i = 0; i < STATS_DURATION_COUNT; ++i) {
    if (!snapshot->capabilities.durations[i])
      continue;
    if (rates->duration_valid[i])
      used += snprintf(output + used, sizeof(output) - (size_t)used, " %s_mean_ms=%.3f",
                       stats_duration_descriptor(i)->name, rates->duration_mean_ns[i] / 1e6);
    else
      used += snprintf(output + used, sizeof(output) - (size_t)used, " %s_mean_ms=unavailable",
                       stats_duration_descriptor(i)->name);
  }
  output[used++] = '\n';
  ui_controller_write(STDOUT_FILENO, output, (size_t)used);
}

static void *stats_worker(void *unused) {
  (void)unused;
  uint64_t next_sample = 0, next_print = time_get_ns() + (uint64_t)g_interval * NS_PER_SEC_INT;
  unsigned page = 0;
  size_t last_count = 0;
  ui_viewport_t viewport = {0};
  bool was_active = false;
  uint64_t next_telemetry = 0;
  while (!atomic_load_bool_impl(&g_stop) && !shutdown_is_requested()) {
    bool active = stats_runtime_active();
    // Own input only while our screen is visible, or while a server has no screen.
    bool server_like = strcmp(g_mode, "server") == 0 || strcmp(g_mode, "discovery-service") == 0;
    if (g_keyboard && (active || (server_like && !ui_controller_screen_live(UI_SCREEN_STATUS)))) {
      keyboard_key_t key = ui_input_read_key(UI_SCREEN_STATS);
      active = stats_runtime_active();
      if (active && key == ']') {
        stats_runtime_toggle();
        active = false;
        ui_controller_toggle_logs();
      } else if (active && (key == '=' || key == KEY_ESCAPE)) {
        atomic_store_bool_impl(&g_active, false);
        active = false;
      } else if (!active && key == '=') {
        stats_runtime_toggle();
        active = stats_runtime_active();
      } else if (key == '\t' || key == KEY_LEFT || key == KEY_RIGHT) {
        page = !page;
        viewport = (ui_viewport_t){0};
      } else {
        terminal_size_t size = ui_controller_size();
        ui_viewport_key(&viewport, last_count, size.rows > 6 ? (size_t)size.rows - 6 : 1, key);
      }
    }
    if (active != was_active)
      log_debug("Statistics TUI visibility: %d (selected=%d)", active, ui_controller_interactive_screen());
    if (!active && was_active) {
      ui_controller_remove(UI_SCREEN_STATS);
      if (ui_controller_state().screen < 0) {
        const char restore[] = "\033[0m\033[2J\033[H\033[?25h";
        ui_controller_write(g_fd, restore, sizeof(restore) - 1);
      }
    }
    uint64_t now = time_get_ns();
    if (now >= next_sample || active != was_active) {
      stats_view_t view = {.page = page};
      stats_snapshot_t snapshot;
      stats_rates_t rates;
      stats_scope_snapshot(g_scope, &snapshot);
      stats_sampler_update(g_sampler, &snapshot, &rates);
      bool print_due = g_interval && now >= next_print;
      bool telemetry_due = ascii_otel_enabled() && now >= next_telemetry;
      if (active || print_due || telemetry_due)
        build_view(&view, &snapshot, &rates);
      if (telemetry_due)
        next_telemetry = now + NS_PER_SEC_INT;
      terminal_size_t size = ui_controller_size();
      ui_viewport_update(&viewport, view.count, size.rows > 6 ? (size_t)size.rows - 6 : 1);
      last_count = view.count;
      view.selected = (unsigned)viewport.selected;
      view.offset = (unsigned)viewport.first_visible;
      if (active)
        ui_controller_submit(UI_SCREEN_STATS, g_fd, tui_stats_minimum(&view), tui_stats_render, &view, sizeof(view));
      if (print_due) {
        print_summary(&snapshot, &rates);
        next_print = now + (uint64_t)g_interval * NS_PER_SEC_INT;
      }
      next_sample = now + 250 * NS_PER_MS_INT;
    }
    was_active = active;
    platform_sleep_ns(10 * NS_PER_MS_INT);
  }
  ui_controller_remove(UI_SCREEN_STATS);
  return NULL;
}

asciichat_error_t stats_runtime_start(const char *mode, unsigned interval_seconds) {
  if (!mode)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing statistics mode");
  if (g_started)
    return ASCIICHAT_OK;
  stats_capabilities_t caps = network_capabilities();
  bool mirror = strcmp(mode, "mirror") == 0, acds = strcmp(mode, "discovery-service") == 0;
  if (mirror)
    memset(&caps, 0, sizeof(caps));
  if (!acds) {
    for (int i = 0; i <= STATS_COUNTER_FRAMES_DROPPED; ++i)
      caps.counters[i] = true;
    caps.counters[STATS_COUNTER_FRAMES_ENQUEUED] = caps.counters[STATS_COUNTER_QUEUE_DROPS] = true;
    caps.counters[STATS_COUNTER_AUDIO_UNDERRUNS] = caps.counters[STATS_COUNTER_AUDIO_OVERRUNS] =
        GET_OPTION(audio_enabled);
    for (int i = 0; i <= STATS_DURATION_TERMINAL_WRITE; ++i)
      caps.durations[i] = true;
    caps.gauges[STATS_GAUGE_AUDIO_BUFFERED_SAMPLES] = GET_OPTION(audio_enabled);
    caps.durations[STATS_DURATION_AUDIO_MIX] = GET_OPTION(audio_enabled) && !mirror;
    if (mirror) {
      caps.counters[STATS_COUNTER_FRAMES_ENCODED] = caps.counters[STATS_COUNTER_FRAMES_DECODED] = false;
      caps.durations[STATS_DURATION_ENCODE] = caps.durations[STATS_DURATION_DECODE] = false;
    }
  } else {
    caps.counters[STATS_COUNTER_FRAMES_SENT] = caps.counters[STATS_COUNTER_FRAMES_RECEIVED] = false;
    for (int i = STATS_COUNTER_SESSION_CREATES; i <= STATS_COUNTER_HOST_MIGRATIONS; ++i)
      caps.counters[i] = true;
    caps.counters[STATS_COUNTER_MIGRATION_FAILURES] = true;
    caps.durations[STATS_DURATION_REQUEST] = true;
    caps.gauges[STATS_GAUGE_SESSIONS_ACTIVE] = caps.gauges[STATS_GAUGE_PARTICIPANTS_ACTIVE] = true;
  }
  if (strcmp(mode, "discovery") == 0)
    caps.counters[STATS_COUNTER_MIGRATION_FAILURES] = true;
  caps.gauges[STATS_GAUGE_VIDEO_QUEUE_DEPTH] = strcmp(mode, "server") == 0;
  if (GET_OPTION(render_file)[0]) {
    caps.counters[STATS_COUNTER_RECORDING_FRAMES] = true;
    caps.durations[STATS_DURATION_RECORDING] = true;
  }
  if (!mirror) {
    caps.counters[STATS_COUNTER_RECONNECTS] = !acds;
    caps.counters[STATS_COUNTER_HOST_MIGRATIONS] = true;
    caps.gauges[STATS_GAUGE_CONNECTIONS_ACTIVE] = true;
    caps.durations[STATS_DURATION_CONNECTION_SETUP] = !acds;
  }
  asciichat_error_t result = stats_scope_create(&caps, &g_scope);
  if (result != ASCIICHAT_OK)
    return result;
  ascii_otel_stats_attach(g_scope);
  stats_sampler_config_t config = {NS_PER_SEC_INT, 250 * NS_PER_MS_INT};
  result = stats_sampler_create(&config, &g_sampler);
  if (result != ASCIICHAT_OK)
    goto fail_scope;
  if (mutex_init(&g_peers_mutex, "stats_peers") != 0) {
    result = SET_ERRNO(ERROR_THREAD, "Cannot initialize stats peer mutex");
    goto fail_sampler;
  }
  if (mutex_init(&g_provider_mutex, "stats_provider") != 0) {
    result = SET_ERRNO(ERROR_THREAD, "Cannot initialize stats provider mutex");
    goto fail_peers;
  }
  g_peer_count = g_peer_id = 0;
  memset(g_peers, 0, sizeof(g_peers));
  g_state[0] = 0;
  stats_runtime_media(0, 0, -1, -1);
  snprintf(g_mode, sizeof(g_mode), "%s", mode);
  g_interval = interval_seconds;
  g_fd = platform_isatty(STDOUT_FILENO) ? STDOUT_FILENO : STDERR_FILENO;
  g_keyboard = platform_isatty(STDIN_FILENO) && platform_isatty(g_fd) && keyboard_init() == ASCIICHAT_OK;
  atomic_store_bool_impl(&g_stop, false);
  atomic_store_bool_impl(&g_active, false);
  g_started = true;
  bool server_like = strcmp(mode, "server") == 0 || strcmp(mode, "discovery-service") == 0;
  if (g_keyboard && server_like && ui_controller_start_terminal(g_fd) != ASCIICHAT_OK)
    LOG_ERRNO_IF_SET("Cannot start terminal presentation");
  if (acthread_create(&g_worker, "stats", stats_worker, NULL) == 0)
    return ASCIICHAT_OK;
  result = SET_ERRNO(ERROR_THREAD, "Cannot start stats worker");
  g_started = false;
  if (g_keyboard)
    keyboard_destroy();
  mutex_destroy(&g_provider_mutex);
fail_peers:
  mutex_destroy(&g_peers_mutex);
fail_sampler:
  stats_sampler_destroy(g_sampler);
  g_sampler = NULL;
fail_scope:
  stats_scope_destroy(g_scope);
  g_scope = NULL;
  return result;
}

void stats_runtime_stop(void) {
  if (!g_started)
    return;
  atomic_store_bool_impl(&g_stop, true);
  thread_join(&g_worker, NULL);
  ui_controller_shutdown();
  if (g_keyboard)
    keyboard_destroy();
  g_provider = NULL;
  g_provider_data = NULL;
  stats_sampler_destroy(g_sampler);
  stats_scope_destroy(g_scope);
  g_sampler = NULL;
  g_scope = NULL;
  mutex_destroy(&g_provider_mutex);
  mutex_destroy(&g_peers_mutex);
  g_started = false;
}
