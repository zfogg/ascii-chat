/**
 * @file ui/splash.c
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

#include <ascii-chat/ui/splash.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/log/interactive_grep.h>
#include "session/display.h"
#include "session/session_log_buffer.h"
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/debug/sync.h>
#include <ascii-chat/video/image.h>
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

// Global update notification (set via splash_set_update_notification)
static char g_update_notification[1024] = {0};
static mutex_t g_update_notification_mutex;
static lifecycle_t g_update_notification_lifecycle = LIFECYCLE_INIT_MUTEX(&g_update_notification_mutex);

static const rgb_pixel_t g_rainbow_colors[] = {
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
  _Atomic(bool) is_running;           // true while animation should continue
  _Atomic(bool) should_stop;          // set to true when first frame ready
  _Atomic(bool) thread_created;       // true if animation thread was successfully created
  int frame;                          // current animation frame
  asciichat_thread_t anim_thread;     // animation thread handle
  uint64_t start_time_ns;             // when splash was started (for minimum display time)
} g_splash_state = {.is_running = false, .should_stop = false, .thread_created = false, .frame = 0, .start_time_ns = 0};

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
static rgb_pixel_t interpolate_color(rgb_pixel_t color1, rgb_pixel_t color2, double t) {
  rgb_pixel_t result;
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
static rgb_pixel_t get_rainbow_color_rgb(double position) {
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
// Header Rendering (callback for terminal_screen)
// ============================================================================

/**
 * @brief Context data for splash header rendering
 */
typedef struct {
  int frame;                      // Current animation frame number
  bool use_colors;                // Whether to use rainbow colors
  char update_notification[1024]; // Update notification message (empty if no update)
} splash_header_ctx_t;

/**
 * @brief Build connection target string for splash display
 * @param buffer Output buffer for connection string
 * @param buffer_size Size of output buffer
 */
static void build_connection_target(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return;
  }
  buffer[0] = '\0';

  // Mirror mode - no network connection
  asciichat_mode_t mode = GET_OPTION(detected_mode);
  if (mode == MODE_MIRROR) {
    const char *media_url = GET_OPTION(media_url);
    const char *media_file = GET_OPTION(media_file);

    if (media_url && media_url[0] != '\0') {
      snprintf(buffer, buffer_size, "Loading from URL...");
    } else if (media_file && media_file[0] != '\0') {
      snprintf(buffer, buffer_size, "Loading from file...");
    } else {
      snprintf(buffer, buffer_size, "Initializing...");
    }
    return;
  }

  // Check if we have a session string (discovery mode)
  const char *session = GET_OPTION(session_string);
  if (session && session[0] != '\0') {
    snprintf(buffer, buffer_size, "Connecting to session: %s", session);
    return;
  }

  // Check if we have an address (client mode)
  const char *addr = GET_OPTION(address);

  if (addr && addr[0] != '\0') {
    // Use IP utility to classify the connection type
    const char *ip_type = get_ip_type_string(addr);

    if (strcmp(ip_type, "Localhost") == 0) {
      snprintf(buffer, buffer_size, "Connecting to localhost...");
    } else if (strcmp(ip_type, "LAN") == 0) {
      snprintf(buffer, buffer_size, "Connecting to %s (LAN)", addr);
    } else if (strcmp(ip_type, "Internet") == 0) {
      snprintf(buffer, buffer_size, "Connecting to %s (Internet)", addr);
    } else if (strcmp(addr, "localhost") == 0) {
      // Hostname "localhost" not detected by IP util
      snprintf(buffer, buffer_size, "Connecting to localhost...");
    } else {
      // Unknown or hostname
      snprintf(buffer, buffer_size, "Connecting to %s", addr);
    }
    return;
  }

  // Fallback if no connection info available
  snprintf(buffer, buffer_size, "Initializing...");
}

/**
 * @brief Render splash header with rainbow animation (callback for terminal_screen)
 *
 * Renders exactly 8 lines:
 * - Line 1: Top border
 * - Lines 2-5: ASCII logo (4 lines, centered, rainbow animated)
 * - Line 6: Tagline (centered)
 * - Line 7: Connection target (centered)
 * - Line 8: Bottom border
 *
 * @param term_size Current terminal dimensions
 * @param user_data Pointer to splash_header_ctx_t
 */
