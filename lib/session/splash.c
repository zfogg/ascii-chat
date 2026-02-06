/**
 * @file session/splash.c
 * @brief Intro and status screen display implementation with animated rainbow effects
 * @ingroup session
 *
 * Implements splash screen functionality including:
 * - TTY detection with stdin/stdout checks
 * - Animated rainbow color generation and cycling
 * - UTF-8 box drawing character support with ASCII fallback
 * - Non-blocking animation that runs while app initializes
 * - Clean integration with main frame rendering
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/session/splash.h>
#include <ascii-chat/session/display.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

// ============================================================================
// ASCII Art and Constants
// ============================================================================

#define ASCII_LOGO_LINES 7
#define ASCII_LOGO_WIDTH 36

/**
 * @brief Rainbow colors in RGB for smooth gradient transitions
 */
typedef struct {
  uint8_t r, g, b;
} rgb_color_t;

static const rgb_color_t g_rainbow_colors[] = {
    {255, 0, 0},   // Red
    {255, 165, 0}, // Orange
    {255, 255, 0}, // Yellow
    {0, 255, 0},   // Green
    {0, 255, 255}, // Cyan
    {0, 0, 255},   // Blue
    {255, 0, 255}  // Magenta
};
#define RAINBOW_COLOR_COUNT 7

/**
 * @brief Animation state for intro splash
 */
static struct {
  _Atomic(bool) is_running;       // true while animation should continue
  _Atomic(bool) should_stop;      // set to true when first frame ready
  int frame;                      // current animation frame
  asciichat_thread_t anim_thread; // animation thread handle
} g_splash_state = {.is_running = false, .should_stop = false, .frame = 0};

// ============================================================================
// Helper Functions - TTY Detection
// ============================================================================

// ============================================================================
// Helper Functions - Rainbow Rendering
// ============================================================================

/**
 * @brief Interpolate between two RGB colors
 * @param color1 First color
 * @param color2 Second color
 * @param t Interpolation factor (0.0 = color1, 1.0 = color2)
 * @return Interpolated RGB color
 */
static rgb_color_t interpolate_color(rgb_color_t color1, rgb_color_t color2, double t) {
  rgb_color_t result;
  result.r = (uint8_t)(color1.r * (1.0 - t) + color2.r * t);
  result.g = (uint8_t)(color1.g * (1.0 - t) + color2.g * t);
  result.b = (uint8_t)(color1.b * (1.0 - t) + color2.b * t);
  return result;
}

/**
 * @brief Get RGB color for a position in the rainbow
 * @param position Position in the rainbow (0.0 to 1.0 or beyond for cycling)
 * @return RGB color at that position
 */
static rgb_color_t get_rainbow_color_rgb(double position) {
  // Normalize position to 0-1 range
  double norm_pos = position - (long)position;
  if (norm_pos < 0) {
    norm_pos += 1.0;
  }

  // Scale position to color range
  double color_pos = norm_pos * (RAINBOW_COLOR_COUNT - 1);
  int color_idx = (int)color_pos;
  double blend = color_pos - color_idx;

  // Wrap around at the end
  if (color_idx >= RAINBOW_COLOR_COUNT - 1) {
    color_idx = RAINBOW_COLOR_COUNT - 1;
    blend = 0;
  }

  int next_idx = (color_idx + 1) % RAINBOW_COLOR_COUNT;

  return interpolate_color(g_rainbow_colors[color_idx], g_rainbow_colors[next_idx], blend);
}

// ============================================================================
// Public API
// ============================================================================

bool splash_should_display(bool is_intro) {
  // Check option flags (display splash if enabled, regardless of TTY for testing)
  if (is_intro) {
    // Allow splash in snapshot mode if loading from URL/file (has loading time)
    // Skip splash only for quick webcam snapshots
    bool is_snapshot = GET_OPTION(snapshot_mode);
    bool has_media = (GET_OPTION(media_url) && strlen(GET_OPTION(media_url)) > 0) ||
                     (GET_OPTION(media_file) && strlen(GET_OPTION(media_file)) > 0);

    // Show splash if:
    // 1. Not in snapshot mode, OR
    // 2. In snapshot mode but loading from URL/file (needs splash during load)
    return GET_OPTION(splash) && (!is_snapshot || has_media);
  } else {
    return GET_OPTION(status_screen);
  }
}

/**
 * @brief Build splash screen buffer with centered content
 * @param buffer Output buffer
 * @param buf_size Buffer size
 * @param width Terminal width
 * @param height Terminal height
 * @return Number of bytes written
 */
