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

#include "render.h"
#include "capture.h"
#include "display.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "util/time.h"
#include "audio/audio.h"
#include "media/source.h"
#include "platform/keyboard.h"
#include "asciichat_errno.h"

#include <stddef.h>
#include <stdbool.h>
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
  // Disable keyboard in snapshot mode only - don't put stdin into raw mode in snapshot mode
  bool keyboard_enabled = false;
  if (keyboard_handler && !snapshot_mode) {
    // Try to initialize keyboard if stdin is a TTY
    if (platform_isatty(STDIN_FILENO)) {
      asciichat_error_t kb_result = keyboard_init();
      if (kb_result == ASCIICHAT_OK) {
        keyboard_enabled = true;
        log_debug("Keyboard input enabled (TTY mode)");
      } else {
        log_debug("Failed to initialize keyboard input (%s) - will attempt fallback",
                  asciichat_error_string(kb_result));
        // Don't fail - continue with keyboard handler (will try to read anyway)
        keyboard_enabled = true; // Allow trying to read keyboard even if init failed
      }
    } else {
      // Not a TTY, but we still allow keyboard attempts
      log_debug("Keyboard requested but stdin is not a TTY - will attempt non-raw reads");
      keyboard_enabled = true; // Allow trying to read keyboard without raw mode
    }
  }

  // Determine mode: synchronous (capture provided) or event-driven (callbacks provided)
  bool is_synchronous = (capture != NULL);

  // Frame rate timing
  uint64_t frame_count = 0;
  uint64_t frame_start_ns = 0;
  uint64_t prev_render_ns = 0; // Track when we actually rendered the last frame
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
        platform_sleep_usec(16666); // ~60 FPS idle rate

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

      // Profile: frame capture
      image = session_capture_read_frame(capture);
      uint64_t capture_elapsed_ns = time_elapsed_ns(capture_start_ns, time_get_ns());

      if (!image) {
        // Check if we've reached end of file for media sources
        if (session_capture_at_end(capture)) {
          log_info("Media source reached end of file");
          break; // Exit render loop - end of media
        }
        // Brief delay before retry on temporary frame unavailability
        if (frame_count > 0 && frame_count % 100 == 0) {
          log_debug("FRAME_STALL: decoder not ready at frame %lu (waiting for data)", frame_count);
        }
        platform_sleep_usec(10000); // 10ms
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
      // EVENT-DRIVEN MODE: Use custom callbacks
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
    conversion_start_ns = time_get_ns();
    char *ascii_frame = session_display_convert_to_ascii(display, image);
    uint64_t conversion_elapsed_ns = time_elapsed_ns(conversion_start_ns, time_get_ns());

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
        bool is_final = snapshot_done || is_paused_frame;

        // Profile: render frame
        render_start_ns = time_get_ns();
        session_display_render_frame(display, ascii_frame, is_final);
        uint64_t render_elapsed_ns = time_elapsed_ns(render_start_ns, time_get_ns());
        uint64_t render_complete_ns = time_get_ns();
        prev_render_ns = render_complete_ns; // Record when this render completed

        // Calculate total time from frame START (frame_start_ns) to render COMPLETE
        frame_to_render_ns = time_elapsed_ns(frame_start_ns, render_complete_ns);
        if (frame_count % 30 == 0) {
          double total_frame_time_ms = (double)frame_to_render_ns / 1000000.0;
          log_warn("ACTUAL_TIME[%lu]: Total frame time from start to render complete: %.1f ms", frame_count,
                   total_frame_time_ms);
        }

        // Log render time every 30 frames
        if (frame_count % 30 == 0) {
          double conversion_ms = (double)conversion_elapsed_ns / 1000000.0;
          double render_ms = (double)render_elapsed_ns / 1000000.0;
          log_info_every(5000000, "PROFILE[%lu]: CONVERT=%.2f ms, RENDER=%.2f ms", frame_count, conversion_ms,
                         render_ms);
        }
      }

      // Exit conditions: snapshot mode exits after capturing the final frame or initial paused frame
      if (snapshot_mode && (snapshot_done || output_paused_frame)) {
        SAFE_FREE(ascii_frame);
        // NOTE: Do NOT free 'image' - ownership depends on source:
        // - Synchronous: owned by capture context
        // - Event-driven: owned by caller (capture_cb)
        break;
      }

      SAFE_FREE(ascii_frame);
    } else {
    }

    // Keyboard input polling (if enabled)
    if (keyboard_enabled && keyboard_handler) {
      keyboard_key_t key = keyboard_read_nonblocking();
      if (key != KEY_NONE) {
        keyboard_handler(capture, key, user_data);
      }
    }

    // Maintain target frame rate by sleeping only for remaining time
    // Apply frame rate limiting in all modes, including snapshot (so animation plays at correct speed)
    if (is_synchronous && capture) {
      uint32_t target_fps = session_capture_get_target_fps(capture);
      if (target_fps > 0) {
        uint64_t frame_elapsed_ns = time_elapsed_ns(frame_start_ns, time_get_ns());
        uint64_t frame_target_ns = NS_PER_SEC_INT / target_fps;

        // Profile: total frame time
        double frame_ms = (double)frame_elapsed_ns / 1000000.0;
        double target_ms = (double)frame_target_ns / 1000000.0;

        if (frame_elapsed_ns < frame_target_ns) {
          uint64_t sleep_ns = frame_target_ns - frame_elapsed_ns;
          uint64_t sleep_us = sleep_ns / 1000;
          if (sleep_us > 0) {
            platform_sleep_usec(sleep_us);
          }
          if (frame_count % 30 == 0) {
            log_info("TIMING[%lu]: ops=%.2f+sleep %.0f ms = %.2fms (%.0f%% utilized)", frame_count, frame_ms,
                     target_ms - frame_ms, target_ms, (frame_ms / target_ms) * 100.0);
          }
        } else {
          // Operations exceeded target frame time - no sleep needed
          double overrun_ms = frame_ms - target_ms;
          log_warn("TIMING[%lu]: OVERRUN %.2f ms (ops=%.2f, budget=%.2f)", frame_count, overrun_ms, frame_ms,
                   target_ms);
        }
      }

      // Debug: log actual render FPS every 30 frames
      if (frame_count % 30 == 0) {
        double actual_fps = session_capture_get_current_fps(capture);
        uint32_t target_fps = session_capture_get_target_fps(capture);
        void *media_src = session_capture_get_media_source(capture);
        double video_pos = media_src ? media_source_get_position((media_source_t *)media_src) : -1.0;
        log_info_every(5000000, "RENDER: frame=%lu, actual=%.1f FPS, target=%u FPS, pos=%.3f sec", frame_count,
                       actual_fps, target_fps, video_pos);
      }
    }

    // Clean up image if it's from a webcam source (they allocate fresh frames)
    // Decoder images are cached and owned by the decoder, so we must NOT free those
    if (is_synchronous && capture) {
      media_source_t *source = session_capture_get_media_source(capture);
      if (source && media_source_get_type(source) == MEDIA_SOURCE_WEBCAM && image) {
        image_destroy(image);
      }
    }
  }

  // Keyboard input cleanup (if it was initialized)
  if (keyboard_enabled) {
    keyboard_cleanup();
    log_debug("Keyboard input disabled");
  }

  return ASCIICHAT_OK;
}
