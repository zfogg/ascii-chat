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
#include "session/stdin_reader.h"
#include <ascii-chat/ui/help_screen.h>
#include <ascii-chat/log/interactive_grep.h>
#include <ascii-chat/common.h>
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

asciichat_error_t session_render_loop(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                      session_should_exit_fn should_exit, session_capture_fn capture_cb,
                                      session_sleep_for_frame_fn sleep_cb, session_keyboard_handler_fn keyboard_handler,
                                      void *user_data) {
  // Validate required parameters
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

  // Snapshot mode state tracking
  bool snapshot_mode = GET_OPTION(snapshot_mode);
  uint64_t snapshot_start_time_ns = (snapshot_mode ? time_get_ns() : 0); // Initialize immediately in snapshot mode
  bool snapshot_done = false;
  bool first_frame_rendered = false;

  // Help screen state tracking for clear-screen transition
  bool help_was_active = false;

  // Terminal resize tracking (for auto_width/auto_height mode)
  unsigned short int last_terminal_width = terminal_get_effective_width();
  unsigned short int last_terminal_height = terminal_get_effective_height();

  log_info("session_render_loop: STARTING - display=%p capture=%p capture_cb=%p snapshot_mode=%s snapshot_delay=%.2f",
           (void *)display, (void *)capture, (void *)capture_cb, snapshot_mode ? "YES" : "NO",
           snapshot_mode ? GET_OPTION(snapshot_delay) : 0.0);

  // Pause mode state tracking
  bool initial_paused_frame_rendered = false;
  bool was_paused = false;
  bool is_paused = false;

  // Keyboard input initialization (if keyboard handler is provided)
  // Disable keyboard only in snapshot mode
  // Allow keyboard input in all other modes, even if stdin/stdout aren't TTYs
  bool keyboard_enabled = false;
  if (keyboard_handler && !snapshot_mode) {
    // Try to initialize keyboard input
    asciichat_error_t kb_result = keyboard_init();
    if (kb_result == ASCIICHAT_OK) {
      keyboard_enabled = true;
      log_debug("Keyboard input enabled");
    } else {
      log_debug("Failed to initialize keyboard input (%s) - will attempt fallback", asciichat_error_string(kb_result));
      // Don't fail - continue with keyboard handler (will try to read anyway)
      keyboard_enabled = true; // Allow trying to read keyboard even if init failed
    }
  }

  // Determine mode: synchronous (capture provided) or event-driven (callbacks provided)
  bool is_synchronous = (capture != NULL);

  // Frame rate timing
  uint64_t frame_count = 0;
  uint64_t frame_start_ns = 0;
  uint64_t frame_to_render_ns = 0;

  // Disable console logging during rendering to prevent logs from corrupting frame display
  // This must be done BEFORE the render loop to avoid repeated lock/unlock cycles that could cause deadlocks
  log_set_terminal_output(false);

  // Main render loop
  log_debug("session_render_loop: entering main loop");
  int loop_iteration = 0;
  while (!should_exit(user_data)) {
    loop_iteration++;
    if (loop_iteration % 60 == 0) {
      log_debug("session_render_loop: iteration %d, should_exit check returning false", loop_iteration);
    }
    // Snapshot mode: exit at start of iteration if done
    // This prevents frame 2+ from being captured when snapshot_delay has elapsed
    if (snapshot_mode && snapshot_done) {
      log_debug("Snapshot mode: exiting at loop iteration start");
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

    if (is_synchronous) {
      capture_start_ns = time_get_ns();
      // SYNCHRONOUS MODE: Use session_capture context

      // Check if we're in stdin render mode (reading ASCII frames from stdin)
      stdin_frame_reader_t *stdin_reader = (stdin_frame_reader_t *)session_display_get_stdin_reader(display);
      if (stdin_reader) {
        // STDIN RENDER MODE: Read ASCII frame text directly from stdin
        char *ascii_frame = NULL;
        asciichat_error_t stdin_err = stdin_frame_reader_next(stdin_reader, &ascii_frame);
        if (stdin_err != ASCIICHAT_OK) {
          log_error_every(200 * MS_PER_SEC_INT, "Failed to read stdin frame: %s", asciichat_error_string(stdin_err));
          break; // Exit render loop on error
        }

        if (!ascii_frame) {
          // EOF reached - exit render loop
          log_info_every(200 * MS_PER_SEC_INT, "stdin_render_mode: EOF reached, exiting render loop");
          break;
        }

        frame_count++;
        log_debug_every(1 * NS_PER_SEC_INT, "RENDER[%lu]: Read ASCII frame from stdin (%zu bytes)", frame_count,
                        strlen(ascii_frame));

        // In stdin render mode, check if rendering to stdout or to file
        const char *render_file_opt = GET_OPTION(render_file);
        if (render_file_opt && strcmp(render_file_opt, "-") == 0) {
          // Render to stdout: write raw ASCII frame directly
          platform_write_all(STDOUT_FILENO, ascii_frame, strlen(ascii_frame));
          platform_write_all(STDOUT_FILENO, "\n", 1);
        } else {
          // Render to file: use display rendering (will use render_file if configured)
          session_display_render_frame(display, ascii_frame);
        }

        SAFE_FREE(ascii_frame);

        // Handle keyboard input (enabled for stdin render mode regardless of TTY status)
        if (keyboard_handler) {
          keyboard_key_t key = keyboard_read_nonblocking();
          if (key != KEY_NONE) {
            if (interactive_grep_should_handle(key)) {
              interactive_grep_handle_key(key);
            } else {
              keyboard_handler(capture, key, user_data);
            }
          }
        }

        // Frame timing for stdin mode
        uint64_t frame_end_ns = time_get_ns();
        uint64_t frame_elapsed_ns = time_elapsed_ns(frame_start_ns, frame_end_ns);
        uint64_t target_frame_ns = (uint64_t)(NS_PER_SEC_INT / GET_OPTION(fps));

        // Sleep to maintain target FPS if frame was faster than target
        if (frame_elapsed_ns < target_frame_ns) {
          uint64_t sleep_ns = target_frame_ns - frame_elapsed_ns;
          platform_sleep_ns(sleep_ns);
        }

        // Skip the normal image capture for this frame
        continue;
      }

      // Check pause state and handle initial frame rendering
      media_source_t *source = session_capture_get_media_source(capture);
      is_paused = source && media_source_is_paused(source);

      // Detect pause transition - mark initial frame as rendered so polling starts
      if (!was_paused && is_paused) {
        initial_paused_frame_rendered = true;
        log_debug_every(1 * NS_PER_SEC_INT, "Media paused, enabling keyboard polling");
      }

      // Detect unpause transition to reset flag
      if (was_paused && !is_paused) {
        initial_paused_frame_rendered = false;
        log_debug_every(1 * NS_PER_SEC_INT, "Media unpaused, resuming frame capture");
      }
      was_paused = is_paused;

      // If paused and already rendered initial frame, skip frame capture and poll for resume
      if (is_paused && initial_paused_frame_rendered) {
        // Sleep briefly to avoid busy-waiting while paused
        uint64_t idle_sleep_ns = (uint64_t)(NS_PER_SEC_INT / GET_OPTION(fps)); // Frame period in nanoseconds
        platform_sleep_ns(idle_sleep_ns);

        // Keep polling keyboard to allow unpausing (even if keyboard wasn't formally initialized)
        // keyboard_read_nonblocking() is safe to call even if keyboard_init() wasn't called - it just returns KEY_NONE
        if (keyboard_handler) {
          keyboard_key_t key = keyboard_read_nonblocking();
          if (key != KEY_NONE) {
            // Check if interactive grep should handle this key
            if (interactive_grep_should_handle(key)) {
              interactive_grep_handle_key(key);
              continue; // Force immediate re-render
            }

            keyboard_handler(capture, key, user_data);
          }
        }
        continue; // Skip frame capture and rendering, keep loop running
      }

      // Profile: frame capture with poll-based blocking
      uint64_t loop_retry_count = 0;
      uint64_t capture_elapsed_ns = 0;

      do {
        // Check for exit request before blocking on frame read
        if (should_exit(user_data)) {
          break;
        }

        log_debug_every(3 * US_PER_SEC_INT, "RENDER[%lu]: Starting frame read", frame_count);
        image = session_capture_read_frame(capture);
        log_info_every(1 * NS_PER_SEC_INT, "RENDER[%lu]: Frame read done, image=%p", frame_count, (void *)image);
        capture_elapsed_ns = time_elapsed_ns(capture_start_ns, time_get_ns());

        if (!image) {
          // Check if we've reached end of file for media sources
          if (session_capture_at_end(capture)) {
            log_info_every(1 * NS_PER_SEC_INT, "Media source reached end of file");
            break; // Exit render loop - end of media
          }

          // Check for exit request
          if (should_exit(user_data)) {
            break;
          }

          // With poll()-based blocking reads, timeouts mean no frame available
          // Retry once with minimal delay to handle transient state
          loop_retry_count++;
          if (loop_retry_count > 1) {
            // No frame after retry - skip this iteration rather than spinning
            log_debug_every(1 * NS_PER_SEC_INT, "FRAME_SKIP: No frame available after %lu retries", loop_retry_count);
            loop_retry_count = 0;
            continue;
          }

          // Single retry with 1ms delay (poll already waited 100ms)
          platform_sleep_us(1 * US_PER_MS_INT); // 1ms
          continue;
        }

        // Frame obtained successfully
        if (loop_retry_count > 0) {
          double wait_ms = (double)capture_elapsed_ns / NS_PER_MS;
          log_debug_every(US_PER_SEC_INT, "FRAME_OBTAINED: after %lu retries, waited %.1f ms", loop_retry_count,
                          wait_ms);
        }
        break; // Exit retry loop
      } while (true);

      // If we still don't have an image after retries, check if we've reached end of file
      // (This happens during network latency or when prefetch thread is catching up)
      if (!image) {
        // Exit main render loop if we've reached end of media file
        if (session_capture_at_end(capture)) {
          log_debug("[SHUTDOWN] Media EOF detected, exiting render loop");
          break;
        }
        continue;
      }

      // Capture phase complete - record timestamp for phase breakdown
      capture_end_ns = time_get_ns();

      frame_count++;

      // Log capture time every 30 frames
      if (frame_count % 30 == 0) {
        char capture_str[32];
        time_pretty(capture_elapsed_ns, -1, capture_str, sizeof(capture_str));
        log_dev_every(5 * US_PER_SEC_INT, "PROFILE[%lu]: CAPTURE=%s", frame_count, capture_str);
      }

      // Pause after first frame if requested via --pause flag
      // We read the frame first, then pause, so the initial frame is available for rendering
      if (!is_paused && frame_count == 1 && GET_OPTION(pause) && source) {
        media_source_pause(source);
        is_paused = true;
        // Note: initial_paused_frame_rendered will be set in next iteration when pause is detected above
        log_debug_every(1 * NS_PER_SEC_INT, "Paused media source after first frame");
      }

    } else {
      // Both sleep_cb and capture_cb are guaranteed non-NULL by validation above
      sleep_cb(user_data);
      image = capture_cb(user_data);

      if (!image) {
        // No frame available - this is normal in async modes (network latency, etc.)
        // Just continue to next iteration, don't exit
        continue;
      }
    }

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
    uint64_t conversion_elapsed_ns = post_convert_ns - pre_convert_ns;

    if (ascii_frame) {
      log_info_every(1 * NS_PER_SEC_INT, "render_loop: ascii_frame ready (len=%zu)", strlen(ascii_frame));
      // Detect when we have a paused frame (first frame after pausing)
      bool is_paused_frame = initial_paused_frame_rendered && is_paused;

      // When paused with snapshot mode, output the initial frame immediately
      bool output_paused_frame = snapshot_mode && is_paused_frame;

      // Always attempt to render frames; the display context will handle filtering based on:
      // - TTY mode: render all frames with cursor control (even in snapshot mode, for animation)
      // - Piped mode: render all frames without cursor control (for continuous capture)
      // - Snapshot mode on non-TTY: only the display context renders the final frame
      bool should_write = true;
      uint64_t pre_render_ns = 0, post_render_ns = 0; // Declare outside if block for timing
      if (should_write) {
        // is_final = true when: snapshot done, or paused frame (for both snapshot and pause modes)
        // Profile: render frame
        pre_render_ns = time_get_ns();
        START_TIMER("render_frame");

        log_info_every(1 * NS_PER_SEC_INT, "render_loop: calling session_display_render_frame - display=%p",
                       (void *)display);
        // Check if help screen is active - if so, render help instead of frame
        // Help screen is disabled in snapshot mode and non-interactive terminals (keyboard disabled)
        bool help_is_active = display && session_display_is_help_active(display);

        // Detect transition from help to ASCII art rendering
        // When help closes, clear the screen before rendering ASCII art
        if (help_was_active && !help_is_active) {
          terminal_clear_screen();
          log_debug_every(1 * NS_PER_SEC_INT, "Cleared screen when transitioning from help to ASCII art");
        }

        if (help_is_active) {
          session_display_render_help(display);
        } else {
          session_display_render_frame(display, ascii_frame);
        }

        // Update help state for next iteration
        help_was_active = help_is_active;

        uint64_t render_elapsed_ns = STOP_TIMER("render_frame");
        post_render_ns = time_get_ns();

        // Calculate total time from frame START (frame_start_ns) to render COMPLETE
        frame_to_render_ns = time_elapsed_ns(frame_start_ns, post_render_ns);
        double total_frame_time_ms = (double)frame_to_render_ns / NS_PER_MS;
        log_dev_every(5 * US_PER_SEC_INT, "ACTUAL_TIME[%lu]: Total frame time from start to render complete: %.1f ms",
                      frame_count, total_frame_time_ms);

        char conversion_str[32], render_str[32];
        time_pretty(conversion_elapsed_ns, -1, conversion_str, sizeof(conversion_str));
        time_pretty(render_elapsed_ns, -1, render_str, sizeof(render_str));
        log_dev_every(5 * US_PER_SEC_INT, "PROFILE[%lu]: CONVERT=%s, RENDER=%s", frame_count, conversion_str,
                      render_str);
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
          if (interactive_grep_should_handle(key)) {
            log_debug("KEYBOARD: Grep handler taking key %d", key);
            interactive_grep_handle_key(key);
            continue; // Force immediate re-render
          }

          // Normal keyboard handler
          log_debug("KEYBOARD: Normal handler taking key %d", key);
          keyboard_handler(capture, key, user_data);
        }
      }

      // Free frame before checking exit conditions to avoid double-free
      SAFE_FREE(ascii_frame);

      // Snapshot mode timing: mark that first frame has been rendered
      // (timer already started at the beginning of the render loop)
      if (snapshot_mode && !first_frame_rendered) {
        first_frame_rendered = true;
        log_dev_every(1 * NS_PER_SEC_INT, "Snapshot mode: first frame rendered");
      }

      // Snapshot mode: check if delay has elapsed after rendering a frame
      if (snapshot_mode && !snapshot_done && first_frame_rendered) {
        uint64_t current_time_ns = time_get_ns();
        double elapsed_sec = time_ns_to_s(time_elapsed_ns(snapshot_start_time_ns, current_time_ns));
        double snapshot_delay = GET_OPTION(snapshot_delay);

        log_debug_every(US_PER_SEC_INT, "SNAPSHOT_DELAY_CHECK: elapsed=%.2f delay=%.2f", elapsed_sec, snapshot_delay);

        // snapshot_delay=0 means exit immediately after rendering first frame
        // snapshot_delay>0 means wait that many seconds after render loop start
        if (elapsed_sec >= snapshot_delay) {
          // We don't end frames with newlines so the next log would print on the same line as the frame's
          // last row without an \n here. We only need this \n in stdout in snapshot mode and when interactive,
          // so piped snapshots don't have a weird newline in stdout that they don't need.
          if (snapshot_mode && terminal_is_interactive()) {
            printf("\n");
          }
          log_info_every(1 * NS_PER_SEC_INT, "Snapshot delay %.2f seconds elapsed, exiting", snapshot_delay);
          snapshot_done = true;
        }
      }

      // Exit conditions: snapshot mode exits after capturing the final frame or initial paused frame
      if (snapshot_mode && (snapshot_done || output_paused_frame)) {
        // Signal application to exit in snapshot mode
        APP_CALLBACK_VOID(signal_exit);
        break;
      }

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

    // Audio-Video Synchronization: Keep audio and video in sync by periodically adjusting audio to match video time
    // DISABLED: Audio sync causes seeking every 1 second, which interrupts playback
    // For file/media playback (mirror mode), audio and video decode independently
    // and don't need synchronization - they play at their natural rates from the file.
    // Audio sync is only needed for multi-client video conferencing (server mode).
    //
    // if (is_synchronous && capture && image && frame_count % 30 == 0) {
    //   media_source_sync_audio_to_video(source);
    // }

    // Frame rate limiting: Only sleep if we're ahead of schedule
    // If decoder is slow and we're already behind, don't add extra sleep
    if (is_synchronous && capture) {
      // Use user-specified FPS from options, not capture context FPS
      uint32_t target_fps = (uint32_t)GET_OPTION(fps);
      if (target_fps > 0) {
        uint64_t frame_end_ns = time_get_ns();
        uint64_t frame_elapsed_ns = time_elapsed_ns(frame_start_ns, frame_end_ns);
        uint64_t frame_target_ns = NS_PER_SEC_INT / target_fps;

        char frame_time_str[32], target_time_str[32];
        time_pretty(frame_elapsed_ns, -1, frame_time_str, sizeof(frame_time_str));
        time_pretty(frame_target_ns, -1, target_time_str, sizeof(target_time_str));
        log_dev_every(500 * NS_PER_MS_INT, "RENDER[%lu] TIMING_TOTAL: frame_time=%s target_time=%s", frame_count,
                      frame_time_str, target_time_str);

        // Only sleep if we have time budget remaining
        // If already behind, skip sleep to catch up
        if (frame_elapsed_ns < frame_target_ns) {
          uint64_t sleep_ns = frame_target_ns - frame_elapsed_ns;
          // Sleep with 500us overhead reserved for recovery
          if (sleep_ns > 500 * US_PER_MS_INT) {
            platform_sleep_ns((sleep_ns - 500 * US_PER_MS_INT));
          }
        }
      }
    }

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
