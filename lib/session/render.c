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

#include <ascii-chat/session/render.h>
#include <ascii-chat/session/capture.h>
#include <ascii-chat/session/display.h>
#include <ascii-chat/session/help_screen.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/asciichat_errno.h>

#include <stddef.h>
#include <stdbool.h>
#include <math.h>
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
  uint64_t snapshot_start_time_ns = 0;
  bool snapshot_done = false;
  bool snapshot_mode = GET_OPTION(snapshot_mode);

  if (snapshot_mode) {
    snapshot_start_time_ns = time_get_ns();
  }

  // Pause mode state tracking
  bool initial_paused_frame_rendered = false;
  bool was_paused = false;
  bool is_paused = false;

  // Keyboard input initialization (if keyboard handler is provided)
  // Allow keyboard in snapshot mode too (for help screen toggle debugging)
  // Only enable keyboard if BOTH stdin AND stdout are TTYs to avoid buffering issues
  // when tcsetattr() modifies the tty line discipline
  bool keyboard_enabled = false;
  if (keyboard_handler && platform_isatty(STDIN_FILENO) && platform_isatty(STDOUT_FILENO)) {
    // Try to initialize keyboard if both stdin and stdout are TTYs in interactive mode
    asciichat_error_t kb_result = keyboard_init();
    if (kb_result == ASCIICHAT_OK) {
      keyboard_enabled = true;
      log_debug("Keyboard input enabled (TTY mode)");
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

  // Main render loop - works for both synchronous and event-driven modes
  while (!should_exit(user_data)) {
    // Frame timing - measure total time to maintain target FPS
    frame_start_ns = time_get_ns();

    // Frame capture and timing - mode-dependent
    image_t *image;
    uint64_t capture_start_ns = 0;
    uint64_t conversion_start_ns = 0;
    uint64_t render_start_ns = 0;

    if (is_synchronous) {
      capture_start_ns = time_get_ns();
      // SYNCHRONOUS MODE: Use session_capture context

      // Check pause state and handle initial frame rendering
      media_source_t *source = session_capture_get_media_source(capture);
      is_paused = source && media_source_is_paused(source);

      // Detect pause transition - mark initial frame as rendered so polling starts
      if (!was_paused && is_paused) {
        initial_paused_frame_rendered = true;
        log_debug("Media paused, enabling keyboard polling");
      }

      // Detect unpause transition to reset flag
      if (was_paused && !is_paused) {
        initial_paused_frame_rendered = false;
        log_debug("Media unpaused, resuming frame capture");
      }
      was_paused = is_paused;

      // If paused and already rendered initial frame, skip frame capture and poll for resume
      if (is_paused && initial_paused_frame_rendered) {
        // Sleep briefly to avoid busy-waiting while paused
        platform_sleep_usec(1 / GET_OPTION(fps) * 100 * 1000); // ~60 FPS idle rate

        // Keep polling keyboard to allow unpausing (even if keyboard wasn't formally initialized)
        // keyboard_read_nonblocking() is safe to call even if keyboard_init() wasn't called - it just returns KEY_NONE
        if (keyboard_handler) {
          keyboard_key_t key = keyboard_read_nonblocking();
          if (key != KEY_NONE) {
            keyboard_handler(capture, key, user_data);
          }
        }
        continue; // Skip frame capture and rendering, keep loop running
      }

      // Profile: frame capture with detailed retry tracking
      static uint64_t max_retries = 0;
      uint64_t loop_retry_count = 0;
      uint64_t capture_elapsed_ns = 0;

      do {
        log_debug_every(3000000, "RENDER[%lu]: Starting frame read", frame_count);
        image = session_capture_read_frame(capture);
        log_debug_every(3000000, "RENDER[%lu]: Frame read done, image=%p", frame_count, (void *)image);
        capture_elapsed_ns = time_elapsed_ns(capture_start_ns, time_get_ns());

        if (!image) {
          // Check if we've reached end of file for media sources
          if (session_capture_at_end(capture)) {
            log_info("Media source reached end of file");
            break; // Exit render loop - end of media
          }
          loop_retry_count++;

          // Brief delay before retry on temporary frame unavailability
          if (loop_retry_count <= 1 || frame_count % 100 == 0) {
            if (loop_retry_count == 1 && frame_count > 0) {
              log_debug_every(500000, "FRAME_WAIT: retry at frame %lu (waited %.1f ms so far)", frame_count,
                              (double)capture_elapsed_ns / 1000000.0);
            }
          }

          // Track max retry count for diagnostic logging
          if (loop_retry_count > max_retries) {
            max_retries = loop_retry_count;
          }

          // Adaptive sleep: first retry is quick (1ms), subsequent retries use longer delay
          // to give prefetch thread more time to decode HTTP stream frames
          uint64_t sleep_us = (loop_retry_count == 1) ? 1000 : 5000; // 1ms first, then 5ms
          platform_sleep_usec(sleep_us);
          continue;
        }

        // Frame obtained successfully
        if (loop_retry_count > 0) {
          double wait_ms = (double)capture_elapsed_ns / 1000000.0;
          log_debug_every(1000000, "FRAME_OBTAINED: after %lu retries, waited %.1f ms", loop_retry_count, wait_ms);
        }
        break; // Exit retry loop
      } while (true);

      // If we still don't have an image after retries, skip this frame and continue
      // (This happens during network latency or when prefetch thread is catching up)
      if (!image) {
        continue;
      }

      frame_count++;

      // Log capture time every 30 frames
      if (frame_count % 30 == 0) {
        double capture_ms = (double)capture_elapsed_ns / 1000000.0;
        log_info_every(5000000, "PROFILE[%lu]: CAPTURE=%.2f ms", frame_count, capture_ms);
      }

      // Pause after first frame if requested via --pause flag
      // We read the frame first, then pause, so the initial frame is available for rendering
      if (!is_paused && frame_count == 1 && GET_OPTION(pause) && source) {
        media_source_pause(source);
        is_paused = true;
        // Note: initial_paused_frame_rendered will be set in next iteration when pause is detected above
        log_debug("Paused media source after first frame");
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

    // Snapshot mode: check if delay has elapsed (AFTER we have a frame)
    // This ensures we capture at least one frame before considering snapshot done
    if (snapshot_mode && !snapshot_done) {
      uint64_t current_time_ns = time_get_ns();
      double elapsed_sec = time_ns_to_s(time_elapsed_ns(snapshot_start_time_ns, current_time_ns));
      float snapshot_delay = GET_OPTION(snapshot_delay);

      // snapshot_delay=0 means exit immediately after first frame
      // snapshot_delay>0 means wait that many seconds after first frame
      if (elapsed_sec >= snapshot_delay) {
        snapshot_done = true;
      }
    }

    // Convert image to ASCII using display context
    // Handles all palette, terminal caps, width, height, stretch settings
    log_debug_every(3000000, "RENDER[%lu]: Starting ASCII conversion", frame_count);
    conversion_start_ns = time_get_ns();
    char *ascii_frame = session_display_convert_to_ascii(display, image);
    uint64_t conversion_elapsed_ns = time_elapsed_ns(conversion_start_ns, time_get_ns());
    log_debug_every(3000000, "RENDER[%lu]: Conversion done (%.2f ms)", frame_count,
                    (double)conversion_elapsed_ns / 1000000.0);

    if (ascii_frame) {
      // Detect when we have a paused frame (first frame after pausing)
      bool is_paused_frame = initial_paused_frame_rendered && is_paused;

      // When paused with snapshot mode, output the initial frame immediately
      bool output_paused_frame = snapshot_mode && is_paused_frame;

      // Always attempt to render frames; the display context will handle filtering based on:
      // - TTY mode: render all frames with cursor control (even in snapshot mode, for animation)
      // - Piped mode: render all frames without cursor control (for continuous capture)
      // - Snapshot mode on non-TTY: only the display context renders the final frame
      bool should_write = true;
      if (should_write) {
        // is_final = true when: snapshot done, or paused frame (for both snapshot and pause modes)
        // Profile: render frame
        log_debug_every(3000000, "RENDER[%lu]: Starting frame render", frame_count);
        render_start_ns = time_get_ns();

        // Check if help screen is active - if so, render help instead of frame
        if (display && session_display_is_help_active(display)) {
          session_display_render_help(display);
        } else {
          session_display_render_frame(display, ascii_frame);
        }

        uint64_t render_elapsed_ns = time_elapsed_ns(render_start_ns, time_get_ns());
        log_debug_every(3000000, "RENDER[%lu]: Frame render done (%.2f ms)", frame_count,
                        (double)render_elapsed_ns / 1000000.0);
        uint64_t render_complete_ns = time_get_ns();

        // Calculate total time from frame START (frame_start_ns) to render COMPLETE
        frame_to_render_ns = time_elapsed_ns(frame_start_ns, render_complete_ns);
        if (frame_count % 30 == 0) {
          double total_frame_time_ms = (double)frame_to_render_ns / 1000000.0;
          log_warn("ACTUAL_TIME[%lu]: Total frame time from start to render complete: %.1f ms", frame_count,
                   total_frame_time_ms);
        }

        // Log render time every 30 frames
        if (frame_count % 150 == 0) {
          double conversion_ms = (double)conversion_elapsed_ns / 1000000.0;
          double render_ms = (double)render_elapsed_ns / 1000000.0;
          log_info_every(5000000, "PROFILE[%lu]: CONVERT=%.2f ms, RENDER=%.2f ms", frame_count, conversion_ms,
                         render_ms);
        }
      }

      // Keyboard input polling (if enabled) - MUST come before snapshot exit check so help screen can be toggled
      if (keyboard_enabled && keyboard_handler) {
        log_debug_every(3000000, "RENDER[%lu]: Starting keyboard read", frame_count);
        keyboard_key_t key = keyboard_read_nonblocking();
        log_debug_every(3000000, "RENDER[%lu]: Keyboard read done (key=%d)", frame_count, key);
        if (key != KEY_NONE) {
          keyboard_handler(capture, key, user_data);
        }
      }

      // Exit conditions: snapshot mode exits after capturing the final frame or initial paused frame
      if (snapshot_mode && (snapshot_done || output_paused_frame)) {
        SAFE_FREE(ascii_frame);
        break;
      }

      SAFE_FREE(ascii_frame);
    } else {
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
      uint32_t target_fps = session_capture_get_target_fps(capture);
      if (target_fps > 0) {
        uint64_t frame_elapsed_ns = time_elapsed_ns(frame_start_ns, time_get_ns());
        uint64_t frame_target_ns = NS_PER_SEC_INT / target_fps;

        // Only sleep if we have time budget remaining
        // If already behind, skip sleep to catch up
        if (frame_elapsed_ns < frame_target_ns) {
          uint64_t sleep_ns = frame_target_ns - frame_elapsed_ns;
          uint64_t sleep_us = sleep_ns / 1000;
          // Sleep in 1ms chunks to respond to interrupts faster
          if (sleep_us > 1000) {
            platform_sleep_usec(sleep_us - 500); // Reserve 500us for overhead
          }
        }
      }
    }

    // Note: Images returned by media sources are cached/reused and should NOT be destroyed
    // The image pointers are managed by the source and will be cleaned up on source shutdown
  } // while (!should_exit(user_data)) {

  // Keyboard input cleanup (if it was initialized)
  if (keyboard_enabled) {
    keyboard_cleanup();
    log_debug("Keyboard input disabled");
  }

  return ASCIICHAT_OK;
}