static size_t build_splash_buffer(char *buffer, size_t buf_size, int width, int height) {
  if (!buffer || buf_size < 100) {
    return 0;
  }

  // Build the splash at TOP of screen (no vertical centering)
  // This keeps the ASCII art fixed like the status screen header
  (void)height; // Not used - splash is fixed at top, not centered

  char *p = buffer;
  size_t remaining = buf_size;

  // ASCII logo from help (same as --help output)
  const char *ascii_logo[4] = {
      "  __ _ ___  ___(_|_)       ___| |__   __ _| |_ ", " / _` / __|/ __| | |_____ / __| '_ \\ / _` | __| ",
      "| (_| \\__ \\ (__| | |_____| (__| | | | (_| | |_ ", " \\__,_|___/\\___|_|_|      \\___|_| |_|\\__,_|\\__| "};

  // Center each logo line
  for (int i = 0; i < 4; i++) {
    int logo_width = 52;
    int horiz_pad = (width - logo_width) / 2;
    if (horiz_pad > 0 && remaining > (size_t)horiz_pad + (size_t)logo_width + 1) {
      for (int j = 0; j < horiz_pad; j++) {
        *p++ = ' ';
      }
      int n = snprintf(p, remaining, "%s\n", ascii_logo[i]);
      if (n > 0) {
        p += n;
        remaining -= n;
      }
    }
  }

  // Blank line
  if (remaining > 1) {
    *p++ = '\n';
    remaining--;
  }

  // Tagline centered
  const char *tagline = "Video chat in your terminal";
  int tagline_width = (int)strlen(tagline);
  int tagline_pad = (width - tagline_width) / 2;
  if (tagline_pad > 0 && remaining > (size_t)tagline_pad + (size_t)tagline_width + 1) {
    for (int j = 0; j < tagline_pad; j++) {
      *p++ = ' ';
    }
    int n = snprintf(p, remaining, "%s\n", tagline);
    if (n > 0) {
      p += n;
      remaining -= n;
    }
  }

  // Ensure null termination
  if (remaining > 0) {
    *p = '\0';
  } else {
    buffer[buf_size - 1] = '\0';
  }

  return p - buffer;
}

// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_splash_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_splash_last_term_size_check_us = 0;
#define SPLASH_TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second

/**
 * @brief Animation thread that displays splash with rainbow wave effect and logs
 * Follows the same pattern as server_status_display() for consistent behavior
 */
