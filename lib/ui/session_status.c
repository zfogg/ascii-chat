/** Server status presentation shared by native and browser sessions. */
#include <ascii-chat/ui/status.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <string.h>

void ui_status_render_header(frame_buffer_t *buf, terminal_size_t term_size, void *user_data) {
  const ui_status_t *status = (const ui_status_t *)user_data;
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
  frame_buffer_printf(buf, "\033[1m%s━", log_level_color(LOG_COLOR_DEBUG));
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
  if (status->detail)
    snprintf(status_line, sizeof(status_line), "ascii-chat %s | ⏱️ %s", status->mode_name, uptime_str);
  else
    snprintf(status_line, sizeof(status_line), "ascii-chat %s | 👥 %zu | ⏱️ %s", status->mode_name,
             status->connected_count, uptime_str);

  // Truncate if needed (ANSI-aware)
  truncate_with_ellipsis(status_line, status_line_truncated, sizeof(status_line_truncated), term_size.cols - 2);
  snprintf(status_line_colored, sizeof(status_line_colored), "\033[1m%s%s\033[0m", log_level_color(LOG_COLOR_DEBUG),
           status_line_truncated);

  int padding = display_center_horizontal(status_line_truncated, term_size.cols);
  for (int i = 0; i < padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", status_line_colored);

  // Line 3: Session + Addresses (centered)
  char addr_line[512] = {0};
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

  if (status->detail)
    SAFE_STRNCPY(addr_line, status->detail, sizeof(addr_line));

  // Truncate if needed (ANSI-aware)
  truncate_with_ellipsis(addr_line, addr_line_truncated, sizeof(addr_line_truncated), term_size.cols - 2);

  int addr_padding = display_center_horizontal(addr_line_truncated, term_size.cols);
  for (int i = 0; i < addr_padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", addr_line_truncated);

  // Line 4: Bottom border
  frame_buffer_printf(buf, "\033[1m%s━", log_level_color(LOG_COLOR_DEBUG));
  for (int i = 1; i < term_size.cols - 1; i++) {
    frame_buffer_printf(buf, "━");
  }
  frame_buffer_printf(buf, "\033[0m\n");
}

void terminal_screen_compose(frame_buffer_t *output, terminal_size_t size, const terminal_screen_config_t *config,
                             session_log_buffer_t *logs) {
  if (!output || !config || !config->render_header || size.cols < 1 || size.rows < 1)
    return;
  frame_buffer_t *body = frame_buffer_create(size.rows, size.cols);
  if (!body)
    return;
  config->render_header(body, size, config->user_data);

  // Measure the actual header, including wrapped lines, as the native status UI does.
  int header_rows = 0;
  const char *line = frame_buffer_get_content(body);
  const char *end = line + frame_buffer_get_length(body);
  for (const char *cursor = line; cursor < end; ++cursor) {
    if (*cursor == '\n') {
      char text[2048];
      size_t length = (size_t)(cursor - line);
      if (length >= sizeof(text))
        length = sizeof(text) - 1;
      memcpy(text, line, length);
      text[length] = '\0';
      int height = display_height(text, size.cols);
      header_rows += height > 0 ? height : 1;
      line = cursor + 1;
    }
  }
  if (!header_rows)
    header_rows = config->fixed_header_lines;
  bool entering = config->show_logs && log_search_is_entering();
  int available = size.rows - header_rows - (entering ? 2 : 1);
  session_log_entry_t *entries = NULL;
  size_t count = 0;
  if (config->show_logs && available > 0) {
    if (log_search_is_active())
      log_search_filter_buffer(logs, &entries, &count);
    else {
      entries = SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(*entries), session_log_entry_t *);
      if (entries)
        count = session_log_buffer_get_recent(logs, entries, SESSION_LOG_BUFFER_SIZE);
    }
    // Choose a contiguous suffix so a long earlier message cannot displace newer logs.
    size_t first = count;
    int used = 0;
    while (first) {
      int height = display_height(entries[first - 1].message, size.cols);
      if (height < 1)
        height = 1;
      if (used + height > available)
        break;
      used += height;
      --first;
    }
    for (size_t i = first; i < count; ++i) {
      const char *message = entries[i].message;
      char plain[SESSION_LOG_LINE_MAX];
      strip_ansi_codes(message, plain, sizeof(plain));
      size_t start = 0, length = 0;
      if (log_search_is_active() && log_search_get_match_info(plain, &start, &length) && length)
        message = grep_highlight_colored(message, plain, start, length);
      frame_buffer_printf(body, "%s\033[0m\n", message);
    }
    while (used++ < available)
      frame_buffer_printf(body, "\n");
  }
  SAFE_FREE(entries);

  // Preserve the existing repaint-in-place behavior, also supplying CR for browser terminals.
  frame_buffer_printf(output, "\033[H%s", config->hide_cursor ? "\033[?25l" : "\033[?25h");
  const char *content = frame_buffer_get_content(body);
  size_t length = frame_buffer_get_length(body);
  for (size_t i = 0; i < length; ++i) {
    if (content[i] == '\n')
      frame_buffer_printf(output, "\033[K\r");
    frame_buffer_append(output, content + i, 1);
  }
  if (entering) {
    mutex_t *mutex = log_search_get_mutex();
    mutex_lock(mutex);
    const char *pattern = log_search_get_input_buffer();
    int cursor = log_search_get_cursor_position();
    // Keep the edit cursor visible even when a pattern exceeds the viewport.
    int offset = cursor >= size.cols - 2 ? cursor - (size.cols - 3) : 0;
    frame_buffer_printf(output, "\033[%d;1H/%.*s\033[K\033[%d;%dH", size.rows, size.cols - 2,
                        pattern ? pattern + offset : "", size.rows, cursor - offset + 2);
    mutex_unlock(mutex);
  } else if (config->show_logs) {
    frame_buffer_printf(output, "\033[%d;1H\033[2K", size.rows);
  }
  frame_buffer_destroy(body);
}
