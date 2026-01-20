/**
 * @file mirror/main.c
 * @ingroup mirror
 * @brief Local media mirror mode: view webcam or media files as ASCII art without network
 *
 * Mirror mode provides a simple way to view webcam feed or media files converted
 * to ASCII art directly in the terminal. No server connection is required.
 *
 * ## Features
 *
 * - Local webcam capture and ASCII conversion
 * - Media file playback (video/audio files, animated GIFs)
 * - Loop playback for media files
 * - Terminal capability detection for optimal color output
 * - Frame rate limiting for smooth display
 * - Clean shutdown on Ctrl+C
 *
 * ## Usage
 *
 * Run as a standalone mode:
 * @code
 * ascii-chat mirror                        # Use webcam
 * ascii-chat mirror --file video.mp4       # Play video file
 * ascii-chat mirror --file video.mp4 --loop # Loop video file
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#include "main.h"
#include "session/capture.h"
#include "session/display.h"
#include "video/ascii.h"
#include "video/image.h"
#include "video/ansi_fast.h"

#include "platform/abstraction.h"
#include "common.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ============================================================================
 * Global State Variables
 * ============================================================================ */

/** Global flag indicating shutdown has been requested */
static atomic_bool g_mirror_should_exit = false;

/**
 * Check if shutdown has been requested
 *
 * @return true if shutdown requested, false otherwise
 */
static bool mirror_should_exit(void) {
  return atomic_load(&g_mirror_should_exit);
}

/**
 * Signal that shutdown should be requested
 */
static void mirror_signal_exit(void) {
  atomic_store(&g_mirror_should_exit, true);
}

/**
 * Console control handler for Ctrl+C and related events
 *
 * @param event The control event that occurred
 * @return true if the event was handled
 */
static bool mirror_console_ctrl_handler(console_ctrl_event_t event) {
  if (event != CONSOLE_CTRL_C && event != CONSOLE_CTRL_BREAK) {
    return false;
  }

  // Use atomic instead of volatile for signal handler
  static _Atomic int ctrl_c_count = 0;
  int count = atomic_fetch_add(&ctrl_c_count, 1) + 1;

  if (count > 1) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    _exit(1);
#endif
  }

  mirror_signal_exit();
  return true;
}

/* ============================================================================
 * Mirror Mode Display (using session library)
 * ============================================================================ */

/* Display is now managed via session_display_ctx_t from lib/session/display.h */

/* ============================================================================
 * Mirror Mode Main Loop
 * ============================================================================ */

/**
 * @brief Run mirror mode main loop
 *
 * Initializes webcam and terminal, then continuously captures frames,
 * converts them to ASCII art, and displays them locally.
 *
 * Uses the session library for unified capture and display management.
 *
 * @return 0 on success, non-zero error code on failure
 */
