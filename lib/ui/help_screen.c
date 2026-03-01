/**
 * @file ui/help_screen.c
 * @brief Help screen TUI rendering implementation
 * @ingroup session
 */

#include <ascii-chat/ui/help_screen.h>
#include "session/display.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/video/color_filter.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

/* Color codes for enabled/disabled settings */
#define ENABLED_COLOR LOG_COLOR_INFO   /* Green */
#define DISABLED_COLOR LOG_COLOR_ERROR /* Red */

/* ============================================================================
 * Help Screen Rendering
 * ============================================================================ */

/**
 * @brief Build a volume bar graph string
 *
 * Renders volume as a bar graph: "████████░░ 80%"
 *
 * @param volume Volume value [0.0, 1.0]
 * @param bar_output Output buffer for bar graph (must be at least 20 bytes)
 * @param bar_output_size Size of output buffer
 */
static void format_volume_bar(double volume, char *bar_output, size_t bar_output_size) {
  if (!bar_output || bar_output_size < 25) {
    return;
  }

  // Clamp volume to [0.0, 1.0]
  if (volume < 0.0) {
    volume = 0.0;
  }
  if (volume > 1.0) {
    volume = 1.0;
  }

  // Calculate filled blocks (10 blocks total for 10% granularity)
  int filled = (int)(volume * 10.0);
  int empty = 10 - filled;

  // Build bar: "[======    ] 80%"
  // Use simple ASCII characters to avoid UTF-8 encoding issues
  snprintf(bar_output, bar_output_size, "[%.*s%.*s] %d%%", filled, "==========", empty, "          ",
           (int)(volume * 100.0));
}

/**
 * @brief Get color mode name
 */
static const char *color_mode_to_string(int mode) {
  switch (mode) {
  case -1:
    return "Auto";
  case 0:
    return "Mono";
  case 1:
    return "16-color";
  case 2:
    return "256-color";
  case 3:
    return "Truecolor";
  default:
    return "Unknown";
  }
}

/**
 * @brief Get render mode name
 */
static const char *render_mode_to_string(int mode) {
  switch (mode) {
  case 0:
    return "Foreground";
  case 1:
    return "Background";
  case 2:
    return "Half-block";
  default:
    return "Unknown";
  }
}

/**
 * @brief Get color filter name
 */
static const char *color_filter_to_string(color_filter_t filter) {
  if (filter == COLOR_FILTER_NONE) {
    return "None";
  }
  const color_filter_def_t *def = color_filter_get_metadata(filter);
  return def ? def->name : "Unknown";
}

/**
 * @brief Build a help screen line with UTF-8 width-aware padding and truncation
 *
 * Constructs lines like "║  <content><padding>║" with the specified max width.
 * Properly accounts for multi-byte UTF-8 characters and truncates if needed.
 *
 * @param output Output buffer for the line
 * @param output_size Size of output buffer (must be at least 256 bytes)
 * @param content Content string to display (may contain UTF-8)
 * @param max_width Maximum total line width
 */
static void build_help_line(char *output, size_t output_size, const char *content, int max_width) {
  if (!output || output_size < 256 || !content || max_width < 6) {
    return;
  }

  // Available width: max_width - 3 (left border "║  ") - 1 (right border "║")
  int content_available = max_width - 4;
  if (content_available < 1)
    content_available = 1;

  // Truncate content if needed (with ellipsis indicator)
  char truncated[256];
  truncate_utf8_with_ellipsis(content, truncated, sizeof(truncated), content_available);

  int content_width = utf8_display_width(truncated);
  int padding = content_available - content_width;
  if (padding < 0)
    padding = 0;

  // Build the line
  char *pos = output;
  int remaining = output_size;

  // Left border and spacing
  int n = snprintf(pos, remaining, "║  %s", truncated);
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  // Padding spaces
  for (int i = 0; i < padding && remaining > 1; i++) {
    *pos++ = ' ';
    remaining--;
  }

  // Right border
  if (remaining > 3) {
    snprintf(pos, remaining, "║");
  }
}

