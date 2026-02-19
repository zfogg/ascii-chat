/**
 * @file client/display.c
 * @ingroup client_display
 * @brief 💻 Client terminal display: TTY detection, frame rendering, and interactive/stdout output routing
 *
 * The display system supports dual output modes:
 * - **Interactive TTY Mode**: Direct terminal control with cursor positioning
 * - **Redirect Mode**: Plain text output for pipes and file redirection
 * - **Snapshot Mode**: Single frame capture with minimal terminal control
 *
 * ## TTY Detection and Management
 *
 * Implements robust TTY detection across platforms:
 * 1. **Environment Variables**: Check $TTY for explicit terminal path
 * 2. **Standard Streams**: Test stdin/stdout/stderr for TTY status
 * 3. **Controlling Terminal**: Fall back to /dev/tty (Unix) or CON (Windows)
 * 4. **Validation**: Verify TTY path accessibility and permissions
 *
 * ## Terminal Control Sequences
 *
 * Uses platform abstraction layer for terminal operations:
 * - **Initialization**: Set terminal to optimal display mode
 * - **Cursor Management**: Hide cursor and position for frame updates
 * - **Screen Control**: Clear screen and scrollback buffer
 * - **Reset Operations**: Restore terminal to original state
 *
 * ## Frame Rendering Pipeline
 *
 * Frame display follows a structured pipeline:
 * 1. **Mode Detection**: Determine output mode (TTY vs redirect)
 * 2. **Cursor Positioning**: Position cursor for frame update (TTY mode)
 * 3. **Data Writing**: Write frame data to appropriate file descriptor
 * 4. **Synchronization**: Ensure data reaches terminal (fsync for redirect)
 * 5. **State Updates**: Track frame dimensions and display state
 *
 * ## Snapshot Mode Support
 *
 * Special handling for single-frame capture:
 * - **Timing Control**: Coordinate with protocol for snapshot timing
 * - **Output Routing**: Final frame written to both TTY and stdout
 * - **Format Control**: Skip terminal control sequences in snapshot output
 * - **Cleanup**: Add newline terminator for proper file formatting
 *
 * ## Platform Compatibility
 *
 * Cross-platform terminal support:
 * - **Unix/Linux**: Uses /dev/tty and POSIX terminal I/O
 * - **macOS**: Enhanced TTY detection with $TTY environment variable
 * - **Windows**: CON device for console output with Windows Console API
 * - **Error Handling**: Graceful fallback for unsupported operations
 *
 * ## Integration Points
 *
 * - **main.c**: Display subsystem initialization and lifecycle management
 * - **protocol.c**: Frame data reception and rendering requests
 * - **server.c**: Terminal capability reporting and resize handling
 * - **options.c**: Display mode configuration from command line
 *
 * ## Error Handling
 *
 * Display errors handled with graceful degradation:
 * - **TTY Access Errors**: Fall back to stdout redirection mode
 * - **Write Errors**: Log errors but continue processing
 * - **Terminal Control Errors**: Skip control sequences, continue with data
 * - **Permission Errors**: Try alternative TTY paths before failing
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0
 */

#include "display.h"
#include "main.h"
#include "../main.h" // Global exit API

#include <ascii-chat/session/display.h>
#include <ascii-chat/session/capture.h>
#include <ascii-chat/session/keyboard_handler.h>
#include <ascii-chat/ui/help_screen.h>
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/common.h>

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ============================================================================
 * Session Display Context
 * ============================================================================ */

/**
 * @brief Session display context for client frame output
 *
 * Uses the session library for TTY management and frame rendering.
 * Created during initialization, destroyed during cleanup.
 *
 * @ingroup client_display
 */
static session_display_ctx_t *g_display_ctx = NULL;

/**
 * @brief Atomic flag indicating if this is the first frame of the current connection
 *
 * Set to true at the start of each new connection, cleared after the first
 * frame is rendered. Used to disable logging during the first frame render
 * to prevent console corruption, then enable logging for subsequent frames.
 *
 * @ingroup client_display
 */
static atomic_bool g_is_first_frame_of_connection = true;

/**
 * @brief Keyboard input state for client mode
 *
 * Tracks whether keyboard input is initialized and ready to use.
 *
 * @ingroup client_display
 */
static bool g_keyboard_enabled = false;

/**
 * @brief Optional local media capture context for client mode
 *
 * When client mode is used with --file or --url, this provides access to
 * the local media source for keyboard controls (seek, pause, play).
 * NULL when client mode is used for network streaming only.
 *
 * @ingroup client_display
 */
static session_capture_ctx_t *g_display_capture_ctx = NULL;

/* TTY detection and terminal reset now handled by session display library */

/* Frame rendering now handled by session display library */

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * @brief Initialize what is necessary to display ascii frames
 *
 * Creates session display context for TTY management and frame rendering.
 * Must be called once during client startup.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_display
 */
