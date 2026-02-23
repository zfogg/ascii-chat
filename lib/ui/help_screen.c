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
 * @brief Truncate UTF-8 string to fit within max display width
 *
 * @param input Input string (may be UTF-8)
 * @param output Output buffer for truncated string
 * @param output_size Size of output buffer
 * @param max_width Maximum display width in columns
 */
static void truncate_to_width(const char *input, char *output, size_t output_size, int max_width) {
  if (!input || !output || output_size < 2) {
    if (output && output_size > 0) output[0] = '\0';
    return;
  }

  // Copy input to output first
  int input_len = strlen(input);
  if (input_len >= (int)output_size) {
    input_len = output_size - 1;
  }
  memcpy(output, input, input_len);
  output[input_len] = '\0';

  // Check if truncation is needed
  int current_width = utf8_display_width(output);
  if (current_width <= max_width) {
    return;  // Already fits
  }

  // Truncate byte by byte, checking display width at each step
  for (int i = input_len - 1; i > 0; i--) {
    output[i] = '\0';
    current_width = utf8_display_width(output);
    if (current_width <= max_width) {
      return;  // Found the right length
    }
  }

  // Fallback: just one character or empty
  output[0] = '\0';
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
  if (content_available < 1) content_available = 1;

  // Truncate content if needed
  char truncated[256];
  truncate_to_width(content, truncated, sizeof(truncated), content_available);

  int content_width = utf8_display_width(truncated);
  int padding = content_available - content_width;
  if (padding < 0) padding = 0;

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
 */
static void build_settings_line(char *output, size_t output_size, const char *label, const char *value, int max_width) {
  if (!output || output_size < 256 || !label || !value || max_width < 20) {
    return;
  }

  // Align all values to start at the same column by padding labels to fixed width
  const int MAX_LABEL_WIDTH = 6;

  int label_width = utf8_display_width(label);

  // Calculate label padding to align all values vertically
  int label_padding = MAX_LABEL_WIDTH - label_width;
  if (label_padding < 0) {
    label_padding = 0;
  }

  // Fixed prefix: "║  " (3) + label (padded to 6) + ":  " (3) = 12 columns
  int fixed_prefix = 1 + 2 + MAX_LABEL_WIDTH + 3;
  int right_border = 1;

  // Available space for value + final padding
  int available = max_width - fixed_prefix - right_border;
  if (available < 4) available = 4;  // Minimum space for truncated value

  // Truncate value if needed
  char truncated_value[256];
  truncate_to_width(value, truncated_value, sizeof(truncated_value), available);

  int value_width = utf8_display_width(truncated_value);
  int padding = available - value_width;
  if (padding < 0) padding = 0;

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
 * @brief Render help screen centered on terminal
 */
void session_display_render_help(session_display_ctx_t *ctx) {
  if (!ctx) {
    return;
  }

  // Get terminal dimensions
  int term_width = (int)GET_OPTION(width);
  int term_height = (int)GET_OPTION(height);

  // Use available terminal width, capped at preferred width
  int box_width = term_width;
  if (box_width > 48) box_width = 48;  // Cap at preferred width
  if (box_width < 30) box_width = 30;  // Absolute minimum for readability

  // Help screen box dimensions (24 rows total: border + title + nav (7 lines) + separator + settings + blank + footer +
  // border)
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
  char border_buf[256];

  // Generate top border
  APPEND("\033[%d;%dH", start_row + 1, start_col + 1);
  border_buf[0] = '\0';
  strcat(border_buf, "╔");
  for (int i = 1; i < box_width - 1; i++) strcat(border_buf, "═");
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
  for (int i = 1; i < box_width - 1; i++) strcat(border_buf, "═");
  strcat(border_buf, "╣");
  APPEND("%s", border_buf);

  // Navigation section
  APPEND("\033[%d;%dH", start_row + 4, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Navigation & Control:", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 5, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "─────────────────────", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 6, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "?       Toggle this help screen", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 7, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Space   Play/Pause (files only)", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 8, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "← / →   Seek backward/forward 30s", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 9, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "m       Mute/Unmute audio", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 10, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "↑ / ↓   Volume up/down (10%)", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 11, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "c       Cycle color mode", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 12, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "f       Flip webcam horizontally", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 13, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "r       Cycle render mode", box_width);
  APPEND("%s", line_buf);

#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 14, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "`       Print current sync primitive state", box_width);
  APPEND("%s", line_buf);

  // Blank line before settings section
  APPEND("\033[%d;%dH", start_row + 15, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "", box_width);
  APPEND("%s", line_buf);

  // Current settings section (adjusted row numbers for Ctrl+L line)
  APPEND("\033[%d;%dH", start_row + 16, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Current Settings:", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 17, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "───────────────", box_width);
  APPEND("%s", line_buf);
#else
  // Blank line before settings section
  APPEND("\033[%d;%dH", start_row + 14, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "", box_width);
  APPEND("%s", line_buf);

  // Current settings section
  APPEND("\033[%d;%dH", start_row + 15, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Current Settings:", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 16, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "───────────────", box_width);
  APPEND("%s", line_buf);
#endif

  // Current settings section
  APPEND("\033[%d;%dH", start_row + 15, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Current Settings:", box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 16, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "───────────────", box_width);
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
  APPEND("\033[%d;%dH", start_row + 18, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Audio", audio_text, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 19, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Volume", volume_bar, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 20, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Color", color_str, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 21, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Render", render_str, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 22, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Flip", flip_text, box_width);
  APPEND("%s", line_buf);

  // Blank line before footer
  APPEND("\033[%d;%dH", start_row + 23, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "", box_width);
  APPEND("%s", line_buf);

  // Footer
  APPEND("\033[%d;%dH", start_row + 24, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Press ? to close", box_width);
  APPEND("%s", line_buf);

  // Bottom border
  APPEND("\033[%d;%dH", start_row + 25, start_col + 1);
  border_buf[0] = '\0'; strcat(border_buf, "╚"); for (int i = 1; i < box_width - 1; i++) strcat(border_buf, "═"); strcat(border_buf, "╝"); APPEND("%s", border_buf);
#else
  APPEND("\033[%d;%dH", start_row + 17, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Audio", audio_text, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 18, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Volume", volume_bar, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 19, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Color", color_str, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 20, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Render", render_str, box_width);
  APPEND("%s", line_buf);

  APPEND("\033[%d;%dH", start_row + 21, start_col + 1);
  build_settings_line(line_buf, sizeof(line_buf), "Flip", flip_text, box_width);
  APPEND("%s", line_buf);

  // Blank line before footer
  APPEND("\033[%d;%dH", start_row + 22, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "", box_width);
  APPEND("%s", line_buf);

  // Footer
  APPEND("\033[%d;%dH", start_row + 23, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "Press ? to close", box_width);
  APPEND("%s", line_buf);

  // Bottom border
  APPEND("\033[%d;%dH", start_row + 24, start_col + 1);
  border_buf[0] = '\0'; strcat(border_buf, "╚"); for (int i = 1; i < box_width - 1; i++) strcat(border_buf, "═"); strcat(border_buf, "╝"); APPEND("%s", border_buf);
#endif

  // Cursor positioning after rendering
#ifndef NDEBUG
  APPEND("\033[%d;%dH", start_row + 26, start_col + 1);
#else
  APPEND("\033[%d;%dH", start_row + 25, start_col + 1);
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