static void *splash_animation_thread(void *arg) {
  (void)arg;

  // ASCII logo lines (same as help output)
  const char *ascii_logo[4] = {
      "  __ _ ___  ___(_|_)       ___| |__   __ _| |_ ", " / _` / __|/ __| | |_____ / __| '_ \\ / _` | __| ",
      "| (_| \\__ \\ (__| | |_____| (__| | | | (_| | |_ ", " \\__,_|___/\\___|_|_|      \\___|_| |_|\\__,_|\\__| "};
  const char *tagline = "Video chat in your terminal";
  const int logo_width = 52;

  // Check if colors should be used (TTY check)
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);

  // Animate with rainbow wave effect
  int frame = 0;
  const int anim_speed = 100;        // milliseconds per frame
  const double rainbow_speed = 0.01; // Characters per frame of wave speed

  while (!atomic_load(&g_splash_state.should_stop)) {
    // Get terminal dimensions (cached to avoid flooding logs with errors)
    // This matches server_status.c lines 94-103
    uint64_t now_us = platform_get_monotonic_time_us();
    if (now_us - g_splash_last_term_size_check_us > SPLASH_TERM_SIZE_CHECK_INTERVAL_US) {
      terminal_size_t temp_size;
      if (terminal_get_size(&temp_size) == ASCIICHAT_OK) {
        g_splash_cached_term_size = temp_size;
      }
      g_splash_last_term_size_check_us = now_us;
    }
    terminal_size_t term_size = g_splash_cached_term_size;
    // Clear screen and move to home position on BOTH stdout and stderr
    // (logs may have been written to either stream before splash started)
    // This matches server_status.c lines 114-116
    fprintf(stderr, "\033[H\033[2J");
    fflush(stderr);
    printf("\033[H\033[2J");

    // ========================================================================
    // SPLASH SCREEN - EXACTLY 8 LINES (fixed height, never scrolls terminal)
    // ========================================================================
    // Calculate rainbow offset for this frame (smooth continuous wave)
    double offset = frame * rainbow_speed;

    // Line 1: Top border
    printf("\033[1;36m━");
    for (int i = 1; i < term_size.cols - 1; i++) {
      printf("━");
    }
    printf("\033[0m\n");

    // Lines 2-5: ASCII logo (centered, truncated if too long)
    for (int logo_line = 0; logo_line < 4; logo_line++) {
      // Build plain text line first (for width calculation)
      char plain_line[512];
      int horiz_pad = (term_size.cols - logo_width) / 2;
      if (horiz_pad < 0) {
        horiz_pad = 0;
      }

      int pos = 0;
      for (int j = 0; j < horiz_pad && pos < (int)sizeof(plain_line) - 1; j++) {
        plain_line[pos++] = ' ';
      }
      snprintf(plain_line + pos, sizeof(plain_line) - pos, "%s", ascii_logo[logo_line]);

      // Check visible width and truncate if needed
      int visible_width = display_width(plain_line);
      if (visible_width < 0) {
        visible_width = (int)strlen(plain_line);
      }
      if (term_size.cols > 0 && visible_width >= term_size.cols) {
        plain_line[term_size.cols - 1] = '\0';
      }

      // Print with rainbow colors
      int char_idx = 0;
      for (int i = 0; plain_line[i] != '\0'; i++) {
        char ch = plain_line[i];
        if (ch == ' ') {
          printf(" ");
        } else if (use_colors) {
          double char_pos = (frame * 52 + char_idx + offset) / 30.0;
          rgb_color_t color = get_rainbow_color_rgb(char_pos);
          printf("\x1b[38;2;%u;%u;%um%c\x1b[0m", color.r, color.g, color.b, ch);
          char_idx++;
        } else {
          printf("%c", ch);
          char_idx++;
        }
      }
      printf("\n");
    }

    // Line 6: Blank line
    printf("\n");

    // Line 7: Tagline (centered, truncated if too long)
    char plain_tagline[512];
    int tagline_len = (int)strlen(tagline);
    int tagline_pad = (term_size.cols - tagline_len) / 2;
    if (tagline_pad < 0) {
      tagline_pad = 0;
    }

    int tpos = 0;
    for (int j = 0; j < tagline_pad && tpos < (int)sizeof(plain_tagline) - 1; j++) {
      plain_tagline[tpos++] = ' ';
    }
    snprintf(plain_tagline + tpos, sizeof(plain_tagline) - tpos, "%s", tagline);

    // Check visible width and truncate if needed
    int tagline_visible_width = display_width(plain_tagline);
    if (tagline_visible_width < 0) {
      tagline_visible_width = (int)strlen(plain_tagline);
    }
    if (term_size.cols > 0 && tagline_visible_width >= term_size.cols) {
      plain_tagline[term_size.cols - 1] = '\0';
    }

    printf("%s\n", plain_tagline);

    // Line 8: Bottom border
    printf("\033[1;36m━");
    for (int i = 1; i < term_size.cols - 1; i++) {
      printf("━");
    }
    printf("\033[0m\n");

    // ========================================================================
    // LIVE LOG FEED - Fills remaining screen (never causes scroll)
    // ========================================================================
    // Calculate EXACTLY how many lines we have for logs
    // Splash took exactly 8 lines above, reserve 1 line to prevent cursor scroll
    // This matches server_status.c line 166
    int logs_available_lines = term_size.rows - 9;
    if (logs_available_lines < 1) {
      logs_available_lines = 1; // At least show something
    }

    // Get recent log entries
    session_log_entry_t logs[SESSION_LOG_BUFFER_SIZE];
    size_t log_count = session_log_buffer_get_recent(logs, SESSION_LOG_BUFFER_SIZE);

    // Calculate actual display lines needed for each log (accounting for wrapping and newlines)
    // Work backwards from most recent logs until we fill available lines
    // This matches server_status.c lines 175-248
    int lines_used = 0;
    size_t start_idx = log_count; // Start past the end, will work backwards

    for (size_t i = log_count; i > 0; i--) {
      size_t idx = i - 1;
      const char *msg = logs[idx].message;

      // Count display lines for this message (newlines + wrapping)
      // Split message by newlines and calculate visible width of each line
      int msg_lines = 0;
      const char *line_start = msg;
      const char *p = msg;

      while (*p) {
        if (*p == '\n') {
          // Calculate visible width of this line (excluding ANSI codes)
          size_t line_len = p - line_start;
          char line_buf[2048];
          if (line_len < sizeof(line_buf)) {
            memcpy(line_buf, line_start, line_len);
            line_buf[line_len] = '\0';

            int visible_width = display_width(line_buf);
            if (visible_width < 0)
              visible_width = (int)line_len; // Fallback

            // Calculate how many terminal lines this takes (with wrapping)
            if (term_size.cols > 0 && visible_width > 0) {
              msg_lines += (visible_width + term_size.cols - 1) / term_size.cols;
            } else {
              msg_lines += 1;
            }
          } else {
            msg_lines += 1; // Line too long, just count as 1
          }

          line_start = p + 1;
        }
        p++;
      }

      // Handle final line if message doesn't end with newline
      if (line_start < p) {
        size_t line_len = p - line_start;
        char line_buf[2048];
        if (line_len < sizeof(line_buf)) {
          memcpy(line_buf, line_start, line_len);
          line_buf[line_len] = '\0';

          int visible_width = display_width(line_buf);
          if (visible_width < 0)
            visible_width = (int)line_len; // Fallback

          // Calculate how many terminal lines this takes (with wrapping)
          if (term_size.cols > 0 && visible_width > 0) {
            msg_lines += (visible_width + term_size.cols - 1) / term_size.cols;
          } else {
            msg_lines += 1;
          }
        } else {
          msg_lines += 1;
        }
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

    // Move to next frame
    frame++;

    // Sleep to control animation speed
    platform_sleep_ms(anim_speed);
  }

  atomic_store(&g_splash_state.is_running, false);
  return NULL;
}

int splash_intro_start(session_display_ctx_t *ctx) {
  (void)ctx; // Parameter not used currently

  // Pre-checks
  if (!splash_should_display(true)) {
    return 0; // ASCIICHAT_OK equivalent
  }

  // Check terminal size
  int width = GET_OPTION(width);
  int height = GET_OPTION(height);
  if (width < 50 || height < 20) {
    return 0;
  }

  // Initialize log buffer for capturing logs during animation
  if (!session_log_buffer_init()) {
    log_warn("Failed to initialize splash log buffer");
    return 0;
  }

  // Clear screen
  terminal_clear_screen();
  fflush(stdout);

  // Set running flag
  atomic_store(&g_splash_state.is_running, true);
  atomic_store(&g_splash_state.should_stop, false);
  g_splash_state.frame = 0;

  // Start animation thread
  if (asciichat_thread_create(&g_splash_state.anim_thread, splash_animation_thread, NULL) != ASCIICHAT_OK) {
    log_warn("Failed to create splash animation thread");
    return 0;
  }

  return 0; // ASCIICHAT_OK
}

int splash_intro_done(void) {
  // Signal animation thread to stop
  atomic_store(&g_splash_state.should_stop, true);

  // Wait for animation thread to finish
  if (atomic_load(&g_splash_state.is_running)) {
    asciichat_thread_join(&g_splash_state.anim_thread, NULL);
  }

  atomic_store(&g_splash_state.is_running, false);

  // Don't clear screen here - first frame will do it
  // Clearing here can cause a brief scroll artifact during transition

  // NOTE: Do NOT cleanup session_log_buffer here - it may be used by status screen
  // Status screen will call session_log_buffer_cleanup() when appropriate

  return 0; // ASCIICHAT_OK
}

int splash_display_status(int mode) {
  // Pre-checks
  if (!splash_should_display(false)) {
    return 0;
  }

  // Check mode validity
  if (mode != 0 && mode != 3) { // MODE_SERVER=0, MODE_DISCOVERY_SERVICE=3
    log_error("Status screen only for server/discovery modes");
    return 1; // ERROR
  }

  // Check terminal size
  int width = GET_OPTION(width);
  int height = GET_OPTION(height);
  if (width < 50 || height < 15) {
    log_debug("Terminal too small for status screen");
    return 0;
  }

  // Get terminal capabilities for UTF-8
  bool has_utf8 = (GET_OPTION(force_utf8) >= 0);
  (void)has_utf8; // Suppress unused warning

  // Build and display status box
  terminal_clear_screen();

  char buffer[4096] = {0};
  snprintf(buffer, sizeof(buffer), "\n");

  if (mode == 0) {
    // Server mode
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Server Status\n");
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Address: %s:%d\n", GET_OPTION(address),
             GET_OPTION(port));
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Max clients: %d\n", GET_OPTION(max_clients));
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Encryption: %s\n",
             GET_OPTION(no_encrypt) ? "Disabled" : "Enabled");
  } else if (mode == 3) {
    // Discovery service mode
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Discovery Service\n");
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Address: %s:%d\n", GET_OPTION(address),
             GET_OPTION(discovery_port));
  }

  snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "\n");

  printf("%s", buffer);
  fflush(stdout);

  return 0; // ASCIICHAT_OK
}