static void render_splash_header(terminal_size_t term_size, void *user_data) {
  const splash_header_ctx_t *ctx = (const splash_header_ctx_t *)user_data;
  if (!ctx) {
    return;
  }

  // ASCII logo lines (same as help output)
  const char *ascii_logo[4] = {
      "  __ _ ___  ___(_|_)       ___| |__   __ _| |_ ", " / _` / __|/ __| | |_____ / __| '_ \\ / _` | __| ",
      "| (_| \\__ \\ (__| | |_____| (__| | | | (_| | |_ ", " \\__,_|___/\\___|_|_|      \\___|_| |_|\\__,_|\\__| "};
  const char *tagline = "Video chat in your terminal";
  const int logo_width = 52;
  const double rainbow_speed = 0.01; // Characters per frame of wave speed

  // Calculate rainbow offset for this frame (smooth continuous wave)
  double offset = ctx->frame * rainbow_speed;

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
      } else if (ctx->use_colors) {
        double char_pos = (ctx->frame * 52 + char_idx + offset) / 30.0;
        rgb_pixel_t color = get_rainbow_color_rgb(char_pos);
        printf("\x1b[38;2;%u;%u;%um%c\x1b[0m", color.r, color.g, color.b, ch);
        char_idx++;
      } else {
        printf("%c", ch);
        char_idx++;
      }
    }
    printf("\n");
  }

  // Line 6: Tagline (centered, truncated if too long)
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

  // Line 7: Update notification (if available)
  if (ctx->update_notification[0] != '\0') {
    char plain_update[1024];
    int update_len = (int)strlen(ctx->update_notification);
    int update_pad = (term_size.cols - update_len) / 2;
    if (update_pad < 0) {
      update_pad = 0;
    }

    int upos = 0;
    for (int j = 0; j < update_pad && upos < (int)sizeof(plain_update) - 1; j++) {
      plain_update[upos++] = ' ';
    }
    snprintf(plain_update + upos, sizeof(plain_update) - upos, "%s", ctx->update_notification);

    // Check visible width and truncate if needed
    int update_visible_width = display_width(plain_update);
    if (update_visible_width < 0) {
      update_visible_width = (int)strlen(plain_update);
    }
    if (term_size.cols > 0 && update_visible_width >= term_size.cols) {
      plain_update[term_size.cols - 1] = '\0';
    }

    // Print in yellow/warning color
    printf("%s\n", colored_string(LOG_COLOR_WARN, plain_update));
  }

  // Line 8 (or 7 if no update): Connection target (centered, truncated if too long)
  char connection_target[512];
  build_connection_target(connection_target, sizeof(connection_target));

  char plain_connection[512];
  int connection_len = (int)strlen(connection_target);
  int connection_pad = (term_size.cols - connection_len) / 2;
  if (connection_pad < 0) {
    connection_pad = 0;
  }

  int cpos = 0;
  for (int j = 0; j < connection_pad && cpos < (int)sizeof(plain_connection) - 1; j++) {
    plain_connection[cpos++] = ' ';
  }
  snprintf(plain_connection + cpos, sizeof(plain_connection) - cpos, "%s", connection_target);

  // Check visible width and truncate if needed
  int connection_visible_width = display_width(plain_connection);
  if (connection_visible_width < 0) {
    connection_visible_width = (int)strlen(plain_connection);
  }
  if (term_size.cols > 0 && connection_visible_width >= term_size.cols) {
    plain_connection[term_size.cols - 1] = '\0';
  }

  printf("%s\n", plain_connection);

  // Line 9 (or 8 if no update): Bottom border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");
}

// ============================================================================
// Public API
// ============================================================================

bool splash_should_display(bool is_intro) {
  // Check option flags (display splash if enabled, regardless of TTY for testing)
  if (is_intro) {
    // Allow splash in snapshot mode if loading from URL/file (has loading time)
    // Skip splash only for quick webcam snapshots
    bool splash_screen_opt = GET_OPTION(splash_screen);
    bool is_snapshot = GET_OPTION(snapshot_mode);
    bool has_media = (GET_OPTION(media_url) && strlen(GET_OPTION(media_url)) > 0) ||
                     (GET_OPTION(media_file) && strlen(GET_OPTION(media_file)) > 0);

    bool should_display = splash_screen_opt && (!is_snapshot || has_media);
    log_info("splash_should_display(intro): splash_screen=%d snapshot=%d has_media=%d => %d",
             splash_screen_opt, is_snapshot, has_media, should_display);
    // Show splash if:
    // 1. Not in snapshot mode, OR
    // 2. In snapshot mode but loading from URL/file (needs splash during load)
    return should_display;
  } else {
    return GET_OPTION(status_screen);
  }
}