/**
 * @brief Build a settings line with UTF-8 width-aware padding and truncation
 *
 * Constructs a line like: "║  Label:      Value<padding>║"
 * The padding is calculated based on actual UTF-8 display width,
 * not byte count, ensuring the right border pipes align vertically.
 *
 * @param output Output buffer for the line
 * @param output_size Size of output buffer (must be at least 256 bytes)
 * @param label Label string (e.g., "Volume")
 * @param value Value string to display (may contain UTF-8)
 * @param max_width Maximum total line width
 * @param label_width Fixed width for labels (usually 6 for standard settings, wider for other sections)
 */
static void build_settings_line(char *output, size_t output_size, const char *label, const char *value, int max_width,
                                int label_width) {
  if (!output || output_size < 256 || !label || !value || max_width < 20) {
    return;
  }

  // Align all values to start at the same column by padding labels to fixed width
  const int MAX_LABEL_WIDTH = label_width;

  int actual_label_width = utf8_display_width(label);

  // Calculate label padding to align all values vertically
  int label_padding = MAX_LABEL_WIDTH - actual_label_width;
  if (label_padding < 0) {
    label_padding = 0;
  }

  // Fixed prefix: "║  " (3) + label (padded to MAX_LABEL_WIDTH) + " : " (3) = 6 + MAX_LABEL_WIDTH columns
  int fixed_prefix = 1 + 2 + MAX_LABEL_WIDTH + 3;
  int right_border = 1;

  // Available space for value + final padding
  int available = max_width - fixed_prefix - right_border;
  if (available < 4)
    available = 4; // Minimum space for truncated value

  // Truncate value if needed (with ellipsis indicator)
  char truncated_value[256];
  truncate_utf8_with_ellipsis(value, truncated_value, sizeof(truncated_value), available);

  int value_width = utf8_display_width(truncated_value);
  int padding = available - value_width;
  if (padding < 0)
    padding = 0;

  // Build the line: "║  <label><label_pad>:  <value><padding>║"
  char *pos = output;
  int remaining = output_size;

  // Left border, spacing, and label
  int n = snprintf(pos, remaining, "║  %s", label);
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  // Add label padding spaces to align values
  for (int i = 0; i < label_padding && remaining > 1; i++) {
    *pos++ = ' ';
    remaining--;
  }

  // Colon with spacing (one space before and after)
  n = snprintf(pos, remaining, " : ");
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  // Value (already truncated)
  n = snprintf(pos, remaining, "%s", truncated_value);
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  // Final padding spaces
  for (int i = 0; i < padding && remaining > 1; i++) {
    *pos++ = ' ';
    remaining--;
  }

  // Right border
  if (remaining > 3) {
    snprintf(pos, remaining, "║");
  }
}

/**
 * @brief Format enabled/disabled status as colored X or O
 * @param enabled true for enabled (green O), false for disabled (red X)
 * @return Colored string with "O" or "X"
 */
static const char *status_indicator(bool enabled) {
  return enabled ? colored_string(ENABLED_COLOR, "O") : colored_string(DISABLED_COLOR, "X");
}

/**
 * @brief Helper to append a help line to the buffer
 */
static void append_help_line(char *buffer, size_t *buf_pos, size_t BUFFER_SIZE, int start_row, int *current_row,
                             int start_col, int box_width, const char *content) {
  if (!buffer || *buf_pos >= BUFFER_SIZE || !content) {
    return;
  }

  char line_buf[256];
  int remaining = BUFFER_SIZE - *buf_pos;

  int written = snprintf(buffer + *buf_pos, remaining, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += written;
  }

  build_help_line(line_buf, sizeof(line_buf), content, box_width);
  written = snprintf(buffer + *buf_pos, BUFFER_SIZE - *buf_pos, "%s", line_buf);
  if (written > 0) {
    *buf_pos += written;
  }

  (*current_row)++;
}

/**
 * @brief Helper to append a settings line to the buffer
 * @param label_width Fixed width for label alignment (typically 6 for standard settings)
 */
