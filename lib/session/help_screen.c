/**
 * @file session/help_screen.c
 * @brief Help screen TUI rendering implementation
 * @ingroup session
 */

#include "help_screen.h"
#include "display.h"
#include "common.h"
#include "options/options.h"
#include "platform/terminal.h"
#include "log/logging.h"
#include "util/string.h"
#include "util/utf8.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

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

  // Line structure: "║  " (3) + label + ": " (2) + padding + value + padding + "║" (3)
  // Total display width: 48 columns
  // Fixed prefix: "║  " (3 bytes = 3 display cols) + label (N bytes = N display cols)
  // Plus ": " (2 bytes = 2 display cols)

  int label_width = utf8_display_width(label);
  int value_width = utf8_display_width(value);

  // Fixed part display width: "║" (1 col) + "  " (2 cols) + label + ":" (1 col) + "    " (4 cols)
  // "║  " = 1+2 = 3, label = label_width, ":" = 1, "    " = 4
  // Total fixed: 1 + 2 + label_width + 1 + 4 = label_width + 8
  int fixed_width = 1 + 2 + label_width + 1 + 4; // "║  " + label + ":" + "    " (in display cols)
  int right_border = 1;                          // "║" = 1 display col

  // Available space for value + padding: 48 - fixed_width - right_border
  int available = 48 - fixed_width - right_border; // Should be 33 columns for standard labels
  int padding = available - value_width;

  // Ensure non-negative padding
  if (padding < 0) {
    padding = 0;
  }

  // Build the line: "║  <label>:    <value><padding>║"
  char *pos = output;
  int remaining = output_size;

  // Left border and spacing
  int n = snprintf(pos, remaining, "║  %s:    ", label);
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

  // Help screen box dimensions (must fit in small terminals - 24 line minimum)
  const int box_width =
      48; // Width of help content in display columns (22 rows total: border + title + nav + settings + footer)

  // Calculate centering position
  // Horizontal centering
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }
  // Vertical positioning: always start near top to ensure entire box fits on screen
  // Standard terminal is 24 rows, box is 22 rows, so start at row 1-2 to fit
  int start_row = 1;

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

  APPEND("\033[%d;%dH", start_row + 7, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "↑ / ↓   Volume up/down (10%)");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 8, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Space   Play/Pause (files only)");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 9, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "c       Cycle color mode");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 10, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "m       Mute/Unmute audio");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 11, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "f       Flip webcam horizontally");
  APPEND("%s", line_buf);

  // Current settings section
  APPEND("\033[%d;%dH", start_row + 12, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Current Settings:");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 13, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "───────────────");
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 14, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "");
  APPEND("%s", line_buf);

  // Get current option values
  double current_volume = GET_OPTION(speakers_volume);
  int current_color_mode = (int)GET_OPTION(color_mode);
  int current_render_mode = (int)GET_OPTION(render_mode);
  bool current_flip = (bool)GET_OPTION(webcam_flip);
  bool current_audio = (bool)GET_OPTION(audio_enabled);

  // Format volume bar as "[========  ] 80%"
  char volume_bar[32];
  format_volume_bar(current_volume, volume_bar, sizeof(volume_bar));

  // Get string values
  const char *color_str = color_mode_to_string(current_color_mode);
  const char *render_str = render_mode_to_string(current_render_mode);
  const char *flip_text = current_flip ? "Flipped" : "Normal";
  const char *audio_text = current_audio ? "Enabled" : "Disabled";

  // Build settings lines with UTF-8 width-aware padding
  APPEND("\033[%d;%dH", start_row + 15, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Volume", volume_bar);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 16, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Color", color_str);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 17, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Render", render_str);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 18, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Webcam", flip_text);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 19, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Audio", audio_text);
  APPEND("%s", line_buf);

  // Footer
  APPEND("\033[%d;%dH", start_row + 20, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Press ? to close");
  APPEND("%s", line_buf);

  // Bottom border
  APPEND("\033[%d;%dH", start_row + 21, start_col + 1);
  APPEND("╚══════════════════════════════════════════════╝");

  // Cursor positioning after rendering
  APPEND("\033[%d;%dH", start_row + 22, start_col + 1);

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
