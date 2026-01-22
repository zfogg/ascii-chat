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
#include "asciichat_errno.h"

#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Unified Render Loop Implementation
 * ============================================================================ */

asciichat_error_t session_render_loop(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                      session_should_exit_fn should_exit, session_capture_fn capture_cb,
                                      session_sleep_for_frame_fn sleep_cb, void *user_data) {
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

  // Determine mode: synchronous (capture provided) or event-driven (callbacks provided)
  bool is_synchronous = (capture != NULL);

  // Audio/video sync debug tracking
  uint64_t frame_count = 0;

  // Main render loop - works for both synchronous and event-driven modes
  while (!should_exit(user_data)) {
    // Frame timing
    uint64_t current_time_ns = time_get_ns();

    // Snapshot mode: check if delay has elapsed
    if (snapshot_mode && !snapshot_done) {
      double elapsed_sec = time_ns_to_s(time_elapsed_ns(snapshot_start_time_ns, current_time_ns));
      float snapshot_delay = GET_OPTION(snapshot_delay);

      if (elapsed_sec >= snapshot_delay) {
        snapshot_done = true;
      }
    }

    // Frame capture and timing - mode-dependent
    image_t *image;

    if (is_synchronous) {
      // SYNCHRONOUS MODE: Use session_capture context
      session_capture_sleep_for_fps(capture);
      image = session_capture_read_frame(capture);

      if (!image) {
        // Check if we've reached end of file for media sources
        if (session_capture_at_end(capture)) {
          log_info("Media source reached end of file");
          break; // Exit render loop - end of media
        }
        // Brief delay before retry on temporary frame unavailability
        platform_sleep_usec(10000); // 10ms
        continue;
      }

      // Debug: log audio/video positions every ~30 frames (~500ms at 60fps)
      frame_count++;
      if (frame_count % 30 == 0) {
        void *media_src = session_capture_get_media_source(capture);
        if (media_src) {
          // Get both decoder positions from media source
          double video_pos = media_source_get_position((media_source_t *)media_src);
          log_info_every(5000000, "A/V SYNC DEBUG: frame=%lu, video_pos=%.3f sec", frame_count, video_pos);
        }
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

    // NOTE: Audio is NOT written from render loop in mirror mode
    // Audio timing must match PortAudio sample rate, not video frame rate
    // The PortAudio callback reads audio on-demand directly from the media source

    // Convert image to ASCII using display context
    // Handles all palette, terminal caps, width, height, stretch settings
    char *ascii_frame = session_display_convert_to_ascii(display, image);

    if (ascii_frame) {
      // When piping/redirecting in snapshot mode, only output the final frame
      // When outputting to TTY, show live preview frames
      bool should_write = !snapshot_mode || session_display_has_tty(display) || snapshot_done;
      if (should_write) {
        session_display_render_frame(display, ascii_frame, snapshot_done);
      }

      // Snapshot mode: exit after capturing the final frame
      if (snapshot_mode && snapshot_done) {
        SAFE_FREE(ascii_frame);
        // NOTE: Do NOT free 'image' - ownership depends on source:
        // - Synchronous: owned by capture context
        // - Event-driven: owned by caller (capture_cb)
        break;
      }

      SAFE_FREE(ascii_frame);
    }

    // NOTE: Do NOT free 'image':
    // - Synchronous mode: owned by capture context and reused on next read
    // - Event-driven mode: owned by caller (capture callback)
  }

  return ASCIICHAT_OK;
}
