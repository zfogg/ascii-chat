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

#include <ascii-chat/video/anim/controller.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/ui/session.h>
#include <ascii-chat/ui/invitation.h>
#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/ui/terminal_screen.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/log/search.h>
#include "session/display.h"
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/debug/sync.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ascii-chat/atomic.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

// ============================================================================
// ASCII Art and Constants
// ============================================================================

// Global update notification (set via splash_set_update_notification)
static char g_update_notification[1024] = {0};
static mutex_t g_update_notification_mutex;
static lifecycle_t g_update_notification_lifecycle = LIFECYCLE_INIT_MUTEX(&g_update_notification_mutex);

typedef ui_discovery_view_t discovery_splash_t;

static discovery_splash_t g_discovery_splash;
static mutex_t g_discovery_splash_mutex;
static lifecycle_t g_discovery_splash_lifecycle = LIFECYCLE_INIT_MUTEX(&g_discovery_splash_mutex);

static void discovery_splash_init(void) {
  if (lifecycle_init_once(&g_discovery_splash_lifecycle)) {
    mutex_init(&g_discovery_splash_mutex, "discovery_splash");
    lifecycle_init_commit(&g_discovery_splash_lifecycle);
  }
}

void splash_set_discovery_session(const char *session, bool joining) {
  discovery_splash_init();
  mutex_lock(&g_discovery_splash_mutex);
  g_discovery_splash.enabled = true;
  g_discovery_splash.joining = joining;
  SAFE_STRNCPY(g_discovery_splash.session_string, session ? session : "", sizeof(g_discovery_splash.session_string));
  mutex_unlock(&g_discovery_splash_mutex);
}

void splash_clear_discovery_session(void) {
  discovery_splash_init();
  mutex_lock(&g_discovery_splash_mutex);
  memset(&g_discovery_splash, 0, sizeof(g_discovery_splash));
  mutex_unlock(&g_discovery_splash_mutex);
}

static discovery_splash_t discovery_splash_snapshot(void) {
  discovery_splash_init();
  mutex_lock(&g_discovery_splash_mutex);
  discovery_splash_t snapshot = g_discovery_splash;
  mutex_unlock(&g_discovery_splash_mutex);
  return snapshot;
}

// ============================================================================
// Log Management
// ============================================================================

void splash_log_init(void) {
  session_log_buffer_t *buf = terminal_screen_log_init();
  if (buf) {
    log_set_session_log_buffer(buf);
  }
}

void splash_log_destroy(void) {
  log_clear_session_log_buffer();
  terminal_screen_log_destroy();
}

void splash_log_clear(void) {
  terminal_screen_log_clear();
}

/**
 * @brief Animation state for intro splash
 */
static struct {
  atomic_t is_running;               // true while animation should continue
  atomic_t should_stop;              // set to true when splash should stop
  atomic_t thread_created;           // true if monitor thread was successfully created
  asciichat_thread_t monitor_thread; // monitor thread handle
  uint64_t start_time_ns;            // when splash was started (for minimum display time)
  atomic_t intro_done_time_ns;       // when splash_intro_done() was called
} g_splash_state = {
    .is_running = {0}, .should_stop = {0}, .thread_created = {0}, .start_time_ns = 0, .intro_done_time_ns = {0}};

// ============================================================================
// Header Rendering (callback for terminal_screen)
// ============================================================================

/**
 * @brief Context data for splash header rendering
 */
typedef ui_splash_view_t splash_header_ctx_t;

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

    // NOLINTNEXTLINE(bugprone-branch-clone)
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
 * @param buf Frame buffer to write into
 * @param term_size Current terminal dimensions
 * @param user_data Pointer to splash_header_ctx_t
 */
// ============================================================================
// Public API
// ============================================================================

bool splash_should_display(bool is_intro) {
  // Check option flags (display splash if enabled, regardless of TTY for testing)
  if (is_intro) {
    // Allow splash in snapshot mode if loading from URL/file (has loading time)
    // Skip splash only for quick webcam snapshots (unless explicitly enabled)
    bool splash_screen_opt = GET_OPTION(splash_screen);
    bool splash_explicitly_set = GET_OPTION(splash_screen_explicitly_set);
    bool is_snapshot = GET_OPTION(snapshot_mode);
    bool has_media = (GET_OPTION(media_url) && strlen(GET_OPTION(media_url)) > 0) ||
                     (GET_OPTION(media_file) && strlen(GET_OPTION(media_file)) > 0);

    // Show splash if enabled, but in snapshot mode only if:
    // 1. Has media file/URL, OR
    // 2. Explicitly enabled with --splash-screen=true
    bool should_display = splash_screen_opt && (!is_snapshot || has_media || splash_explicitly_set);
    // Show splash if:
    // 1. Not in snapshot mode, OR
    // 2. In snapshot mode but loading from URL/file (needs splash during load)
    return should_display;
  } else {
    return GET_OPTION(status_screen);
  }
}

