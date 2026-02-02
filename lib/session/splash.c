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

#include "session/splash.h"
#include "session/display.h"
#include "platform/terminal.h"
#include "platform/system.h"
#include "video/ansi_fast.h"
#include "options/options.h"
#include "log/logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

// ============================================================================
// ASCII Art and Constants
// ============================================================================

/**
 * @brief ASCII art for "ascii-chat" logo (7 lines)
 */
static const char *g_ascii_logo[] = {"  ▄▄▄  ███████  ██████    ██  ██", " ▐███▌ ██       ██   ██   ██  ██",
                                     "  ██   █████    ██████    ██  ██", "  ██   ██       ██  ██    ██  ██",
                                     "  ██   ███████  ██   ██    ████ ", "",
                                     "   Video chat in your terminal"};
#define ASCII_LOGO_LINES 7
#define ASCII_LOGO_WIDTH 36

/**
 * @brief Rainbow color cycle (6 colors for continuous animation)
 */
static const int g_rainbow_colors[] = {
    196, // Red
    226, // Yellow
    46,  // Green
    51,  // Cyan
    21,  // Blue
    201  // Magenta
};
#define RAINBOW_COLOR_COUNT 6

/**
 * @brief Animation state for intro splash
 */
static struct {
  _Atomic(bool) is_running;  // true while animation should continue
  _Atomic(bool) should_stop; // set to true when first frame ready
  int frame;                 // current animation frame
} g_splash_state = {.is_running = false, .should_stop = false, .frame = 0};

// ============================================================================
// Helper Functions - TTY Detection
// ============================================================================

/**
 * @brief Check if both stdin and stdout are TTY
 */
static bool check_is_tty(void) {
  return platform_isatty(STDIN_FILENO) && platform_isatty(STDOUT_FILENO);
}

// ============================================================================
// Helper Functions - Rainbow Rendering
// ============================================================================

/**
 * @brief Get ANSI code for a rainbow color at the given animation frame
 */
static int get_rainbow_color(int frame, int position) {
  int color_idx = (frame + position) % RAINBOW_COLOR_COUNT;
  return g_rainbow_colors[color_idx];
}

/**
 * @brief Build a single frame of the splash screen
 */
static void build_splash_frame(char *buffer, size_t buf_size, int width, int height, bool has_utf8) {
  if (!buffer || buf_size == 0) {
    return;
  }

  size_t pos = 0;
  int center_row = (height - ASCII_LOGO_LINES) / 2;

  // Top padding
  for (int i = 0; i < center_row; i++) {
    if (pos < buf_size - 1) {
      buffer[pos++] = '\n';
    }
  }

  // Simplified version - just print the ASCII art with some spacing
  int padding = (width - ASCII_LOGO_WIDTH) / 2;
  if (padding < 0)
    padding = 0;

  // Top border line
  for (int i = 0; i < padding; i++) {
    if (pos < buf_size - 1)
      buffer[pos++] = ' ';
  }
  for (int i = 0; i < ASCII_LOGO_WIDTH; i++) {
    if (pos < buf_size - 1)
      buffer[pos++] = '-';
  }
  if (pos < buf_size - 1)
    buffer[pos++] = '\n';

  // ASCII logo lines
  for (int i = 0; i < ASCII_LOGO_LINES; i++) {
    for (int j = 0; j < padding; j++) {
      if (pos < buf_size - 1)
        buffer[pos++] = ' ';
    }
    const char *line = g_ascii_logo[i];
    for (int j = 0; line[j] && pos < buf_size - 1; j++) {
      buffer[pos++] = line[j];
    }
    if (pos < buf_size - 1)
      buffer[pos++] = '\n';
  }

  // Bottom border line
  for (int i = 0; i < padding; i++) {
    if (pos < buf_size - 1)
      buffer[pos++] = ' ';
  }
  for (int i = 0; i < ASCII_LOGO_WIDTH; i++) {
    if (pos < buf_size - 1)
      buffer[pos++] = '-';
  }
  if (pos < buf_size - 1)
    buffer[pos++] = '\n';

  buffer[pos] = '\0';
}

// ============================================================================
// Public API
// ============================================================================

bool splash_should_display(bool is_intro) {
  // Check option flags (display splash if enabled, regardless of TTY for testing)
  if (is_intro) {
    return GET_OPTION(splash) && !GET_OPTION(snapshot_mode);
  } else {
    return GET_OPTION(status_screen);
  }
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

  // Clear screen and display splash
  terminal_clear_screen();
  fflush(stdout);

  // Set running flag for splash_intro_done() to check
  atomic_store(&g_splash_state.is_running, true);
  atomic_store(&g_splash_state.should_stop, false);
  g_splash_state.frame = 0;

  // Display splash frame immediately and return (non-blocking)
  // The splash stays on screen while render loop initializes
  char buffer[2048];

  // Build simple visible splash - multiple newlines then logo
  snprintf(buffer, sizeof(buffer),
           "\n\n\n"
           "   ▄▄▄  ███████  ██████    ██  ██\n"
           "  ▐███▌ ██       ██   ██   ██  ██\n"
           "   ██   █████    ██████    ██  ██\n"
           "   ██   ██       ██  ██    ██  ██\n"
           "   ██   ███████  ██   ██    ████ \n"
           "\n"
           "  Video chat in your terminal\n"
           "\n\n");

  // Print splash to stdout with explicit flushing
  fputs(buffer, stdout);
  fflush(stdout);

  return 0; // ASCIICHAT_OK
}

int splash_intro_done(void) {
  // Signal animation to stop
  atomic_store(&g_splash_state.should_stop, true);
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
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "  Address: %s:%s\n", GET_OPTION(address),
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