int mirror_main(void) {
  log_info("Starting mirror mode");

  // Install console control-c handler
  platform_set_console_ctrl_handler(mirror_console_ctrl_handler);

#ifndef _WIN32
  platform_signal(SIGPIPE, SIG_IGN);
#endif

  // Build capture configuration from options
  session_capture_config_t capture_config = {0};
  const char *media_file = GET_OPTION(media_file);
  bool media_from_stdin = GET_OPTION(media_from_stdin);

  if (media_file[0] != '\0') {
    // File or stdin streaming
    capture_config.type = media_from_stdin ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_FILE;
    capture_config.path = media_file;
    capture_config.loop = GET_OPTION(media_loop) && !media_from_stdin;
  } else if (GET_OPTION(test_pattern)) {
    // Test pattern mode
    capture_config.type = MEDIA_SOURCE_TEST;
    capture_config.path = NULL;
  } else {
    // Webcam mode (default)
    static char webcam_index_str[32];
    snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", GET_OPTION(webcam_index));
    capture_config.type = MEDIA_SOURCE_WEBCAM;
    capture_config.path = webcam_index_str;
  }
  capture_config.target_fps = 60; // 60 FPS for local display
  capture_config.resize_for_network = false;

  // Create capture context
  session_capture_ctx_t *capture = session_capture_create(&capture_config);
  if (!capture) {
    log_fatal("Failed to initialize capture source");
    return ERROR_MEDIA_INIT;
  }

  // Build display configuration from options
  session_display_config_t display_config = {
      .snapshot_mode = GET_OPTION(snapshot_mode),
      .palette_type = GET_OPTION(palette_type),
      .custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL,
      .color_mode = TERM_COLOR_AUTO // Will be overridden by command-line options
  };

  // Create display context
  session_display_ctx_t *display = session_display_create(&display_config);
  if (!display) {
    log_fatal("Failed to initialize display");
    session_capture_destroy(capture);
    return ERROR_DISPLAY;
  }

  // Get terminal capabilities and palette from display context
  const terminal_capabilities_t *caps = session_display_get_caps(display);
  const char *palette_chars = session_display_get_palette_chars(display);
  const char *luminance_palette = session_display_get_luminance_palette(display);

  // Initialize ANSI color lookup tables based on terminal capabilities
  if (caps->color_level == TERM_COLOR_TRUECOLOR) {
    ansi_fast_init();
  } else if (caps->color_level == TERM_COLOR_256) {
    ansi_fast_init_256color();
  } else if (caps->color_level == TERM_COLOR_16) {
    ansi_fast_init_16color();
  }

  // Snapshot mode timing
  struct timespec snapshot_start_time = {0, 0};
  bool snapshot_done = false;
  if (GET_OPTION(snapshot_mode)) {
    (void)clock_gettime(CLOCK_MONOTONIC, &snapshot_start_time);
  }

  // FPS tracking
  uint64_t frame_count = 0;
  struct timespec fps_report_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &fps_report_time);

  log_info("Mirror mode running - press Ctrl+C to exit");
  log_set_terminal_output(false);

  while (!mirror_should_exit()) {
    // Frame rate limiting using session capture's adaptive sleep
    session_capture_sleep_for_fps(capture);

    // Capture timestamp for snapshot mode timing
    struct timespec current_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

    // Snapshot mode: check if delay has elapsed (delay 0 = capture first frame immediately)
    if (GET_OPTION(snapshot_mode) && !snapshot_done) {
      double elapsed_sec = (double)(current_time.tv_sec - snapshot_start_time.tv_sec) +
                           (double)(current_time.tv_nsec - snapshot_start_time.tv_nsec) / 1e9;

      float snapshot_delay = GET_OPTION(snapshot_delay);
      if (elapsed_sec >= snapshot_delay) {
        snapshot_done = true;
      }
    }

    // Read frame from capture source
    image_t *image = session_capture_read_frame(capture);
    if (!image) {
      // Check if we've reached end of file for media sources
      if (session_capture_at_end(capture)) {
        log_info("Media source reached end of file");
        break; // Exit mirror loop - end of media
      }
      platform_sleep_usec(10000); // 10ms delay before retry
      continue;
    }

    // Convert image to ASCII
    // When stretch is 0 (disabled), we preserve aspect ratio (true)
    // When stretch is 1 (enabled), we allow stretching without aspect ratio preservation (false)
    bool stretch = GET_OPTION(stretch);
    unsigned short int width = GET_OPTION(width);
    unsigned short int height = GET_OPTION(height);
    bool preserve_aspect_ratio = !stretch;

    // Need a mutable copy for ascii_convert_with_capabilities
    terminal_capabilities_t caps_copy = *caps;
    char *ascii_frame = ascii_convert_with_capabilities(image, width, height, &caps_copy, preserve_aspect_ratio, stretch,
                                                        palette_chars, luminance_palette);

    if (ascii_frame) {
      // When piping/redirecting in snapshot mode, only output the final frame
      // When outputting to TTY, show live preview frames
      bool snapshot_mode = GET_OPTION(snapshot_mode);
      bool should_write = !snapshot_mode || session_display_has_tty(display) || snapshot_done;
      if (should_write) {
        session_display_render_frame(display, ascii_frame, snapshot_done);
      }

      // Snapshot mode: exit after capturing the final frame
      if (snapshot_mode && snapshot_done) {
        SAFE_FREE(ascii_frame);
        // NOTE: Do NOT free 'image' - it's owned by capture context
        break;
      }

      SAFE_FREE(ascii_frame);
      frame_count++;
    }

    // NOTE: Do NOT free 'image' - it's owned by capture context and reused on next read

    // FPS reporting every 5 seconds
    uint64_t fps_elapsed_us = ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
                              ((uint64_t)fps_report_time.tv_sec * 1000000 + (uint64_t)fps_report_time.tv_nsec / 1000);

    if (fps_elapsed_us >= 5000000) {
      double fps = (double)frame_count / ((double)fps_elapsed_us / 1000000.0);
      log_debug("Mirror FPS: %.1f (target: %u)", fps, session_capture_get_target_fps(capture));
      frame_count = 0;
      fps_report_time = current_time;
    }
  }

  // Cleanup
  log_set_terminal_output(true);
  log_info("Mirror mode shutting down");

  session_display_destroy(display);
  session_capture_destroy(capture);

  return 0;
}