static void append_settings_line(char *buffer, size_t *buf_pos, size_t BUFFER_SIZE, int start_row, int *current_row,
                                 int start_col, int box_width, const char *label, const char *value, int label_width) {
  if (!buffer || *buf_pos >= BUFFER_SIZE || !label || !value) {
    return;
  }

  char line_buf[256];
  int remaining = BUFFER_SIZE - *buf_pos;

  int written = snprintf(buffer + *buf_pos, remaining, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += written;
  }

  build_settings_line(line_buf, sizeof(line_buf), label, value, box_width, label_width);
  written = snprintf(buffer + *buf_pos, BUFFER_SIZE - *buf_pos, "%s", line_buf);
  if (written > 0) {
    *buf_pos += written;
  }

  (*current_row)++;
}

/**
 * @brief Render help screen centered on terminal
 */
void session_display_render_help(session_display_ctx_t *ctx) {
  if (!ctx) {
    log_error("session_display_render_help: ctx is NULL!");
    return;
  }

  log_info("session_display_render_help: STARTING");

  // Get terminal dimensions
  int term_width = (int)terminal_get_effective_width();
  int term_height = (int)terminal_get_effective_height();
  log_info("session_display_render_help: term_width=%d, term_height=%d", term_width, term_height);

  // Use available terminal width, capped at preferred width
  int box_width = term_width;
  if (box_width > 48)
    box_width = 48; // Cap at preferred width
  if (box_width < 30)
    box_width = 30; // Absolute minimum for readability

  // Help screen box dimensions (24 rows total: border + title + nav (7 lines) + separator + settings + blank + footer +
  // border)
  const int box_height = 25; // Total rows including borders

  // Calculate centering position (true mathematical centering)
  // Horizontal centering: center of box at center of terminal
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }

  // Vertical centering: center of box at center of terminal
  int start_row = (term_height - box_height) / 2;
  if (start_row < 0) {
    start_row = 0;
  }

  // Build help screen content
  const size_t BUFFER_SIZE = 8192; // Increased from 4096 to ensure all content fits
  char *buffer = SAFE_MALLOC(BUFFER_SIZE, char *);
  size_t buf_pos = 0;

#define APPEND(fmt, ...)                                                                                               \
  do {                                                                                                                 \
    int written = snprintf(buffer + buf_pos, BUFFER_SIZE - buf_pos, fmt, ##__VA_ARGS__);                               \
    if (written > 0) {                                                                                                 \
      buf_pos += written;                                                                                              \
    }                                                                                                                  \
  } while (0)

  // Clear screen and position cursor
  APPEND("\033[2J"); // Clear screen
  APPEND("\033[H");  // Cursor to home

  // Build help screen with proper spacing using UTF-8 width-aware padding
  char line_buf[256];
  char border_buf[256];

  // Generate top border
  APPEND("\033[%d;%dH", start_row + 1, start_col + 1);
  border_buf[0] = '\0';
  strcat(border_buf, "╔");
  for (int i = 1; i < box_width - 1; i++)
    strcat(border_buf, "═");
  strcat(border_buf, "╗");
  APPEND("%s", border_buf);

  // Title
  APPEND("\033[%d;%dH", start_row + 2, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "ascii-chat Keyboard Shortcuts", box_width);
  APPEND("%s", line_buf);

  // Generate separator border
  APPEND("\033[%d;%dH", start_row + 3, start_col + 1);
  border_buf[0] = '\0';
  strcat(border_buf, "╠");
  for (int i = 1; i < box_width - 1; i++)
    strcat(border_buf, "═");
  strcat(border_buf, "╣");
  APPEND("%s", border_buf);

  // Navigation section
  int current_row = 4;
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "Navigation & Control:");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "─────────────────────");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "?       Toggle this help screen");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "Esc     Close help / Quit app");

  // Check if media is provided (only show Space/Seek keys if media is loaded)
  const char *media_url = GET_OPTION(media_url);
  const char *media_file = GET_OPTION(media_file);
  bool has_media = (media_url && strlen(media_url) > 0) || (media_file && strlen(media_file) > 0);

  if (has_media) {
    append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                     "Space   Play/Pause (files only)");
    append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                     "← / →   Seek backward/forward 30s");
  }

  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "m / M   Mute/Unmute audio");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "↑ / ↓   Volume up/down (10%)");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "c / C   Cycle color mode");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "f / F   Cycle color filter");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "x / X   Flip webcam horizontally");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "y / Y   Flip webcam vertically");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "r / R   Cycle render mode");