/**
 * @brief Animation thread that displays splash with rainbow wave effect and logs
 * Uses terminal_screen abstraction for consistent rendering
 */
static void *splash_animation_thread(void *arg) {
  (void)arg;

  // Check if colors should be used (TTY check)
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);

  // Initialize keyboard for interactive grep (if terminal is interactive)
  bool keyboard_enabled = false;
  if (terminal_is_interactive()) {
    if (keyboard_init() == ASCIICHAT_OK) {
      keyboard_enabled = true;
    }
  }

  // Animate with rainbow wave effect
  int frame = 0;
  int fps = GET_OPTION(fps);
  if (fps <= 0) fps = 60; // Default to 60 FPS if not specified
  const int anim_speed = 1000 / fps; // milliseconds per frame
  uint64_t loop_start_ns = time_get_ns();

  log_info("[SPLASH_ANIM] Starting animation loop: fps=%d, anim_speed=%dms per frame", fps, anim_speed);
  while (!atomic_load(&g_splash_state.should_stop) && !shutdown_is_requested()) {
    log_debug("[SPLASH_ANIM] Frame %d: should_stop=%d, shutdown=%d", frame,
              atomic_load(&g_splash_state.should_stop), shutdown_is_requested());

    // Poll keyboard for interactive grep and Escape to cancel
    log_debug("[SPLASH_ANIM] Frame %d: keyboard_enabled=%d", frame, keyboard_enabled);
    if (keyboard_enabled) {
      keyboard_key_t key = keyboard_read_nonblocking();
      if (key == KEY_ESCAPE) {
        // Escape key: cancel grep if active, otherwise cancel splash
        if (interactive_grep_is_active()) {
          interactive_grep_exit_mode(false); // Cancel grep without applying
        } else {
          atomic_store(&g_splash_state.should_stop, true); // Exit splash screen
        }
      } else if (key != KEY_NONE && interactive_grep_should_handle(key)) {
        interactive_grep_handle_key(key);
        // Continue to render immediately with grep active
      }
    }
    log_debug("[SPLASH_ANIM] Frame %d: keyboard check done", frame);

    // Set up splash header context for this frame
    splash_header_ctx_t header_ctx = {
        .frame = frame,
        .use_colors = use_colors,
    };

    log_debug("[SPLASH_ANIM] Frame %d: Setting up header context", frame);

    // Initialize update notification lifecycle and mutex once (safe to call multiple times)
    if (lifecycle_init_once(&g_update_notification_lifecycle)) {
      log_debug("[SPLASH_ANIM] Initializing lifecycle for update_notification");
      // Initialize the mutex (already configured in the lifecycle structure)
      mutex_init(&g_update_notification_mutex, "update_notification");
      lifecycle_init_commit(&g_update_notification_lifecycle);
    }

    // Copy update notification from global state (thread-safe)
    log_debug("[SPLASH_ANIM] Frame %d: About to lock update_notification_mutex", frame);
    mutex_lock(&g_update_notification_mutex);
    log_debug("[SPLASH_ANIM] Frame %d: Locked update_notification_mutex", frame);
    SAFE_STRNCPY(header_ctx.update_notification, g_update_notification, sizeof(header_ctx.update_notification));
    mutex_unlock(&g_update_notification_mutex);
    log_debug("[SPLASH_ANIM] Frame %d: Unlocked update_notification_mutex", frame);

    // Calculate header lines: 8 base lines + 1 if update notification present
    int header_lines = 8;
    if (header_ctx.update_notification[0] != '\0') {
      header_lines = 9;
    }

    // Configure terminal screen with splash header callback
    terminal_screen_config_t screen_config = {
        .fixed_header_lines = header_lines,
        .render_header = render_splash_header,
        .user_data = &header_ctx,
        .show_logs = true, // Show live log feed below splash
    };

    // Render the screen (header + logs) only in interactive mode
    // OR if splash screen was explicitly requested
    // In non-interactive mode without explicit flag, logs flow to stdout/stderr normally
    log_debug("[SPLASH_ANIM] Frame %d: About to render", frame);
    const options_t *opts_render = options_get();
    bool should_render = terminal_is_interactive() || (opts_render && opts_render->splash_screen_explicitly_set);
    if (should_render) {
      log_debug("[SPLASH_ANIM] Frame %d: Calling terminal_screen_render()", frame);
      terminal_screen_render(&screen_config);
      log_debug("[SPLASH_ANIM] Frame %d: terminal_screen_render() completed", frame);
    } else {
      log_debug("[SPLASH_ANIM] Frame %d: Skipping render (should_render=false)", frame);
    }

    // Move to next frame
    frame++;

    // Log frame progress every 60 frames (1 second at 60fps)
    if (frame % 60 == 0) {
      uint64_t now_ns = time_get_ns();
      uint64_t elapsed_ns = now_ns - loop_start_ns;
      double elapsed_sec = elapsed_ns / 1e9;
      double actual_fps = frame / elapsed_sec;
      log_info("[SPLASH_ANIM] Frame %d: %.1f seconds elapsed, actual FPS: %.1f", frame, elapsed_sec, actual_fps);
    }

    // Sleep to control animation speed (unless grep needs immediate rerender)
    if (!interactive_grep_needs_rerender()) {
      platform_sleep_ms(anim_speed);
    }
  }

  // Log final frame count
  uint64_t final_ns = time_get_ns();
  uint64_t total_elapsed_ns = final_ns - loop_start_ns;
  double total_elapsed_sec = total_elapsed_ns / 1e9;
  double final_fps = total_elapsed_sec > 0 ? frame / total_elapsed_sec : 0;
  log_info("[SPLASH_ANIM] === ANIMATION LOOP EXITED === %d frames rendered in %.3f seconds (%.1f FPS). should_stop=%d, shutdown=%d",
           frame, total_elapsed_sec, final_fps, atomic_load(&g_splash_state.should_stop), shutdown_is_requested());

  // Cleanup keyboard
  if (keyboard_enabled) {
    log_debug("[SPLASH_ANIM] Destroying keyboard");
    keyboard_destroy();
  }

  atomic_store(&g_splash_state.is_running, false);
  log_debug("[SPLASH_ANIM] Animation thread exiting");
  return NULL;
}