int display_init() {
  // Build display configuration from options
  session_display_config_t config = {
      .snapshot_mode = GET_OPTION(snapshot_mode),
      .palette_type = GET_OPTION(palette_type),
      .custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL,
      .color_mode = TERM_COLOR_AUTO // Will be overridden by command-line options
  };

  // Create display context using session library
  g_display_ctx = session_display_create(&config);
  if (!g_display_ctx) {
    SET_ERRNO(ERROR_DISPLAY, "Failed to initialize display");
    return -1;
  }

  // Check if client mode should load local media (--file or --url)
  const char *media_url = GET_OPTION(media_url);
  const char *media_file = GET_OPTION(media_file);

  if ((media_url && strlen(media_url) > 0) || (media_file && strlen(media_file) > 0)) {
    // Client mode with local media file/URL - create capture context for keyboard controls
    session_capture_config_t capture_config = {0};
    capture_config.target_fps = 60;
    capture_config.resize_for_network = false;
    capture_config.should_exit_callback = NULL;
    capture_config.callback_data = NULL;

    if (media_url && strlen(media_url) > 0) {
      capture_config.type = MEDIA_SOURCE_FILE;
      capture_config.path = media_url;
    } else {
      if (strcmp(media_file, "-") == 0) {
        capture_config.type = MEDIA_SOURCE_STDIN;
        capture_config.path = NULL;
      } else {
        capture_config.type = MEDIA_SOURCE_FILE;
        capture_config.path = media_file;
      }
    }

    capture_config.loop = GET_OPTION(media_loop);
    capture_config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);

    g_display_capture_ctx = session_capture_create(&capture_config);
    if (!g_display_capture_ctx) {
      log_warn("Failed to create capture context for local media - keyboard seek/pause disabled");
      g_display_capture_ctx = NULL;
    }
  }

  // Initialize keyboard input for interactive controls (volume, color mode, flip, seek, pause)
  // Only initialize in TTY mode to avoid interfering with piped/redirected I/O
  if (terminal_is_stdin_tty()) {
    asciichat_error_t kb_result = keyboard_init();
    if (kb_result == ASCIICHAT_OK) {
      g_keyboard_enabled = true;
    } else {
      // Non-fatal: client can work without keyboard support
      log_warn("Failed to initialize keyboard input: %s", asciichat_error_string(kb_result));
      g_keyboard_enabled = false;
    }
  }

  return 0;
}

/**
 * @brief Check if display has TTY capability
 *
 * @return true if TTY is available for interactive output, false otherwise
 *
 * @ingroup client_display
 */
bool display_has_tty() {
  return session_display_has_tty(g_display_ctx);
}

/**
 * @brief Perform full display reset
 *
 * Executes complete terminal reset sequence for clean display state.
 * Safe to call multiple times and handles mode-specific behavior.
 *
 * @ingroup client_display
 */
void display_full_reset() {
  if (g_display_ctx && !should_exit()) {
    session_display_reset(g_display_ctx);
  }
}

/**
 * @brief Reset display state for new connection
 *
 * Resets the first frame tracking flag to prepare for a new connection.
 * Call this when starting a new connection to reset first frame tracking.
 *
 * @ingroup client_display
 */
void display_reset_for_new_connection() {
  atomic_store(&g_is_first_frame_of_connection, true);
}

/**
 * @brief Disable terminal logging for first frame
 *
 * Disables terminal logging before clearing the display for the first frame
 * to prevent log output from interfering with ASCII display.
 *
 * @ingroup client_display
 */
void display_disable_logging_for_first_frame() {
  // Disable terminal logging before clearing display and rendering first frame
  if (atomic_load(&g_is_first_frame_of_connection)) {
    log_set_terminal_output(false);
    atomic_store(&g_is_first_frame_of_connection, false);

    // Signal the intro splash screen to stop - first frame is ready to render
    splash_intro_done();
  }
}

/**
 * Render ASCII frame to display
 *
 * Uses session display library for frame output routing based on display
 * mode and snapshot requirements. Also polls for keyboard input for
 * interactive controls (volume, color mode, flip).
 *
 * @param frame_data ASCII frame data to render
 * @param is_snapshot_frame Whether this is the final snapshot frame
 *
 * @ingroup client_display
 */
void display_render_frame(const char *frame_data) {
  if (!frame_data) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Attempted to render NULL frame data");
    return;
  }

  // Stop splash screen animation on first frame
  // This must be called BEFORE any frame rendering to prevent splash/frame flickering
  display_disable_logging_for_first_frame();

  // Render help screen if active (suppresses frame rendering)
  if (session_display_is_help_active(g_display_ctx)) {
    session_display_render_help(g_display_ctx);

    // Still poll keyboard for interactive controls while help is visible
    if (g_keyboard_enabled) {
      keyboard_key_t key = keyboard_read_nonblocking();
      if (key != KEY_NONE) {
        session_handle_keyboard_input(g_display_capture_ctx, g_display_ctx, key);
      }
    }
    return; // Skip normal frame rendering
  }

  // Use session display library for frame rendering
  session_display_render_frame(g_display_ctx, frame_data);

  // Poll for keyboard input and handle interactive controls
  // - If client mode has local media (--file/--url), pass capture context for full controls
  //   (seek, pause, play, volume, color mode, flip)
  // - If client mode is network-only, pass NULL (volume, color mode, flip work; seek/pause ignored)
  if (g_keyboard_enabled) {
    keyboard_key_t key = keyboard_read_nonblocking();
    if (key != KEY_NONE) {
      session_handle_keyboard_input(g_display_capture_ctx, g_display_ctx, key);
    }
  }
}

/**
 * @brief Cleanup display subsystem
 *
 * Destroys session display context and releases all resources including
 * keyboard input handling.
 *
 * @ingroup client_display
 */
void display_cleanup() {
  // Cleanup keyboard input if it was initialized
  if (g_keyboard_enabled) {
    keyboard_destroy();
    g_keyboard_enabled = false;
  }

  // Cleanup optional local media capture context if it was created
  if (g_display_capture_ctx) {
    session_capture_destroy(g_display_capture_ctx);
    g_display_capture_ctx = NULL;
  }

  if (g_display_ctx) {
    session_display_destroy(g_display_ctx);
    g_display_ctx = NULL;
  }
}
