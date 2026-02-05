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

// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_last_term_size_check_us = 0;
#define TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second

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

void server_status_log_clear(void) {
  if (!g_status_log_buffer) {
    return;
  }
  mutex_lock(&g_status_log_buffer->mutex);
  // Clear all log entries by resetting write position and sequence
  atomic_store(&g_status_log_buffer->write_pos, 0);
  atomic_store(&g_status_log_buffer->sequence, 0);
  // Clear all entries
  for (size_t i = 0; i < STATUS_LOG_BUFFER_SIZE; i++) {
    g_status_log_buffer->entries[i].message[0] = '\0';
    g_status_log_buffer->entries[i].sequence = 0;
  }
  mutex_unlock(&g_status_log_buffer->mutex);
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

  // Get terminal dimensions (cached to avoid flooding logs with errors)
  uint64_t now_us = platform_get_monotonic_time_us();
  if (now_us - g_last_term_size_check_us > TERM_SIZE_CHECK_INTERVAL_US) {
    terminal_size_t temp_size;
    if (terminal_get_size(&temp_size) == ASCIICHAT_OK) {
      g_cached_term_size = temp_size;
    }
    g_last_term_size_check_us = now_us;
  }
  terminal_size_t term_size = g_cached_term_size;

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / 3600;
  int uptime_mins = (uptime_secs % 3600) / 60;
  int uptime_secs_rem = uptime_secs % 60;

  // Clear screen and move to home position on BOTH stdout and stderr
  // (logs may have been written to either stream before status screen started)
  fprintf(stderr, "\033[H\033[2J");
  fflush(stderr);
  printf("\033[H\033[2J");

  // ========================================================================
  // STATUS BOX - EXACTLY 4 LINES (fixed height, never scrolls terminal)
  // ========================================================================
  // Line 1: Top border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");

  // Line 2: Title + Stats (truncate if too long to fit on one line)
  char status_line[512];
  snprintf(status_line, sizeof(status_line), "  ascii-chat %s | 👥 %zu | ⏱️ %dh %dm %ds", status->mode_name,
           status->connected_count, uptime_hours, uptime_mins, uptime_secs_rem);
  if (term_size.cols > 0 && (int)strlen(status_line) >= term_size.cols) {
    status_line[term_size.cols - 1] = '\0'; // Truncate
  }
  printf("\033[1;36m%s\033[0m\n", status_line);

  // Line 3: Session + Addresses (truncate if too long)
  char addr_line[512];
  int pos = 0;
  if (status->session_string[0] != '\0') {
    pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, "  🔗 %s", status->session_string);
  }
  if (status->ipv4_bound && pos < (int)sizeof(addr_line) - 30) {
    pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, " | %s", status->ipv4_address);
  }
  if (status->ipv6_bound && pos < (int)sizeof(addr_line) - 30) {
    snprintf(addr_line + pos, sizeof(addr_line) - pos, " | %s", status->ipv6_address);
  }
  if (term_size.cols > 0 && (int)strlen(addr_line) >= term_size.cols) {
    addr_line[term_size.cols - 1] = '\0'; // Truncate
  }
  printf("%s\n", addr_line);

  // Line 4: Bottom border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");

  // ========================================================================
  // LIVE LOG FEED - Fills remaining screen (never causes scroll)
  // ========================================================================
  // Calculate EXACTLY how many lines we have for logs
  // Status took exactly 4 lines above
  int logs_available_lines = term_size.rows - 4;
  if (logs_available_lines < 1) {
    logs_available_lines = 1; // At least show something
  }

  // Get recent log entries
  status_log_entry_t logs[STATUS_LOG_BUFFER_SIZE];
  size_t log_count = server_status_log_get_recent(logs, STATUS_LOG_BUFFER_SIZE);

  // Calculate actual display lines needed for each log (accounting for wrapping and newlines)
  // Work backwards from most recent logs until we fill available lines
  int lines_used = 0;
  size_t start_idx = log_count; // Start past the end, will work backwards

  for (size_t i = log_count; i > 0; i--) {
    size_t idx = i - 1;
    const char *msg = logs[idx].message;

    // Count display lines for this message (newlines + wrapping)
    int msg_lines = 0;
    int current_line_len = 0;
    for (const char *p = msg; *p; p++) {
      if (*p == '\n') {
        msg_lines++;
        current_line_len = 0;
      } else {
        current_line_len++;
        if (term_size.cols > 0 && current_line_len >= term_size.cols) {
          msg_lines++;
          current_line_len = 0;
        }
      }
    }
    // Count final line if message doesn't end with newline
    if (current_line_len > 0 || (msg[0] != '\0' && msg[strlen(msg) - 1] == '\n')) {
      msg_lines++;
    }

    // Check if this log fits in remaining space
    if (lines_used + msg_lines <= logs_available_lines) {
      lines_used += msg_lines;
      start_idx = idx;
    } else {
      break; // No more room
    }
  }

  // Display logs that fit (starting from start_idx)
  // Logs are already formatted with colors from logging.c
  for (size_t i = start_idx; i < log_count; i++) {
    printf("%s", logs[i].message);
    if (logs[i].message[0] != '\0' && logs[i].message[strlen(logs[i].message) - 1] != '\n') {
      printf("\n");
    }
  }

  // Fill remaining lines to reach EXACTLY the bottom of screen without scrolling
  for (int i = lines_used; i < logs_available_lines; i++) {
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
