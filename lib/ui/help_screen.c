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
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
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
 * @brief Build a help screen line with UTF-8 width-aware padding
 *
 * Constructs lines like "║  <content><padding>║" ensuring total display width is 48 columns.
 * Properly accounts for multi-byte UTF-8 characters when calculating padding.
 *
 * @param output Output buffer for the line
 * @param output_size Size of output buffer (must be at least 256 bytes)
 * @param content Content string to display (may contain UTF-8)
 */
static void build_help_line(char *output, size_t output_size, const char *content) {
  if (!output || output_size < 256 || !content) {
    return;
  }

  int content_width = utf8_display_width(content);

  // Line structure: "║  " (3 cols) + content + padding + "║" (1 col) = 48 cols
  // So: padding = 48 - 3 - content_width - 1 = 44 - content_width
  int padding = 44 - content_width;

  if (padding < 0) {
    padding = 0;
  }

  // Build the line
  char *pos = output;
  int remaining = output_size;

  // Left border and spacing
  int n = snprintf(pos, remaining, "║  %s", content);
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
 * @brief Build a settings line with UTF-8 width-aware padding
 *
 * Constructs a line like: "║  Label:      Value<padding>║"
 * The padding is calculated based on actual UTF-8 display width,
 * not byte count, ensuring the right border pipes align vertically.
 *
 * @param output Output buffer for the line
 * @param output_size Size of output buffer (must be at least 256 bytes)
 * @param label Label string (e.g., "Volume")
 * @param value Value string to display (may contain UTF-8)
 */
static void build_settings_line(char *output, size_t output_size, const char *label, const char *value) {
  if (!output || output_size < 256 || !label || !value) {
    return;
  }

  // Align all values to start at the same column by padding labels to fixed width
  // Maximum label width is 6 ("Volume", "Render", "Webcam")
  const int MAX_LABEL_WIDTH = 6;

  int label_width = utf8_display_width(label);
  int value_width = utf8_display_width(value);

  // Calculate label padding to align all values vertically
  int label_padding = MAX_LABEL_WIDTH - label_width;
  if (label_padding < 0) {
    label_padding = 0;
  }

  // Line structure: "║  " (3) + label + padding + ":  " (3) + value + spacing + "║" (1)
  // Fixed part: 1 (║) + 2 (  ) + 6 (label max) + 3 (:__) = 12 columns to value start
  // Total: 12 + value_width + final_padding + 1 = 48

  int fixed_prefix = 1 + 2 + MAX_LABEL_WIDTH + 3; // "║  " + label (padded) + ":  "
  int right_border = 1;                           // "║" = 1 display col

  // Available space for value + padding: 48 - fixed_prefix - right_border
  int available = 48 - fixed_prefix - right_border; // 36 columns for value + padding
  int padding = available - value_width;

  // Ensure non-negative padding
  if (padding < 0) {
    padding = 0;
  }

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

  // Colon and spacing before value (two spaces)
  n = snprintf(pos, remaining, ":  ");
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  // Value
  n = snprintf(pos, remaining, "%s", value);
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
 * @brief Render help screen centered on terminal
 */
void session_display_render_help(session_display_ctx_t *ctx) {
  if (!ctx) {
    return;
  }

  // Get terminal dimensions
  int term_width = (int)GET_OPTION(width);
  int term_height = (int)GET_OPTION(height);

  // Minimum viable terminal size
  if (term_width < 50 || term_height < 20) {
    // Terminal too small - just show a minimal message
    const char *msg = "\n[Terminal too small for help screen - try resizing]\n";
    session_display_write_raw(ctx, msg, strlen(msg));
    return;
  }

  // Help screen box dimensions (24 rows total: border + title + nav (7 lines) + separator + settings + blank + footer +
  // border)
  const int box_width = 48;  // Display columns
  const int box_height = 24; // Total rows including borders

  // Calculate centering position
  // Horizontal centering
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }

  // Vertical centering
  int start_row = (term_height - box_height) / 2;
  if (start_row < 1) {
    start_row = 1; // Never put at top of screen (leave room for prompts)
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

  // Top border
  APPEND("\033[%d;%dH", start_row + 1, start_col + 1);
  APPEND("╔══════════════════════════════════════════════╗");

  // Title
  APPEND("\033[%d;%dH", start_row + 2, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "ascii-chat Keyboard Shortcuts");
  APPEND("%s", line_buf);

  // Separator after title
  APPEND("\033[%d;%dH", start_row + 3, start_col + 1);
  APPEND("╠══════════════════════════════════════════════╣");

  // Navigation section
  APPEND("\033[%d;%dH", start_row + 4, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Navigation & Control:");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 5, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "─────────────────────");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 6, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "?       Toggle this help screen");
  APPEND("%s", line_buf);

  // Only show Play/Pause and Seek controls if a file or URL is being used
  const char *url = GET_OPTION(media_url);
  const char *file = GET_OPTION(media_file);
  int row_offset = 0;
  if ((url && url[0] != '\0') || (file && file[0] != '\0')) {
    APPEND("\033[%d;%dH", start_row + 7, start_col + 1);
    build_help_line(line_buf, sizeof(line_buf), "Space   Play/Pause (files only)");
    APPEND("%s", line_buf);

    APPEND("\033[%d;%dH", start_row + 8, start_col + 1);
    build_help_line(line_buf, sizeof(line_buf), "← / →   Seek backward/forward 30s");
    APPEND("%s", line_buf);
  } else {
    row_offset = -2;  // Skip the 2 hidden lines
  }

  APPEND("\033[%d;%dH", start_row + 9 + row_offset, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "m       Mute/Unmute audio");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 10 + row_offset, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "↑ / ↓   Volume up/down (10%)");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 11 + row_offset, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "c       Cycle color mode");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 12 + row_offset, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "f       Flip webcam horizontally");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 13 + row_offset, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "r       Cycle render mode");
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 14 + row_offset, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "`       Print current sync primitive state");
  APPEND("%s", line_buf);

  // Blank line before settings section (debug builds shift by 1 more row)
  APPEND("\033[%d;%dH", start_row + 15 + row_offset, start_col + 1);