#ifndef NDEBUG
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "`       Print current sync primitive state");
#endif

  // Blank line before settings section
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "");

  // Current settings section
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Current Settings:");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "───────────────");

  // Get current option values
  double current_volume = GET_OPTION(speakers_volume);
  int current_color_mode = (int)GET_OPTION(color_mode);
  int current_render_mode = (int)GET_OPTION(render_mode);
  color_filter_t current_color_filter = GET_OPTION(color_filter);
  bool flip_x = (bool)GET_OPTION(flip_x);
  bool flip_y = (bool)GET_OPTION(flip_y);
  bool current_audio = (bool)GET_OPTION(audio_enabled);

  // Format volume bar as "[========  ] 80%"
  char volume_bar[32];
  format_volume_bar(current_volume, volume_bar, sizeof(volume_bar));

  // Get string values
  const char *color_str = color_mode_to_string(current_color_mode);
  const char *filter_str = color_filter_to_string(current_color_filter);
  const char *render_str = render_mode_to_string(current_render_mode);

  // Create status indicators for flip, audio, and matrix rain
  // Format flip status as "rows=X/O cols=X/O" (rows=flip_y, cols=flip_x)
  char flip_status[64];
  snprintf(flip_status, sizeof(flip_status), "rows=%s cols=%s", status_indicator(flip_y), status_indicator(flip_x));

  const char *audio_text = status_indicator(current_audio);
  bool matrix_rain_enabled = GET_OPTION(matrix_rain);
  const char *matrix_text = status_indicator(matrix_rain_enabled);

  // Build settings lines with UTF-8 width-aware padding (ordered to match keybinds: m, ↑/↓, c, f, x/y, r)
  append_settings_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Audio",
                       audio_text, 6);
  append_settings_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Volume",
                       volume_bar, 6);
  append_settings_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Color", color_str,
                       6);
  append_settings_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Filter",
                       filter_str, 6);
  append_settings_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Render",
                       render_str, 6);
  append_settings_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Flip",
                       flip_status, 6);

  // Blank line before animations section
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "");

  // Animations section
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width,
                   "Animations (number key toggle):");

  // Format: "(0) Matrix \"Digital Rain\" : X/O"
  char animation_line[256];
  snprintf(animation_line, sizeof(animation_line), "(0) Matrix \"Digital Rain\" : %s", matrix_text);
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, animation_line);

  // FPS Counter toggle
  char fps_line[256];
  snprintf(fps_line, sizeof(fps_line), "(-) FPS Counter : %s", status_indicator(GET_OPTION(fps_counter)));
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, fps_line);

  // Blank line before footer
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "");

  // Footer
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "Press ? to close");

  // Bottom border
  int remaining_buf = BUFFER_SIZE - buf_pos;
  int written = snprintf(buffer + buf_pos, remaining_buf, "\033[%d;%dH", start_row + current_row, start_col + 1);
  if (written > 0) {
    buf_pos += written;
  }
  border_buf[0] = '\0';
  strcat(border_buf, "╚");
  for (int i = 1; i < box_width - 1; i++)
    strcat(border_buf, "═");
  strcat(border_buf, "╝");
  written = snprintf(buffer + buf_pos, BUFFER_SIZE - buf_pos, "%s", border_buf);
  if (written > 0) {
    buf_pos += written;
  }

#undef APPEND

  log_info("session_display_render_help: buffer prepared, buf_pos=%zu", buf_pos);

  // Write buffer to terminal
  session_display_write_raw(ctx, buffer, buf_pos);
  log_info("session_display_render_help: buffer written to terminal");

  // Flush output
  if (ctx && session_display_has_tty(ctx)) {
    int tty_fd = session_display_get_tty_fd(ctx);
    log_info("session_display_render_help: tty_fd=%d", tty_fd);
    if (tty_fd >= 0) {
      (void)terminal_flush(tty_fd);
      log_info("session_display_render_help: terminal flushed");
    }
  }

  SAFE_FREE(buffer);
  log_info("session_display_render_help: COMPLETE");
}

/* ============================================================================
 * Help Screen State Management
 *
 * Note: session_display_toggle_help() and session_display_is_help_active()
 * are implemented in display.c where they have access to the internal
 * struct session_display_ctx definition containing help_screen_active.
 * ============================================================================ */
