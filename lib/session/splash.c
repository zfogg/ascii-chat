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

  // Calculate vertical padding for center
  int logo_height = 4;
  int tagline_height = 1;
  int content_height = logo_height + tagline_height + 1; // +1 for blank line
  int vert_pad = (height - content_height) / 2;
  if (vert_pad < 0)
    vert_pad = 0;

  // Build the splash with centered content
  char *p = buffer;
  size_t remaining = buf_size;

  // Add vertical padding
  for (int i = 0; i < vert_pad && remaining > 1; i++) {
    *p++ = '\n';
    remaining--;
  }

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

/**
 * @brief Animation thread that displays splash with rainbow wave effect
 */
static void *splash_animation_thread(void *arg) {
  (void)arg;

  int width = GET_OPTION(width);
  int height = GET_OPTION(height);
  char base_buffer[2048];

  // Build splash buffer once
  build_splash_buffer(base_buffer, sizeof(base_buffer), width, height);

  // Check if colors should be used (TTY check)
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);

  // Animate with rainbow wave effect
  int frame = 0;
  const int anim_speed = 100;        // milliseconds per frame
  const double rainbow_speed = 0.01; // Characters per frame of wave speed

  while (!atomic_load(&g_splash_state.should_stop)) {
    // Clear screen
    terminal_clear_screen();

    // Calculate rainbow offset for this frame (smooth continuous wave)
    double offset = frame * rainbow_speed;

    // Print splash with rainbow wave effect
    // Color each character based on its position + offset
    for (int i = 0; base_buffer[i] != '\0'; i++) {
      char ch = base_buffer[i];

      if (ch == '\n') {
        // Print newlines as-is
        printf("%c", ch);
      } else if (ch == ' ') {
        // Spaces don't need color, just print
        printf("%c", ch);
      } else if (use_colors) {
        // Calculate smooth rainbow color for this character
        // Position is normalized by spreading over many characters for smooth fade
        double char_pos = (i + offset) / 30.0; // Spread over 30 chars for smooth transition
        rgb_color_t color = get_rainbow_color_rgb(char_pos);

        // Print character with truecolor ANSI escape: \x1b[38;2;R;G;Bm
        printf("\x1b[38;2;%u;%u;%um%c\x1b[0m", color.r, color.g, color.b, ch);
      } else {
        // No colors when piping - just print the character
        printf("%c", ch);
      }
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

  // Clear screen for next render
  terminal_clear_screen();

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