int splash_intro_start(session_display_ctx_t *ctx) {
  (void)ctx; // Parameter not used currently

  log_info("splash_intro_start: ENTRY");

  // Pre-checks
  bool should_display = splash_should_display(true);
  log_info("splash_intro_start: splash_should_display(true)=%d", should_display);
  if (!should_display) {
    log_info("splash_intro_start: returning early - splash should not display");
    return 0; // ASCIICHAT_OK equivalent
  }

  // Don't initialize log buffer in non-interactive mode UNLESS explicitly requested
  bool is_interactive = terminal_is_interactive();
  const options_t *opts = options_get();
  bool splash_explicitly_set = opts && opts->splash_screen_explicitly_set;
  log_info("splash_intro_start: terminal_is_interactive()=%d, splash_explicitly_set=%d", is_interactive, splash_explicitly_set);
  if (!is_interactive && !splash_explicitly_set) {
    log_info("splash_intro_start: returning early - not interactive and splash not explicitly set");
    return 0;
  }

  // Check terminal size
  int width = GET_OPTION(width);
  int height = GET_OPTION(height);
  if (width < 50 || height < 20) {
    return 0;
  }

  // Initialize log buffer for capturing logs during animation
  log_debug("[SPLASH] About to call session_log_buffer_init()");
  if (!session_log_buffer_init()) {
    log_warn("Failed to initialize splash log buffer");
    return 0;
  }
  log_debug("[SPLASH] session_log_buffer_init() completed successfully");

  // Clear screen
  log_debug("[SPLASH] Clearing screen");
  terminal_clear_screen();
  // Show cursor for splash screen display (with logs)
  (void)terminal_cursor_show();
  fflush(stdout);

  // Set running flag
  log_debug("[SPLASH] Setting running flag and initializing state");
  atomic_store(&g_splash_state.is_running, true);
  atomic_store(&g_splash_state.should_stop, false);
  g_splash_state.frame = 0;
  g_splash_state.start_time_ns = time_get_ns();  // Track start time for minimum display duration

  // Start animation thread
  log_debug("[SPLASH] About to create animation thread");
  int err = asciichat_thread_create(&g_splash_state.anim_thread, "splash_anim", splash_animation_thread, NULL);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to create splash animation thread: error=%d", err);
    atomic_store(&g_splash_state.thread_created, false);
    return 0;
  }
  atomic_store(&g_splash_state.thread_created, true);
  log_debug("[SPLASH] Animation thread created successfully, thread_created=%d", atomic_load(&g_splash_state.thread_created));

  return 0; // ASCIICHAT_OK
}

