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
#include <ascii-chat/session/client_like.h>
#include <ascii-chat/session/capture.h>
#include <ascii-chat/session/display.h>
#include <ascii-chat/session/render.h>
#include <ascii-chat/session/keyboard_handler.h>
#include <ascii-chat/log/logging.h>

/* ============================================================================
 * Mode-Specific Keyboard Handler
 * ============================================================================ */

/**
 * Mirror mode keyboard handler callback
 *
 * @param capture Capture context for media source control
 * @param key Keyboard key code
 * @param user_data Display context for help screen toggle (borrowed reference)
 */
static void mirror_keyboard_handler(session_capture_ctx_t *capture, int key, void *user_data) {
  session_display_ctx_t *display = (session_display_ctx_t *)user_data;
  session_handle_keyboard_input(capture, display, (keyboard_key_t)key);
}

/* ============================================================================
 * Mode-Specific Main Loop
 * ============================================================================ */

/**
 * Mirror mode run callback
 *
 * Called after all shared initialization is complete (capture opened, audio
 * started, display ready, splash done). Runs the unified render loop.
 *
 * @param capture   Initialized capture context
 * @param display   Initialized display context
 * @param user_data Unused (NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t mirror_run(session_capture_ctx_t *capture, session_display_ctx_t *display, void *user_data) {
  (void)user_data; // Unused

  // Get the render loop's should_exit callback from session_client_like
  bool (*render_should_exit)(void *) = session_client_like_get_render_should_exit();
  if (!render_should_exit) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Render should_exit callback not initialized");
  }

  // Run the unified render loop with keyboard support
  return session_render_loop(capture, display,
                             render_should_exit,      // Exit check (checks global + custom)
                             NULL,                    // No custom capture callback
                             NULL,                    // No custom sleep callback
                             mirror_keyboard_handler, // Keyboard handler
                             display);                // user_data for keyboard handler
}

/* ============================================================================
 * Mirror Mode Entry Point
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
  // Configure mode-specific session settings
  session_client_like_config_t config = {
      .run_fn = mirror_run,
      .run_user_data = NULL,
      .keyboard_handler = mirror_keyboard_handler,
      .print_newline_on_tty_exit = true, // Mirror prints newline to separate frame from prompt
  };

  // Run shared initialization/teardown with mode-specific loop
  asciichat_error_t result = session_client_like_run(&config);

  if (result != ASCIICHAT_OK) {
    log_error("Mirror mode failed with error code: %d", result);
  }

  return (int)result;
}
