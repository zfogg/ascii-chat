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
#include "session/render.h"

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

/**
 * Exit condition callback for render loop
 *
 * @param user_data Unused (NULL)
 * @return true if render loop should exit
 */
static bool mirror_render_should_exit(void *user_data) {
  (void)user_data; // Unused parameter
  return mirror_should_exit();
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

  log_info("Mirror mode running - press Ctrl+C to exit");
  log_set_terminal_output(false);

  // Run the unified render loop - handles frame capture, ASCII conversion, and rendering
  // Synchronous mode: pass capture context, NULL for callbacks
  asciichat_error_t result = session_render_loop(capture, display, mirror_render_should_exit,
                                                 NULL,  // No custom capture callback
                                                 NULL,  // No custom sleep callback
                                                 NULL); // No user_data needed

  if (result != ASCIICHAT_OK) {
    log_error("Render loop failed with error code: %d", result);
  }

  // Cleanup
  log_set_terminal_output(true);
  log_info("Mirror mode shutting down");

  session_display_destroy(display);
  session_capture_destroy(capture);

  return 0;
}