int splash_intro_done(void) {
  log_info("[SPLASH_DONE] Entry: is_running=%d, should_stop=%d, thread_created=%d",
           atomic_load(&g_splash_state.is_running),
           atomic_load(&g_splash_state.should_stop),
           atomic_load(&g_splash_state.thread_created));

  // Enforce minimum display time of 2 seconds
  if (g_splash_state.start_time_ns > 0) {
    uint64_t now_ns = time_get_ns();
    uint64_t elapsed_ns = now_ns - g_splash_state.start_time_ns;
    const uint64_t min_display_ns = 2000000000ULL;  // 2 seconds in nanoseconds

    if (elapsed_ns < min_display_ns) {
      uint64_t remaining_ns = min_display_ns - elapsed_ns;
      uint64_t remaining_ms = (remaining_ns + 999999) / 1000000;  // Round up to ms
      log_info("[SPLASH_DONE] Elapsed: %.2f seconds, minimum: 2s. Waiting %llu ms",
               elapsed_ns / 1000000000.0, (unsigned long long)remaining_ms);
      platform_sleep_ms(remaining_ms);
    }
  }

  // Signal animation thread to stop
  log_info("[SPLASH_DONE] Setting should_stop=true");
  atomic_store(&g_splash_state.should_stop, true);

  // Wait for animation thread to finish (only join once, even if called multiple times)
  log_debug("[SPLASH_DONE] Checking if animation thread was created");
  bool expected = true;
  if (atomic_compare_exchange_strong(&g_splash_state.thread_created, &expected, false)) {
    // We successfully changed thread_created from true to false, so we join
    log_debug("[SPLASH_DONE] About to join animation thread");
    asciichat_thread_join(&g_splash_state.anim_thread, NULL);
    log_debug("[SPLASH_DONE] Animation thread join completed");
  } else {
    // Another caller already joined the thread
    log_debug("[SPLASH_DONE] Animation thread already joined or never created");
  }

  atomic_store(&g_splash_state.is_running, false);
  log_debug("[SPLASH_DONE] Exit");

  // Don't clear screen here - first frame will do it
  // Clearing here can cause a brief scroll artifact during transition

  // NOTE: Do NOT cleanup session_log_buffer here - it may be used by status screen
  // Status screen will call session_log_buffer_destroy() when appropriate

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

  // Add update notification if available
  if (lifecycle_init_once(&g_update_notification_lifecycle)) {
    mutex_init(&g_update_notification_mutex, "update_notification");
    lifecycle_init_commit(&g_update_notification_lifecycle);
  }
  mutex_lock(&g_update_notification_mutex);
  if (g_update_notification[0] != '\0') {
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "\n  %s\n",
             colored_string(LOG_COLOR_WARN, g_update_notification));
  }
  mutex_unlock(&g_update_notification_mutex);

  snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "\n");

  printf("%s", buffer);
  fflush(stdout);

  return 0; // ASCIICHAT_OK
}

void splash_set_update_notification(const char *notification) {
  if (lifecycle_init_once(&g_update_notification_lifecycle)) {
    mutex_init(&g_update_notification_mutex, "update_notification");
    lifecycle_init_commit(&g_update_notification_lifecycle);
  }
  mutex_lock(&g_update_notification_mutex);

  if (!notification || notification[0] == '\0') {
    g_update_notification[0] = '\0';
    log_debug("Cleared update notification for splash/status screens");
  } else {
    SAFE_STRNCPY(g_update_notification, notification, sizeof(g_update_notification));
    log_debug("Set update notification for splash/status screens: %s", notification);
  }

  mutex_unlock(&g_update_notification_mutex);
}