#else
  // Blank line before settings section (release builds skip backtick line)
  APPEND("\033[%d;%dH", start_row + 14 + row_offset, start_col + 1);
#endif
  build_help_line(line_buf, sizeof(line_buf), "");
  APPEND("%s", line_buf);

  // Current settings section
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 16 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 15 + row_offset, start_col + 1);
#endif
  build_help_line(line_buf, sizeof(line_buf), "Current Settings:");
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 17 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 16 + row_offset, start_col + 1);
#endif
  build_help_line(line_buf, sizeof(line_buf), "───────────────");
  APPEND("%s", line_buf);

  // Get current option values
  double current_volume = GET_OPTION(speakers_volume);
  int current_color_mode = (int)GET_OPTION(color_mode);
  int current_render_mode = (int)GET_OPTION(render_mode);
  bool flip_x = (bool)GET_OPTION(flip_x);
  bool flip_y = (bool)GET_OPTION(flip_y);
  bool current_audio = (bool)GET_OPTION(audio_enabled);

  // Format volume bar as "[========  ] 80%"
  char volume_bar[32];
  format_volume_bar(current_volume, volume_bar, sizeof(volume_bar));

  // Get string values
  const char *color_str = color_mode_to_string(current_color_mode);
  const char *render_str = render_mode_to_string(current_render_mode);

  // Create colored strings for flip state
  const char *flip_text = "None";
  if (flip_x && flip_y) {
    flip_text = colored_string(ENABLED_COLOR, "X & Y");
  } else if (flip_x) {
    flip_text = colored_string(ENABLED_COLOR, "X");
  } else if (flip_y) {
    flip_text = colored_string(ENABLED_COLOR, "Y");
  } else {
    flip_text = colored_string(DISABLED_COLOR, "None");
  }

  // Audio state (Enabled = green, Disabled = red)
  const char *audio_text =
      current_audio ? colored_string(ENABLED_COLOR, "Enabled") : colored_string(DISABLED_COLOR, "Disabled");

  // Build settings lines with UTF-8 width-aware padding (ordered to match keybinds: m, ↑/↓, c, r, f)
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 18 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 17 + row_offset, start_col + 1);
#endif
  build_settings_line(line_buf, sizeof(line_buf), "Audio", audio_text);
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 19 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 18 + row_offset, start_col + 1);
#endif
  build_settings_line(line_buf, sizeof(line_buf), "Volume", volume_bar);
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 20 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 19 + row_offset, start_col + 1);
#endif
  build_settings_line(line_buf, sizeof(line_buf), "Color", color_str);
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 21 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 20 + row_offset, start_col + 1);
#endif
  build_settings_line(line_buf, sizeof(line_buf), "Render", render_str);
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 22 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 21 + row_offset, start_col + 1);
#endif
  build_settings_line(line_buf, sizeof(line_buf), "Flip", flip_text);
  APPEND("%s", line_buf);

  // Blank line before footer
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 23 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 22 + row_offset, start_col + 1);
#endif
  build_help_line(line_buf, sizeof(line_buf), "");
  APPEND("%s", line_buf);

  // Footer
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 24 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 23 + row_offset, start_col + 1);
#endif
  build_help_line(line_buf, sizeof(line_buf), "Press ? to close");
  APPEND("%s", line_buf);

  // Bottom border
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 25 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 24 + row_offset, start_col + 1);
#endif
  APPEND("╚══════════════════════════════════════════════╝");

  // Cursor positioning after rendering
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 27 + row_offset, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 26 + row_offset, start_col + 1);
#endif

#undef APPEND

  // Write buffer to terminal
  session_display_write_raw(ctx, buffer, buf_pos);

  // Flush output
  if (ctx && session_display_has_tty(ctx)) {
    int tty_fd = session_display_get_tty_fd(ctx);
    if (tty_fd >= 0) {
      (void)terminal_flush(tty_fd);
    }
  }

  SAFE_FREE(buffer);
}

/* ============================================================================
 * Help Screen State Management
 *
 * Note: session_display_toggle_help() and session_display_is_help_active()
 * are implemented in display.c where they have access to the internal
 * struct session_display_ctx definition containing help_screen_active.
 * ============================================================================ */