/**
 * @brief Bind native splash metadata to the common presentation clock
 */
static void render_splash_header(frame_buffer_t *buffer, terminal_size_t size, void *data) {
  splash_header_ctx_t view = *(splash_header_ctx_t *)data;
  const animation_sample_t *animation = ui_controller_animation_sample();
  if (animation)
    view.animation = *animation;
  ui_session_splash_render(buffer, size, &view);
}

static void splash_help(terminal_size_t size, const void *snapshot) {
  (void)snapshot;
  terminal_size_t minimum;
  char *text = ui_session_help_render(size, false, false, &minimum);
  if (text) {
    ui_controller_write(STDOUT_FILENO, text, strlen(text));
    SAFE_FREE(text);
  }
}

static void *splash_monitor_thread(void *arg) {
  (void)arg;
  bool keyboard_enabled = terminal_is_interactive();
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);
  splash_header_ctx_t previous = {0};
  bool published = false;
  while (!atomic_load_bool(&g_splash_state.should_stop) && !shutdown_is_requested()) {
    uint64_t now = time_get_ns();
    if (atomic_load_u64(&g_splash_state.intro_done_time_ns) && now - g_splash_state.start_time_ns >= 2 * NS_PER_SEC_INT)
      break;
    if (keyboard_enabled) {
      keyboard_key_t key = ui_input_read_key(UI_SCREEN_SPLASH);
      if (discovery_splash_snapshot().enabled && key != ']' && key != KEY_QUESTION &&
          ui_controller_current_screen() != UI_SCREEN_LOGS)
        key = KEY_NONE;
      if (ui_controller_current_screen() == UI_SCREEN_LOGS && ui_session_logs_key(key)) {
        // Search consumed the key.
      } else if (key == ']') {
        ui_controller_toggle_logs();
      } else if (key == KEY_QUESTION || (key == KEY_ESCAPE && ui_controller_current_screen() == UI_SCREEN_HELP)) {
        if (ui_controller_current_screen() == UI_SCREEN_HELP) {
          ui_controller_remove(UI_SCREEN_HELP);
        } else {
          terminal_size_t minimum;
          char *text = ui_session_help_render(ui_controller_size(), false, false, &minimum);
          if (text) {
            const bool snapshot = true;
            ui_controller_submit(UI_SCREEN_HELP, STDOUT_FILENO, minimum, splash_help, &snapshot, sizeof(snapshot));
            SAFE_FREE(text);
          }
        }
      } else if (key == KEY_ESCAPE && ui_controller_current_screen() == UI_SCREEN_LOGS) {
        ui_controller_hide_logs();
      } else if (key == KEY_ESCAPE) {
        if (log_search_is_active())
          log_search_exit_mode(false);
        else
          break;
      }
    }
    splash_header_ctx_t view = {.discovery = discovery_splash_snapshot(), .use_colors = use_colors};
    if (lifecycle_init_once(&g_update_notification_lifecycle)) {
      mutex_init(&g_update_notification_mutex, "update_notification");
      lifecycle_init_commit(&g_update_notification_lifecycle);
    }
    mutex_lock(&g_update_notification_mutex);
    SAFE_STRNCPY(view.update_notification, g_update_notification, sizeof(view.update_notification));
    mutex_unlock(&g_update_notification_mutex);
    build_connection_target(view.connection_target, sizeof(view.connection_target));
    bool should_render = terminal_is_interactive() || GET_OPTION(splash_screen_explicitly_set);
    // Only publish model changes. The presentation runtime owns the animation
    // clock while this monitor handles startup and input.
    if (should_render && (!published || memcmp(&view, &previous, sizeof(view)))) {
      terminal_screen_config_t config = {
          .screen = UI_SCREEN_SPLASH,
          .output_fd = platform_isatty(STDOUT_FILENO) ? STDOUT_FILENO : STDERR_FILENO,
          .user_data_size = sizeof(view),
          .minimum_cols = view.discovery.enabled ? 2 : 52,
          .fixed_header_lines = view.discovery.enabled ? 0 : (view.update_notification[0] ? 9 : 8),
          .render_header = render_splash_header,
          .user_data = &view,
          .show_logs = false,
          .hide_cursor = view.discovery.enabled,
      };
      terminal_screen_render(&config);
      previous = view;
      published = true;
    }
    platform_sleep_ms(16);
  }
  ui_controller_remove(UI_SCREEN_SPLASH);
  ui_controller_remove(UI_SCREEN_HELP);
  atomic_store_bool(&g_splash_state.is_running, false);
  return NULL;
}

