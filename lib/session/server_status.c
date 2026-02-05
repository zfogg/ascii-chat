/**
 * @file lib/session/server_status.c
 * @brief Server status screen display with live log feed at FPS rate
 */

#include <ascii-chat/session/server_status.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/colorize.h>
#include <ascii-chat/common.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

// ============================================================================
// Log Buffer for Status Screen
// ============================================================================

#define STATUS_LOG_BUFFER_SIZE 100
#define STATUS_LOG_LINE_MAX 512

typedef struct {
  char message[STATUS_LOG_LINE_MAX];
  uint64_t sequence;
} status_log_entry_t;

typedef struct {
  status_log_entry_t entries[STATUS_LOG_BUFFER_SIZE];
  _Atomic size_t write_pos;
  _Atomic uint64_t sequence;
  mutex_t mutex;
} status_log_buffer_t;

static status_log_buffer_t *g_status_log_buffer = NULL;

void server_status_log_init(void) {
  if (g_status_log_buffer) {
    return;
  }
  g_status_log_buffer = SAFE_CALLOC(1, sizeof(status_log_buffer_t), status_log_buffer_t *);
  if (!g_status_log_buffer) {
    return;
  }
  atomic_init(&g_status_log_buffer->write_pos, 0);
  atomic_init(&g_status_log_buffer->sequence, 0);
  mutex_init(&g_status_log_buffer->mutex);
}

void server_status_log_cleanup(void) {
  if (!g_status_log_buffer) {
    return;
  }
  mutex_destroy(&g_status_log_buffer->mutex);
  SAFE_FREE(g_status_log_buffer);
}

void server_status_log_append(const char *message) {
  if (!g_status_log_buffer || !message) {
    return;
  }

  mutex_lock(&g_status_log_buffer->mutex);

  size_t pos = atomic_load(&g_status_log_buffer->write_pos);
  uint64_t seq = atomic_fetch_add(&g_status_log_buffer->sequence, 1);

  SAFE_STRNCPY(g_status_log_buffer->entries[pos].message, message, STATUS_LOG_LINE_MAX);
  g_status_log_buffer->entries[pos].sequence = seq;

  atomic_store(&g_status_log_buffer->write_pos, (pos + 1) % STATUS_LOG_BUFFER_SIZE);

  mutex_unlock(&g_status_log_buffer->mutex);
}

static size_t server_status_log_get_recent(status_log_entry_t *out_entries, size_t max_count) {
  if (!g_status_log_buffer || !out_entries || max_count == 0) {
    return 0;
  }

  mutex_lock(&g_status_log_buffer->mutex);

  size_t write_pos = atomic_load(&g_status_log_buffer->write_pos);
  uint64_t total_entries = atomic_load(&g_status_log_buffer->sequence);

  size_t start_pos = write_pos;
  size_t entries_to_check = STATUS_LOG_BUFFER_SIZE;

  if (total_entries < STATUS_LOG_BUFFER_SIZE) {
    start_pos = 0;
    entries_to_check = write_pos;
  }

  size_t count = 0;
  for (size_t i = 0; i < entries_to_check && count < max_count; i++) {
    size_t idx = (start_pos + i) % STATUS_LOG_BUFFER_SIZE;
    if (g_status_log_buffer->entries[idx].sequence > 0) {
      memcpy(&out_entries[count], &g_status_log_buffer->entries[idx], sizeof(status_log_entry_t));
      count++;
    }
  }

  mutex_unlock(&g_status_log_buffer->mutex);
  return count;
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

void server_status_display(const server_status_t *status) {
  if (!status) {
    return;
  }

  // Get terminal dimensions
  terminal_size_t term_size;
  if (terminal_get_size(&term_size) != ASCIICHAT_OK) {
    term_size.cols = 80;
    term_size.rows = 24;
  }

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / 3600;
  int uptime_mins = (uptime_secs % 3600) / 60;
  int uptime_secs_rem = uptime_secs % 60;

  // Clear screen and move to home position
  printf("\033[H\033[2J");

  // ========================================================================
  // STATUS BOX (compact, top of screen)
  // ========================================================================
  printf("\033[1;36m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n");
  printf("\033[1;36m  ascii-chat %s Status\033[0m", status->mode_name);
  printf("  👥 \033[1;33m%zu\033[0m", status->connected_count);
  printf("  ⏱️ %dh %dm %ds\n", uptime_hours, uptime_mins, uptime_secs_rem);

  if (status->session_string[0] != '\0') {
    printf("🔗 %s", status->session_string);
    if (status->session_is_mdns_only) {
      printf(" \033[2m(LAN)\033[0m");
    } else {
      printf(" \033[2m(Internet)\033[0m");
    }
  }
  if (status->ipv4_bound) {
    printf("  📍 %s", status->ipv4_address);
  }
  if (status->ipv6_bound) {
    printf("  📍 [%s]:%u", status->ipv6_address, status->port);
  }
  printf("\n");

  printf("\033[1;36m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n");

  // ========================================================================
  // LIVE LOG FEED (bottom-anchored, no scrolling)
  // ========================================================================
  printf("\033[1;37mLive Logs:\033[0m\n");

  // Get recent log entries
  status_log_entry_t logs[STATUS_LOG_BUFFER_SIZE];
  size_t log_count = server_status_log_get_recent(logs, STATUS_LOG_BUFFER_SIZE);

  // Calculate available lines for logs (leave room for status + borders + "Live Logs:" header)
  // Status: 3 lines (borders + info), "Live Logs:" header: 1 line, bottom margin: 1 line
  int logs_available_lines = term_size.rows - 5;
  if (logs_available_lines < 5) {
    logs_available_lines = 5; // Minimum
  }

  // Display ONLY the most recent N logs that fit on screen (tail behavior)
  // This prevents scrolling - new logs push old ones off the top
  size_t start_idx = (log_count > (size_t)logs_available_lines) ? log_count - logs_available_lines : 0;

  for (size_t i = start_idx; i < log_count; i++) {
    // Use existing logging colorization system
    const char *colorized = colorize_log_message(logs[i].message);
    printf("%s", colorized);
    if (colorized[strlen(colorized) - 1] != '\n') {
      printf("\n");
    }
  }

  // Fill remaining lines with blank space to prevent scrolling
  size_t logs_displayed = log_count - start_idx;
  for (int i = logs_displayed; i < logs_available_lines; i++) {
    printf("\n");
  }

  fflush(stdout);
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
  uint64_t frame_interval_us = 1000000ULL / fps;

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
