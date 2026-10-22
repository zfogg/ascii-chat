/** Server status presentation shared by native and browser sessions. */
#include <ascii-chat/ui/status.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/ui/overlay_box.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/options.h>
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
  utf8_setting_t utf8 = GET_OPTION(force_utf8);
  bool unicode = utf8 == UTF8_SETTING_TRUE || (utf8 == UTF8_SETTING_AUTO && terminal_supports_utf8());
  bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(frame_buffer_get_screen_output_fd());
  frame_buffer_render_divider(buf, term_size.cols - 1, unicode, colors);

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
  snprintf(status_line_colored, sizeof(status_line_colored), "\033[1m%s\033[0m",
           colored_string(LOG_COLOR_DEBUG, status_line_truncated));

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
  frame_buffer_render_divider(buf, term_size.cols - 1, unicode, colors);
}

typedef struct {
  size_t entry;
  int row;
} log_row_ref_t;

static int log_message_rows(const char *message, int width) {
  int rows = display_height(message, width);
  return rows > 0 ? rows : 1;
}

static size_t utf8_byte_offset_for_char(const char *text, size_t character_offset) {
  const char *cursor = text;
  while (*cursor && character_offset) {
    uint32_t codepoint;
    int length = utf8_decode((const uint8_t *)cursor, &codepoint);
    cursor += length > 0 ? length : 1;
    --character_offset;
  }
  return (size_t)(cursor - text);
}

static void append_log_row(frame_buffer_t *body, const char *message, int row, int width) {
  char plain[SESSION_LOG_LINE_MAX];
  strip_ansi_codes(message, plain, sizeof(plain));
  const char *start = plain;
  size_t remaining = strlen(plain);
  for (int i = 0; i < row && remaining; ++i) {
    size_t prefix = utf8_prefix_bytes_for_width(start, remaining, width);
    if (!prefix)
      break;
    start += prefix;
    remaining -= prefix;
  }
  if (!remaining) {
    frame_buffer_printf(body, "\n");
    return;
  }
  size_t length = utf8_prefix_bytes_for_width(start, remaining, width);
  if (!length) {
    frame_buffer_printf(body, "\n");
    return;
  }
  char line[SESSION_LOG_LINE_MAX];
  memcpy(line, start, length);
  line[length] = '\0';
  size_t match_start = 0;
  size_t match_length = 0;
  const char *rendered = line;
  if (log_search_get_match_info(line, &match_start, &match_length)) {
    size_t start_byte = utf8_byte_offset_for_char(line, match_start);
    size_t end_byte = utf8_byte_offset_for_char(line, match_start + match_length);
    rendered = grep_highlight(line, start_byte, end_byte - start_byte);
  }
  frame_buffer_printf(body, "%s\033[0m\n", rendered);
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
    log_row_ref_t *rows = SAFE_MALLOC((size_t)available * sizeof(*rows), log_row_ref_t *);
    if (rows) {
      size_t total_rows = 0;
      uint64_t previous_tail_sequence = 0;
      size_t appended_rows = 0;
      for (size_t entry = 0; entry < count; ++entry) {
        size_t height = (size_t)log_message_rows(entries[entry].message, size.cols);
        if (SIZE_MAX - total_rows < height)
          total_rows = SIZE_MAX;
        else
          total_rows += height;
      }
      uint64_t tail_sequence = count ? entries[count - 1].sequence : 0;
      previous_tail_sequence = terminal_screen_logs_last_tail_sequence();
      if (previous_tail_sequence && tail_sequence > previous_tail_sequence) {
        for (size_t entry = 0; entry < count; ++entry) {
          if (entries[entry].sequence > previous_tail_sequence)
            appended_rows += (size_t)log_message_rows(entries[entry].message, size.cols);
        }
      }
      size_t max_offset = total_rows ? total_rows - 1 : 0;
      size_t skip = terminal_screen_logs_retain_position(tail_sequence, appended_rows, max_offset);
      size_t row_count = 0;
      for (size_t entry = count; entry && row_count < (size_t)available; --entry) {
        int height = log_message_rows(entries[entry - 1].message, size.cols);
        if (skip >= (size_t)height) {
          skip -= (size_t)height;
          continue;
        }
        int row = height - 1 - (int)skip;
        skip = 0;
        for (; row >= 0 && row_count < (size_t)available; --row)
          rows[row_count++] = (log_row_ref_t){.entry = entry - 1, .row = row};
      }
      for (size_t row = row_count; row; --row)
        append_log_row(body, entries[rows[row - 1].entry].message, rows[row - 1].row, size.cols);
      while (row_count++ < (size_t)available)
        frame_buffer_printf(body, "\n");
      SAFE_FREE(rows);
    }
  }
  SAFE_FREE(entries);

  // Preserve the existing repaint-in-place behavior, also supplying CR for browser terminals.
  frame_buffer_printf(output, "\033[H%s", config->hide_cursor ? "\033[?25l" : "\033[?25h");
  const char *content = frame_buffer_get_content(body);
  size_t length = frame_buffer_get_length(body);
  for (size_t i = 0; i < length; ++i) {
    // Normalize CRLF before erasing the line; erasing after CR removes its text.
    if (content[i] == '\r' && i + 1 < length && content[i + 1] == '\n')
      continue;
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
  if (config->show_logs && terminal_screen_logs_help_open()) {
    char lines[4][128];
    snprintf(lines[0], sizeof(lines[0]), "%s / %s        newer line", colored_string(LOG_COLOR_WARN, "j"),
             colored_string(LOG_COLOR_WARN, "Down"));
    snprintf(lines[1], sizeof(lines[1]), "%s / %s          older line", colored_string(LOG_COLOR_WARN, "k"),
             colored_string(LOG_COLOR_WARN, "Up"));
    snprintf(lines[2], sizeof(lines[2]), "%s / %s  half page newer", colored_string(LOG_COLOR_WARN, "Ctrl-D"),
             colored_string(LOG_COLOR_WARN, "Right"));
    snprintf(lines[3], sizeof(lines[3]), "%s / %s   half page older", colored_string(LOG_COLOR_WARN, "Ctrl-U"),
             colored_string(LOG_COLOR_WARN, "Left"));
    const char *line_text[] = {lines[0], lines[1], lines[2], lines[3]};
    char footer[128];
    snprintf(footer, sizeof(footer), "%s or %s closes this help", colored_string(LOG_COLOR_WARN, "?"),
             colored_string(LOG_COLOR_WARN, "Esc"));
    ui_overlay_box_t box = {.title = "Log Viewer Keyboard Controls",
                            .lines = line_text,
                            .line_count = sizeof(line_text) / sizeof(line_text[0]),
                            .footer = footer};
    (void)ui_overlay_box_render(output, size, &box);
  } else if (config->show_logs && terminal_screen_logs_help_too_small()) {
    frame_buffer_printf(output, "\033[%d;1H%s", size.rows, "Help needs at least 11 terminal rows");
  }
  frame_buffer_destroy(body);
}
