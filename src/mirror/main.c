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

#include "platform/abstraction.h"
#include "common.h"
#include "options/options.h"
#include "util/time.h"

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

  // Create capture and display contexts from command-line options
  // Passing NULL config auto-initializes from GET_OPTION()
  // This handles: media files, stdin, webcam, test patterns, terminal capabilities, ANSI init
  session_capture_ctx_t *capture = session_capture_create(NULL);
  if (!capture) {
    log_fatal("Failed to initialize capture source");
    return ERROR_MEDIA_INIT;
  }

  session_display_ctx_t *display = session_display_create(NULL);
  if (!display) {
    log_fatal("Failed to initialize display");
    session_capture_destroy(capture);
    return ERROR_DISPLAY;
  }

  // Snapshot mode timing
  uint64_t snapshot_start_time_ns = 0;
  bool snapshot_done = false;
  if (GET_OPTION(snapshot_mode)) {
    snapshot_start_time_ns = time_get_ns();
  }

  // FPS tracking
  uint64_t frame_count = 0;
  uint64_t fps_report_time_ns = time_get_ns();

  log_info("Mirror mode running - press Ctrl+C to exit");
  log_set_terminal_output(false);

  while (!mirror_should_exit()) {
    // Frame rate limiting using session capture's adaptive sleep
    session_capture_sleep_for_fps(capture);

    // Capture timestamp for snapshot mode timing
    uint64_t current_time_ns = time_get_ns();

    // Snapshot mode: check if delay has elapsed (delay 0 = capture first frame immediately)
    if (GET_OPTION(snapshot_mode) && !snapshot_done) {
      double elapsed_sec = time_ns_to_s(time_elapsed_ns(snapshot_start_time_ns, current_time_ns));

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

    // Convert image to ASCII using display context
    // Handles all palette, terminal caps, width, height, stretch settings
    char *ascii_frame = session_display_convert_to_ascii(display, image);

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
    uint64_t fps_elapsed_ns = time_elapsed_ns(fps_report_time_ns, current_time_ns);

    if (fps_elapsed_ns >= 5 * NS_PER_SEC_INT) {
      double elapsed_sec = time_ns_to_s(fps_elapsed_ns);
      double fps = (double)frame_count / elapsed_sec;
      log_debug("Mirror FPS: %.1f (target: %u)", fps, session_capture_get_target_fps(capture));
      frame_count = 0;
      fps_report_time_ns = current_time_ns;
    }
  }

  // Cleanup
  log_set_terminal_output(true);
  log_info("Mirror mode shutting down");

  session_display_destroy(display);
  session_capture_destroy(capture);

  return 0;
}