int splash_intro_start(session_display_ctx_t *ctx) {
  (void)ctx; // Not needed - display context is not actively used

  // Check if splash should display
  if (!splash_should_display(true)) {
    return 0;
  }

  discovery_splash_t discovery = discovery_splash_snapshot();
  if (discovery.enabled && (!terminal_is_interactive() || !terminal_is_stdout_tty() || GET_OPTION(snapshot_mode))) {
    return 0;
  }
  if (atomic_load_bool(&g_splash_state.thread_created))
    return 0;

  // Redirect splash screen to stderr when stdout is piped (not a TTY).
  // This keeps stdout clean for frame data while splash goes to stderr.
  // Uses platform_isatty() so non-desktop platforms (iOS, WASM) can override.
  if (!platform_isatty(STDOUT_FILENO)) {
    terminal_screen_set_output_fd(STDERR_FILENO);
  }

  // Initialize log buffer (same pattern as server_status)
  splash_log_init();

  // Clear log buffer for clean slate
  splash_log_clear();

  // Set running flag
  atomic_store_bool(&g_splash_state.is_running, true);
  atomic_store_bool(&g_splash_state.should_stop, false);
  atomic_store_u64(&g_splash_state.intro_done_time_ns, 0);
  g_splash_state.start_time_ns = time_get_ns();

  // Start monitor thread (only create once)
  bool expected_false = false;
  if (!atomic_cas_bool(&g_splash_state.thread_created, &expected_false, true)) {
    return 0;
  }

  int err = acthread_create(&g_splash_state.monitor_thread, "splash_monitor", splash_monitor_thread, NULL);
  if (err != ASCIICHAT_OK) {
    log_warn("Failed to create splash monitor thread: error=%d", err);
    atomic_store_bool(&g_splash_state.thread_created, false);
    atomic_store_bool(&g_splash_state.is_running, false);
    return 0;
  }

  return 0; // ASCIICHAT_OK
}

int splash_intro_done(void) {
  atomic_store_u64(&g_splash_state.intro_done_time_ns, time_get_ns());
  if (discovery_splash_snapshot().enabled || (GET_OPTION(snapshot_mode) && GET_OPTION(snapshot_delay) == 0.0))
    atomic_store_bool(&g_splash_state.should_stop, true);
  // The monitor retires the splash snapshot before clearing is_running. Keep
  // thread_created set until splash_wait_for_animation() joins that monitor.

  return 0;
}

void splash_restore_stderr(void) {
  // No-op: stderr suppression is no longer used
  // Logging works naturally through session_log_buffer
}

void splash_clear_display_context(void) {
  // No-op: display context is no longer cached
}

bool splash_is_running(void) {
  // Check if splash animation is currently rendering
  // Used to prevent ASCII art from rendering while splash is animating
  return atomic_load_bool(&g_splash_state.is_running);
}

void splash_wait_for_animation(void) {
  if (!atomic_load_bool(&g_splash_state.thread_created))
    return;

  // Keep terminal ownership until the writer finishes, but allow shutdown to
  // proceed if an undrained output pipe blocks the monitor thread. Polling
  // also handles shutdown requested after the normal handoff has begun, and
  // avoids platforms whose timed thread join falls back to a blocking join.
  uint64_t shutdown_started_ns = 0;
  while (atomic_load_bool(&g_splash_state.is_running)) {
    if (shutdown_is_requested()) {
      atomic_store_bool(&g_splash_state.should_stop, true);
      uint64_t now = time_get_ns();
      if (shutdown_started_ns == 0)
        shutdown_started_ns = now;
      if (now - shutdown_started_ns >= 100 * NS_PER_MS_INT)
        return;
    }
    platform_sleep_ms(10);
  }

  // The monitor clears is_running after retiring its presentation snapshot.
  asciichat_error_t err = thread_join(&g_splash_state.monitor_thread, NULL);
  if (err != ASCIICHAT_OK) {
    log_warn("Splash animation join failed: %s", asciichat_error_format(NULL, err));
    return;
  }
  atomic_store_bool(&g_splash_state.thread_created, false);
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
