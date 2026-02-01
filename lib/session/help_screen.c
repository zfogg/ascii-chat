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
  if (!bar_output || bar_output_size < 20) {
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

  // Build bar: "████░░ 80%"
  snprintf(bar_output, bar_output_size, "%.*s%.*s %d%%", filled, "██████████", empty, "░░░░░░░░░░",
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
 * @brief Render help screen centered on terminal
 */
void session_display_render_help(session_display_ctx_t *ctx) {
  if (!ctx || !session_display_has_tty(ctx)) {
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

  // Help screen box dimensions
  const int box_width = 46;  // Width of help content
  const int box_height = 23; // Height of help content

  // Calculate centering position
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }
  int start_row = (term_height - box_height) / 2;
  if (start_row < 0) {
    start_row = 0;
  }

  // Build help screen content
  char *buffer = SAFE_MALLOC(4096, char *);
  size_t buf_pos = 0;

#define APPEND(fmt, ...)                                                                                               \
  do {                                                                                                                 \
    int written = snprintf(buffer + buf_pos, 4096 - buf_pos, fmt, ##__VA_ARGS__);                                      \
    if (written > 0) {                                                                                                 \
      buf_pos += written;                                                                                              \
    }                                                                                                                  \
  } while (0)

  // Clear screen and position cursor
  APPEND("\033[2J"); // Clear screen
  APPEND("\033[H");  // Cursor to home

  // Build help screen with proper spacing
  // Top border
  APPEND("\033[%d;%dH", start_row + 1, start_col + 1);
  APPEND("╔════════════════════════════════════════════╗");

  // Title
  APPEND("\033[%d;%dH", start_row + 2, start_col + 1);
  APPEND("║     ascii-chat Keyboard Shortcuts        ║");

  // Separator after title
  APPEND("\033[%d;%dH", start_row + 3, start_col + 1);
  APPEND("╠════════════════════════════════════════════╣");

  // Navigation section
  APPEND("\033[%d;%dH", start_row + 4, start_col + 1);
  APPEND("║                                            ║");

  APPEND("\033[%d;%dH", start_row + 5, start_col + 1);
  APPEND("║  Navigation & Control:                    ║");

  APPEND("\033[%d;%dH", start_row + 6, start_col + 1);
  APPEND("║  ───────────────────                      ║");

  APPEND("\033[%d;%dH", start_row + 7, start_col + 1);
  APPEND("║  ?       Toggle this help screen          ║");

  APPEND("\033[%d;%dH", start_row + 8, start_col + 1);
  APPEND("║  ↑ / ↓   Volume up/down (10%%)             ║");

  APPEND("\033[%d;%dH", start_row + 9, start_col + 1);
  APPEND("║  ← / →   Seek ±30s (files only)          ║");

  APPEND("\033[%d;%dH", start_row + 10, start_col + 1);
  APPEND("║  Space   Play/Pause (files only)          ║");

  APPEND("\033[%d;%dH", start_row + 11, start_col + 1);
  APPEND("║  c       Cycle color mode                 ║");

  APPEND("\033[%d;%dH", start_row + 12, start_col + 1);
  APPEND("║  m       Mute/Unmute audio                ║");

  APPEND("\033[%d;%dH", start_row + 13, start_col + 1);
  APPEND("║  f       Flip webcam horizontally         ║");

  // Current settings section
  APPEND("\033[%d;%dH", start_row + 14, start_col + 1);
  APPEND("║                                            ║");

  APPEND("\033[%d;%dH", start_row + 15, start_col + 1);
  APPEND("║  Current Settings:                        ║");

  APPEND("\033[%d;%dH", start_row + 16, start_col + 1);
  APPEND("║  ───────────────                          ║");

  // Get current option values
  double current_volume = GET_OPTION(speakers_volume);
  int current_color_mode = (int)GET_OPTION(color_mode);
  bool current_flip = (bool)GET_OPTION(webcam_flip);
  bool current_audio = (bool)GET_OPTION(audio_enabled);

  // Format volume bar
  char volume_bar[20];
  format_volume_bar(current_volume, volume_bar, sizeof(volume_bar));

  APPEND("\033[%d;%dH", start_row + 17, start_col + 1);
  APPEND("║  Volume:   %s ║", volume_bar);

  APPEND("\033[%d;%dH", start_row + 18, start_col + 1);
  APPEND("║  Color:    %-16s ║", color_mode_to_string(current_color_mode));

  APPEND("\033[%d;%dH", start_row + 19, start_col + 1);
  APPEND("║  Webcam:   %-16s ║", current_flip ? "Flipped" : "Normal");

  APPEND("\033[%d;%dH", start_row + 20, start_col + 1);
  APPEND("║  Audio:    %-16s ║", current_audio ? "Enabled" : "Disabled");

  // Footer
  APPEND("\033[%d;%dH", start_row + 21, start_col + 1);
  APPEND("║                                            ║");

  APPEND("\033[%d;%dH", start_row + 22, start_col + 1);
  APPEND("║  Press ? to close                         ║");

  // Bottom border
  APPEND("\033[%d;%dH", start_row + 23, start_col + 1);
  APPEND("╚════════════════════════════════════════════╝");

  // Cursor positioning after rendering
  APPEND("\033[%d;%dH", start_row + 24, start_col + 1);

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
