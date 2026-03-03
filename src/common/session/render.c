/**
 * @file session/render.c
 * @ingroup session
 * @brief Unified render loop implementation for all display modes
 *
 * Provides a single, centralized render loop that supports both synchronous and
 * event-driven modes. All display modes (mirror, client, discovery) use this loop.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "session/render.h"
#include "session/capture.h"
#include "session/display.h"
#include "session/pipeline.h"
#include <ascii-chat/terminal/fd/reader.h>
#include <ascii-chat/ui/keyboard_help.h>
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/common.h>
#include <fcntl.h>
#include <unistd.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/app_callbacks.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ============================================================================
 * Unified Render Loop Implementation
 * ============================================================================ */

// Global flags for snapshot mode timing
bool g_snapshot_first_frame_rendered = false;
uint64_t g_snapshot_first_frame_rendered_ns = 0;  // Timestamp when first frame was rendered (for terminal output)
uint64_t g_snapshot_first_capture_ns = 0;         // Timestamp when first frame was captured (for video output)
uint64_t g_snapshot_actual_duration_ms = 0;       // Actual elapsed time in ms when capture thread hits snapshot timeout
uint64_t g_snapshot_last_capture_elapsed_ns = 0;  // Elapsed time from first to last frame capture (for PTS distribution)

asciichat_error_t session_render_loop(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                      session_should_exit_fn should_exit, session_capture_fn capture_cb,
                                      session_sleep_for_frame_fn sleep_cb, session_keyboard_handler_fn keyboard_handler,
                                      void *user_data) {
  // Validate required parameters
  log_info("[SESSION_RENDER_LOOP] START: capture=%p, display=%p, mode=%s",
           (void *)capture, (void *)display, capture ? "SYNCHRONOUS" : "EVENT-DRIVEN");

  if (!display) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_render_loop: display context is NULL");
    return ERROR_INVALID_PARAM;
  }

  if (!should_exit) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_render_loop: should_exit callback is NULL");
    return ERROR_INVALID_PARAM;
  }

  // Validate mode: either capture context OR custom callbacks, not both
  if (!capture && !capture_cb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_render_loop: must provide either capture context or capture callback");
    return ERROR_INVALID_PARAM;
  }

  if (capture && capture_cb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_render_loop: cannot provide both capture context and capture callback");
    return ERROR_INVALID_PARAM;
  }

  // In event-driven mode, both capture_cb and sleep_cb must be provided together
  if (capture_cb && !sleep_cb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_render_loop: capture_cb requires sleep_cb (must provide both or neither)");
    return ERROR_INVALID_PARAM;
  }

  if (sleep_cb && !capture_cb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_render_loop: sleep_cb requires capture_cb (must provide both or neither)");
    return ERROR_INVALID_PARAM;
  }

  // SYNCHRONOUS MODE: Use threaded pipeline for capture + render
  if (capture != NULL) {
    log_info("[SESSION_RENDER_LOOP_SYNCHRONOUS] Creating pipeline...");
    session_pipeline_t *pipeline = NULL;
    asciichat_error_t pipeline_err = session_pipeline_create(capture, display, &pipeline);
    if (pipeline_err != ASCIICHAT_OK) {
      log_error("[SESSION_RENDER_LOOP_SYNCHRONOUS] Pipeline creation failed: %d", pipeline_err);
      return pipeline_err;
    }
    log_info("[SESSION_RENDER_LOOP_SYNCHRONOUS] Pipeline created successfully, running main loop...");

    asciichat_error_t run_err = session_pipeline_run_main(pipeline, should_exit, keyboard_handler, user_data);

    // In snapshot mode, pass actual capture-based elapsed time to encoder for correct video duration
    if (GET_OPTION(snapshot_mode) && display && g_snapshot_first_capture_ns > 0) {
      uint64_t now_ns = time_get_ns();
      uint64_t elapsed_ns = now_ns - g_snapshot_first_capture_ns;
      double capture_elapsed_sec = (double)elapsed_ns / (double)NS_PER_SEC_INT;
      log_info("SNAPSHOT: Pipeline finished - passing capture_elapsed=%.3f to encoder", capture_elapsed_sec);
      session_display_set_snapshot_actual_duration(display, capture_elapsed_sec);
    }

    session_pipeline_destroy(pipeline);
    return run_err;
  }

  // Snapshot mode state tracking
  bool snapshot_mode = GET_OPTION(snapshot_mode);
  bool snapshot_done = false;
  uint64_t frames_rendered_since_first = 0; // Track frames rendered after first frame (for snapshot delay)
  // NOTE: g_snapshot_first_frame_rendered is set by display.c when first frame is rendered via platform_write_all()

  // Help screen state tracking for clear-screen transition
  bool help_was_active = false;

  // Terminal resize tracking (for auto_width/auto_height mode)
  unsigned short int last_terminal_width = terminal_get_effective_width();
  unsigned short int last_terminal_height = terminal_get_effective_height();

  log_info("session_render_loop: STARTING - display=%p capture=%p capture_cb=%p snapshot_mode=%s snapshot_delay=%.2f",
           (void *)display, (void *)capture, (void *)capture_cb, snapshot_mode ? "YES" : "NO",
           snapshot_mode ? GET_OPTION(snapshot_delay) : 0.0);
  log_info("session_render_loop: About to enter main loop, should_exit fn check...");
  if (should_exit(user_data)) {
    log_warn("session_render_loop: EXIT signal already set at entry, aborting render loop");
    return ASCIICHAT_OK;
  }
  log_info("session_render_loop: OK to proceed with main loop");


  // Keyboard input initialization (if keyboard handler is provided)
  // Disable keyboard only in snapshot mode
  // Keyboard is pre-initialized from asciichat_shared_init()
  // Allow keyboard input in all other modes if handler provided
  bool keyboard_enabled = keyboard_handler && !snapshot_mode;

  // Pause mode state tracking (for pause/unpause transitions in event-driven mode)
  bool initial_paused_frame_rendered = false;
  bool is_paused = false;
  log_info("render_loop: Keyboard setup - handler=%p snapshot=%s enabled=%s", (void *)keyboard_handler,
           snapshot_mode ? "YES" : "NO", keyboard_enabled ? "YES" : "NO");

  // Log TTY detection status for debugging keyboard issues
  log_info("TTY Status: stdin_tty=%d stdout_tty=%d interactive=%d keyboard_enabled=%d", terminal_is_stdin_tty(),
           terminal_is_stdout_tty(), terminal_is_interactive(), keyboard_enabled);

  // Frame rate timing (event-driven mode only)
  uint64_t frame_count = 0;
  uint64_t frame_start_ns = 0;

  // Event-driven mode: disable console logging during rendering to prevent logs from corrupting frame display
  // This must be done BEFORE the render loop to avoid repeated lock/unlock cycles that could cause deadlocks
  log_info("[EVENT_DRIVEN_MODE] Entering render loop. snapshot_mode=%s", snapshot_mode ? "YES" : "NO");
  log_set_terminal_output(false);

  // Main render loop
  log_debug("session_render_loop: entering main loop");
  int loop_iteration = 0;
  log_info("[RENDER_LOOP_START] snapshot_mode=%s, snapshot_delay=%.2f", snapshot_mode ? "YES" : "NO",
           snapshot_mode ? GET_OPTION(snapshot_delay) : 0.0);

  // Wait for splash animation to complete before rendering first frame
  // This prevents ASCII art from mixing with splash screen output
  static bool splash_wait_done = false;
  if (!splash_wait_done) {
    splash_wait_done = true;
    // Re-enable logging briefly for splash wait, then disable again
    log_set_terminal_output(true);
    splash_wait_for_animation();
    log_set_terminal_output(false);
  }

  // In snapshot mode, we must render at least one frame before checking should_exit
  // to ensure the snapshot delay timer can start
  bool force_first_iteration = snapshot_mode && frame_count == 0;

  while (force_first_iteration || (!should_exit(user_data) && !(snapshot_mode && snapshot_done))) {
    force_first_iteration = false;
    loop_iteration++;
    log_info("[LOOP_ITER] iteration=%d, snapshot_done=%s, frame_count=%lu", loop_iteration, snapshot_done ? "YES" : "NO", frame_count);
    if (loop_iteration % 60 == 0) {
      log_debug("session_render_loop: iteration %d, should_exit check returning false", loop_iteration);
    }
    // Snapshot mode: exit at start of iteration if done
    // This prevents frame 2+ from being captured when snapshot_delay has elapsed
    if (snapshot_mode && snapshot_done && frame_count > 0) {
      log_info("[SNAPSHOT_EXIT] Snapshot mode: exiting at loop iteration start (snapshot_done=true, frames=%lu)", frame_count);
      break;
    }

    log_debug_every(US_PER_SEC_INT, "session_render_loop: frame %lu", frame_count);
    // Frame timing - measure total time to maintain target FPS
    frame_start_ns = time_get_ns();

    // Frame capture and timing - mode-dependent
    image_t *image = NULL;
    uint64_t capture_start_ns = 0;
    uint64_t capture_end_ns = 0;
    uint64_t pre_convert_ns = 0;
    uint64_t post_convert_ns = 0;
    uint64_t conversion_elapsed_ns = 0;

    // EVENT-DRIVEN MODE: Frames come from callbacks
    // Both sleep_cb and capture_cb are guaranteed non-NULL by validation above
    sleep_cb(user_data);
    image = capture_cb(user_data);

    if (!image) {
      // No frame available - this is normal in async modes (network latency, etc.)
      // Just continue to next iteration, don't exit
      continue;
    }

    // Event-driven mode: increment frame count
    frame_count++;

    // Check for terminal resize (if auto_width or auto_height is enabled)
    // This allows the render to adapt immediately when the user resizes the terminal
    bool auto_width = GET_OPTION(auto_width);
    bool auto_height = GET_OPTION(auto_height);
    if (auto_width || auto_height) {
      unsigned short int current_width = 0;
      unsigned short int current_height = 0;

      asciichat_error_t size_err = get_terminal_size(&current_width, &current_height);
      if (size_err == ASCIICHAT_OK) {
        bool width_changed = auto_width && (current_width != last_terminal_width);
        bool height_changed = auto_height && (current_height != last_terminal_height);

        if (width_changed || height_changed) {
          if (width_changed) {
            options_set_int("width", current_width);
            log_info("Terminal width changed: %u → %u", last_terminal_width, current_width);
            last_terminal_width = current_width;
          }
          if (height_changed) {
            options_set_int("height", current_height);
            log_info("Terminal height changed: %u → %u", last_terminal_height, current_height);
            last_terminal_height = current_height;
          }
          // Clear screen when terminal is resized to avoid visual artifacts
          terminal_clear_screen();
        }
      }
    }

    // Convert image to ASCII using display context
    // Handles all palette, terminal caps, width, height, stretch settings
    pre_convert_ns = time_get_ns();
    char *ascii_frame = session_display_convert_to_ascii(display, image);
    post_convert_ns = time_get_ns();
    conversion_elapsed_ns = post_convert_ns - pre_convert_ns;

    // DEBUG: Log frame conversion to file
    FILE *debug_log = fopen("/tmp/render_loop_debug.log", "a");
    if (debug_log) {
      fprintf(debug_log, "[FRAME_CONVERT] frame %lu: image=%p, ascii_frame=%p (len=%zu), conversion_ms=%u\n",
              frame_count, (void *)image, (void *)ascii_frame,
              ascii_frame ? strlen(ascii_frame) : 0,
              (unsigned)(conversion_elapsed_ns / NS_PER_MS_INT));
      fclose(debug_log);
    }

    if (frame_count <= 3) {
      log_info("RENDER_LOOP[%lu]: image=%p, ascii_frame=%p (conversion took %u ms)",
               frame_count, (void *)image, (void *)ascii_frame,
               (unsigned)(conversion_elapsed_ns / NS_PER_MS_INT));
    }

    // Declare variables that need scope beyond the if (ascii_frame) block
    bool is_paused_frame = initial_paused_frame_rendered && is_paused;
    bool output_paused_frame = snapshot_mode && is_paused_frame;
    uint64_t pre_render_ns = 0, post_render_ns = 0;

    if (ascii_frame) {
      log_info_every(1 * NS_PER_SEC_INT, "render_loop: ascii_frame ready (len=%zu)", strlen(ascii_frame));

      // Always attempt to render frames; the display context will handle filtering based on:
      // - TTY mode: render all frames with cursor control (even in snapshot mode, for animation)
      // - Piped mode: render all frames without cursor control (for continuous capture)
      // - Snapshot mode on non-TTY: only the display context renders the final frame
      // BUT: don't render if splash screen is still animating - wait for it to finish
      bool splash_running = splash_is_running();
      bool should_write = !splash_running;
      if (frame_count == 1) {
        log_dev("[RENDER_FRAME] Frame 1: splash_is_running=%s, should_write=%s", splash_running ? "YES" : "NO", should_write ? "YES" : "NO");
      }
      if (should_write) {
        // is_final = true when: snapshot done, or paused frame (for both snapshot and pause modes)
        // Profile: render frame
        pre_render_ns = time_get_ns();
        START_TIMER("render_frame");

        if (frame_count <= 5 || frame_count % 10 == 0) {
          log_info("RENDER_LOOP: Frame %lu - calling session_display_render_frame, display=%p",
                   (unsigned long)frame_count, (void *)display);
        }

        // Check if help screen is active - if so, render help instead of frame
        // Help screen is disabled in snapshot mode and non-interactive terminals (keyboard disabled)
        bool help_is_active = display && keyboard_help_is_active(display);

        // Detect transition from help to ASCII art rendering
        // When help closes, clear the screen before rendering ASCII art
        if (help_was_active && !help_is_active) {
          terminal_clear_screen();
          log_debug_every(1 * NS_PER_SEC_INT, "Cleared screen when transitioning from help to ASCII art");
        }

        if (help_is_active) {
          keyboard_help_render(display);
        } else {
          session_display_render_frame(display, ascii_frame);
        }

        // Snapshot mode: increment frame counter for all displayed frames
        // Counter starts at 1 for the first displayed frame, then 2, 3, 4... for subsequent frames
        // Timer is started by display.c when g_snapshot_first_frame_rendered is set
        if (snapshot_mode && g_snapshot_first_frame_rendered) {
          frames_rendered_since_first++;
          log_info_every(1 * NS_PER_SEC_INT, "[SNAPSHOT] Frame rendered: frames_rendered_since_first=%lu", frames_rendered_since_first);
        }

        // Update help state for next iteration
        help_was_active = help_is_active;

        STOP_TIMER("render_frame");
      }

      // Keyboard input polling (if enabled) - MUST come before snapshot exit check so help screen can be toggled
      // Allow keyboard in snapshot mode too (for help screen toggle debugging)
      // Only enable keyboard if BOTH stdin AND stdout are TTYs to avoid buffering issues
      // when tcsetattr() modifies the tty line discipline
      if (keyboard_enabled && keyboard_handler) {
        START_TIMER("keyboard_read_%lu", (unsigned long)frame_count);
        keyboard_key_t key = keyboard_read_nonblocking();
        double keyboard_elapsed_ns = STOP_TIMER("keyboard_read_%lu", (unsigned long)frame_count);
        if (keyboard_elapsed_ns >= 0.0) {
          char _duration_str[32];
          time_pretty((uint64_t)keyboard_elapsed_ns, -1, _duration_str, sizeof(_duration_str));
          log_dev_every(1 * NS_PER_SEC_INT, "RENDER[%lu] Keyboard read complete (key=%d) in %s",
                        (unsigned long)frame_count, key, _duration_str);
        }
        if (key != KEY_NONE) {
          log_debug("KEYBOARD: Key pressed: code=%d char='%c'", key, (key >= 32 && key < 127) ? key : '?');
          // Check if interactive grep should handle this key
          if (log_search_should_handle(key)) {
            log_debug("KEYBOARD: Grep handler taking key %d", key);
            log_search_handle_key(key);
            continue; // Force immediate re-render
          }

          // Normal keyboard handler
          log_debug("KEYBOARD: Normal handler taking key %d", key);
          keyboard_handler(capture, key, user_data);
        }
      }


      // Free frame before checking exit conditions to avoid double-free
      SAFE_FREE(ascii_frame);
    }

    // Snapshot mode: check if elapsed time has reached snapshot_delay duration (terminal render timing)
    // Separate from capture timing - both should measure their own snapshot_delay
    if (snapshot_mode && !snapshot_done) {
      if (g_snapshot_first_frame_rendered && g_snapshot_first_frame_rendered_ns > 0) {
        double snapshot_delay = GET_OPTION(snapshot_delay);
        uint64_t now_ns = time_get_ns();
        uint64_t elapsed_ns = now_ns - g_snapshot_first_frame_rendered_ns;
        double elapsed_sec = (double)elapsed_ns / (double)NS_PER_SEC_INT;

        if (frames_rendered_since_first == 1 || frames_rendered_since_first % 20 == 0) {
          log_info("SNAPSHOT: RENDER_CHECK iter=%lu elapsed=%.3f target=%.2f (from first_frame_rendered)",
                   frames_rendered_since_first, elapsed_sec, snapshot_delay);
        }

        // snapshot_delay=0 means exit after first frame
        // snapshot_delay>0 means wait that many seconds before exiting
        // Exit is based on elapsed wall-clock time, not frame count
        bool should_exit = (snapshot_delay == 0.0) || (elapsed_sec >= snapshot_delay);

        if (should_exit) {
          log_info("SNAPSHOT: RENDER_DONE at iteration %lu - elapsed=%.3f target=%.2f (setting snapshot_done=true)",
                   frames_rendered_since_first, elapsed_sec, snapshot_delay);
          // We don't end frames with newlines so the next log would print on the same line as the frame's
          // last row without an \n here. We only need this \n in stdout in snapshot mode and when interactive,
          // so piped snapshots don't have a weird newline in stdout that they don't need.
          if (snapshot_mode && terminal_is_interactive()) {
            printf("\n");
          }
          snapshot_done = true;
        }
      } else {
        if (frames_rendered_since_first == 0) {
          log_debug("SNAPSHOT: Waiting for first_frame_rendered (g_snapshot_first_frame_rendered=%d, g_snapshot_first_frame_rendered_ns=%llu)",
                    g_snapshot_first_frame_rendered, (unsigned long long)g_snapshot_first_frame_rendered_ns);
        }
      }
    }

    // Exit conditions: snapshot mode exits after capturing the final frame or initial paused frame
    if (snapshot_mode && (snapshot_done || output_paused_frame)) {
      log_info("SNAPSHOT: EXIT CONDITION MET - snapshot_done=%d, output_paused_frame=%d", snapshot_done, output_paused_frame);

      // Calculate elapsed time from first frame captured (for video file)
      // This is separate from render time - video should span from first capture to last capture
      double capture_elapsed_sec = 0.0;
      if (g_snapshot_first_capture_ns > 0) {
        uint64_t now_ns = time_get_ns();
        uint64_t elapsed_ns = now_ns - g_snapshot_first_capture_ns;
        capture_elapsed_sec = (double)elapsed_ns / (double)NS_PER_SEC_INT;
        log_info("SNAPSHOT: EXIT - Capture elapsed: %.3f seconds, render elapsed: %.3f seconds, g_snapshot_first_capture_ns=%llu",
                 capture_elapsed_sec,
                 (g_snapshot_first_frame_rendered_ns > 0) ? (double)(now_ns - g_snapshot_first_frame_rendered_ns) / NS_PER_SEC_INT : 0.0,
                 (unsigned long long)g_snapshot_first_capture_ns);
      } else {
        log_warn("SNAPSHOT: EXIT condition met but g_snapshot_first_capture_ns is 0!");
      }

      // Pass actual capture elapsed time to encoder for correct video duration
      log_info("SNAPSHOT: About to call session_display_set_snapshot_actual_duration with duration=%.3f, display=%p",
               capture_elapsed_sec, (void *)display);
      if (display) {
        session_display_set_snapshot_actual_duration(display, capture_elapsed_sec);
      } else {
        log_warn("SNAPSHOT: display context is NULL!");
      }

      // Signal application to exit in snapshot mode
      APP_CALLBACK_VOID(signal_exit);
      break;
    }

    // Frame rendering and timing details (moved down since snapshot exit is now above it)
    if (ascii_frame) {

      // Measure frame completion right after rendering, BEFORE keyboard polling
      // This gives us accurate timing for just the core frame operations
      uint64_t frame_end_render_ns = time_get_ns();

      // Calculate each phase duration
      uint64_t prestart_ms =
          (capture_start_ns > frame_start_ns) ? (capture_start_ns - frame_start_ns) / NS_PER_MS_INT : 0;
      uint64_t capture_ms =
          (capture_end_ns > capture_start_ns) ? (capture_end_ns - capture_start_ns) / NS_PER_MS_INT : 0;
      uint64_t convert_ms = conversion_elapsed_ns / NS_PER_MS_INT;
      uint64_t render_ms =
          (post_render_ns > pre_render_ns && post_render_ns > 0) ? (post_render_ns - pre_render_ns) / NS_PER_MS_INT : 0;
      uint64_t total_ms =
          (frame_end_render_ns > frame_start_ns) ? (frame_end_render_ns - frame_start_ns) / NS_PER_MS_INT : 0;

      // Log phase breakdown every 5 frames
      if (frame_count % 5 == 0) {
        log_dev_every(
            2 * NS_PER_SEC_INT,
            "PHASE_BREAKDOWN[%lu]: prestart=%llu ms, capture=%llu ms, convert=%llu ms, render=%llu ms (total=%llu ms)",
            frame_count, prestart_ms, capture_ms, convert_ms, render_ms, total_ms);
      }
    } else {
      // Snapshot mode: even if frame conversion failed, check if we should exit
      // This ensures snapshot_delay is honored even if display context isn't rendering
      if (snapshot_mode && snapshot_done) {
        break;
      }
    }

    // In event-driven mode, frame rate limiting is handled by the sleep_cb callback
    // Synchronous mode (with capture context) uses the pipeline and returns early above

    // Note: Images returned by media sources are cached/reused and should NOT be destroyed
    // The image pointers are managed by the source and will be cleaned up on source shutdown
  } // while (!should_exit(user_data)) {

  // Re-enable console logging after rendering completes
  log_set_terminal_output(true);
  if (!snapshot_mode && terminal_is_interactive()) {
    printf("\n");
  }

  // Keyboard input cleanup (if it was initialized)
  if (keyboard_enabled) {
    keyboard_destroy();
    log_debug_every(2 * NS_PER_SEC_INT, "Keyboard input disabled");
  }

  return ASCIICHAT_OK;
}
