/**
 * @file session/keyboard_handler.c
 * @brief Keyboard input handler implementation
 * @ingroup session
 */

#include "keyboard_handler.h"
#include "capture.h"
#include "media/source.h"
#include "options/options.h"
#include "log/logging.h"
#include <string.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/** Muted volume state (used to restore volume when unmuting) */
static double g_mute_saved_volume = 0.5;

/**
 * @brief Clamp volume to valid range [0.0, 2.0]
 */
static double clamp_volume(double volume) {
  if (volume < 0.0) {
    return 0.0;
  }
  if (volume > 2.0) {
    return 2.0;
  }
  return volume;
}

/**
 * @brief Get next color mode in cycle
 */
static int next_color_mode(int current) {
  // Cycle: TERM_COLOR_NONE → TERM_COLOR_16 → TERM_COLOR_256 → TERM_COLOR_TRUECOLOR → TERM_COLOR_NONE
  switch (current) {
  case 0:     // TERM_COLOR_NONE
    return 1; // TERM_COLOR_16
  case 1:     // TERM_COLOR_16
    return 2; // TERM_COLOR_256
  case 2:     // TERM_COLOR_256
    return 3; // TERM_COLOR_TRUECOLOR
  default:
    return 0; // Back to TERM_COLOR_NONE
  }
}

/* ============================================================================
 * Keyboard Handler
 * ============================================================================ */

void session_handle_keyboard_input(session_capture_ctx_t *capture, int key) {
  switch (key) {
  // ===== SEEK CONTROLS (file sources only) =====
  case 258: { // KEY_LEFT - Seek backward 30 seconds
    if (capture) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(capture);
      if (source && media_source_get_type(source) == MEDIA_SOURCE_FILE) {
        double current_pos = media_source_get_position(source);
        if (current_pos >= 0.0) {
          double new_pos = current_pos - 30.0;
          if (new_pos < 0.0) {
            new_pos = 0.0;
          }
          asciichat_error_t err = media_source_seek(source, new_pos);
          if (err == ASCIICHAT_OK) {
            log_info("Seeked backward to %.1f seconds", new_pos);
          }
        }
      }
    }
    break;
  }

  case 259: { // KEY_RIGHT - Seek forward 30 seconds
    if (capture) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(capture);
      if (source && media_source_get_type(source) == MEDIA_SOURCE_FILE) {
        double current_pos = media_source_get_position(source);
        double duration = media_source_get_duration(source);
        if (current_pos >= 0.0) {
          double new_pos = current_pos + 30.0;
          // Clamp to duration if known
          if (duration > 0.0 && new_pos > duration) {
            new_pos = duration;
          }
          asciichat_error_t err = media_source_seek(source, new_pos);
          if (err == ASCIICHAT_OK) {
            log_info("Seeked forward to %.1f seconds", new_pos);
          }
        }
      }
    }
    break;
  }

  // ===== VOLUME CONTROLS =====
  case 257: { // KEY_DOWN - Decrease volume 10%
    double current_volume = GET_OPTION(speakers_volume);
    double new_volume = clamp_volume(current_volume - 0.1);
    options_set_double("speakers_volume", new_volume);
    log_info("Volume: %.0f%%", new_volume * 100.0);
    break;
  }

  case 256: { // KEY_UP - Increase volume 10%
    double current_volume = GET_OPTION(speakers_volume);
    double new_volume = clamp_volume(current_volume + 0.1);
    options_set_double("speakers_volume", new_volume);
    log_info("Volume: %.0f%%", new_volume * 100.0);
    break;
  }

  // ===== PLAY/PAUSE CONTROL =====
  case 32: { // KEY_SPACE - Toggle play/pause
    if (capture) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(capture);
      if (source && media_source_get_type(source) == MEDIA_SOURCE_FILE) {
        media_source_toggle_pause(source);
        if (media_source_is_paused(source)) {
          log_info("Paused");
        } else {
          log_info("Playing");
        }
      }
    }
    break;
  }

  // ===== COLOR MODE CONTROL =====
  case 'c': {
    int current_mode = (int)GET_OPTION(color_mode);
    int next_mode = next_color_mode(current_mode);
    options_set_int("color_mode", next_mode);

    const char *mode_names[] = {"Mono", "16-color", "256-color", "Truecolor"};
    if (next_mode >= 0 && next_mode <= 3) {
      log_info("Color mode: %s", mode_names[next_mode]);
    }
    break;
  }

  // ===== MUTE CONTROL =====
  case 'm': {
    double current_volume = GET_OPTION(speakers_volume);
    if (current_volume > 0.01) { // If not already muted
      // Save current volume and mute
      g_mute_saved_volume = current_volume;
      options_set_double("speakers_volume", 0.0);
      log_info("Muted");
    } else {
      // Restore previous volume
      double restore_volume = g_mute_saved_volume > 0.0 ? g_mute_saved_volume : 0.5;
      options_set_double("speakers_volume", restore_volume);
      log_info("Unmuted: %.0f%%", restore_volume * 100.0);
    }
    break;
  }

  // ===== WEBCAM FLIP CONTROL =====
  case 'f': {
    bool current_flip = (bool)GET_OPTION(webcam_flip);
    options_set_bool("webcam_flip", !current_flip);
    log_info("Webcam flip: %s", !current_flip ? "enabled" : "disabled");
    break;
  }

  default:
    // Unknown key - silently ignore
    break;
  }
}
