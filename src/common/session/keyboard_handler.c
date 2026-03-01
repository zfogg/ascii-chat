/**
 * @file session/keyboard_handler.c
 * @brief Keyboard input handler implementation
 * @ingroup session
 */

#include "session/keyboard_handler.h"
#include "session/capture.h"
#include "session/display.h"
#include <ascii-chat/ui/help_screen.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/platform/terminal.h>
#include <string.h>
#include <signal.h>

#ifndef NDEBUG
#include <ascii-chat/debug/sync.h>
#endif

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/** Muted volume state (used to restore volume when unmuting) - default 100% */
static double g_mute_saved_volume = 1.0;

/**
 * @brief Clamp volume to valid range [0.0, 1.0]
 *
 * Restricts volume to the valid audio range where:
 * - 0.0 = silent (mute)
 * - 0.7 = default/normal level (70%)
 * - 1.0 = maximum volume (100%)
 *
 * @param volume Unclamped volume value
 * @return Volume clamped to [0.0, 1.0] range
 */
static double clamp_volume(double volume) {
  if (volume < 0.0) {
    return 0.0;
  }
  if (volume > 1.0) {
    return 1.0;
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

/**
 * @brief Get next render mode in cycle
 */
static int next_render_mode(int current) {
  // 3 render modes: FOREGROUND (0), BACKGROUND (1), HALF_BLOCK (2)
  return (current + 1) % 3;
}

/**
 * @brief Get next color filter in cycle
 */
static int next_color_filter(int current) {
  return (current + 1) % COLOR_FILTER_COUNT;
}

/* ============================================================================
 * Keyboard Handler
 * ============================================================================ */

void session_handle_keyboard_input(session_capture_ctx_t *capture, session_display_ctx_t *display, keyboard_key_t key) {
  // Debug: log all key codes to help identify unknown keys
  if (key != KEY_NONE) {
    log_debug("Keyboard input received: code=%d (0x%02x) char='%c'", key, key, (key >= 32 && key < 127) ? key : '?');
  }

  switch ((int)key) {
  // ===== HELP SCREEN TOGGLE =====
  case KEY_QUESTION: {
    log_debug("KEYBOARD: KEY_QUESTION matched, display=%p", (void *)display);
    if (display) {
      log_info("KEYBOARD: Toggling help screen");
      session_display_toggle_help(display);
      // Render help screen immediately so user sees it
      session_display_render_help(display);
    } else {
      log_warn("KEYBOARD: Cannot toggle help - display context is NULL");
    }
    break;
  }

  // ===== HELP SCREEN CLOSE / QUIT =====
  case KEY_ESCAPE: {
    if (display && session_display_is_help_active(display)) {
      // Close help screen if it's active
      session_display_toggle_help(display);
      terminal_clear_screen();
    } else {
      // If help screen is not active, quit the app (like Ctrl-C)
      // The signal handler will gracefully shutdown all modes (client, server, mirror, etc.)
      raise(SIGINT);
    }
    break;
  }

  // ===== SEEK CONTROLS (file sources only) =====
  case KEY_LEFT: { // Seek backward 30 seconds
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
          void *audio_ctx = session_capture_get_audio_context(capture);
          if (audio_ctx) {
            audio_flush_playback_buffers((audio_context_t *)audio_ctx);
          }
        }
      }
    }
    break;
  }

  case KEY_RIGHT: { // Seek forward 30 seconds
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
          void *audio_ctx = session_capture_get_audio_context(capture);
          if (audio_ctx) {
            audio_flush_playback_buffers((audio_context_t *)audio_ctx);
          }
        }
      }
    }
    break;
  }

  // ===== VOLUME CONTROLS =====
  case KEY_DOWN: { // Decrease volume 10%
    double current_volume = GET_OPTION(speakers_volume);
    double new_volume = clamp_volume(current_volume - 0.1);
    options_set_double("speakers_volume", new_volume);
    double verify_volume = GET_OPTION(speakers_volume);
    log_info("Volume DOWN: %.0f%% → %.0f%% (verified: %.0f%%)", current_volume * 100.0, new_volume * 100.0,
             verify_volume * 100.0);
    break;
  }

  case KEY_UP: { // Increase volume 10%
    double current_volume = GET_OPTION(speakers_volume);
    double new_volume = clamp_volume(current_volume + 0.1);
    options_set_double("speakers_volume", new_volume);
    double verify_volume = GET_OPTION(speakers_volume);
    log_info("Volume UP: %.0f%% → %.0f%% (verified: %.0f%%)", current_volume * 100.0, new_volume * 100.0,
             verify_volume * 100.0);
    break;
  }

  // ===== PLAY/PAUSE CONTROL =====
  case KEY_SPACE: { // Toggle play/pause
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
  case KEY_C:
  case 'C': {
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
  case KEY_M:
  case 'M': {
    double current_volume = GET_OPTION(speakers_volume);
    log_debug("Mute toggle: current_volume=%.2f, g_mute_saved_volume=%.2f, threshold=0.01", current_volume,
              g_mute_saved_volume);

    if (current_volume > 0.01) { // If not already muted
      // Save current volume and mute
      g_mute_saved_volume = current_volume;
      options_set_double("speakers_volume", 0.0);
      double verify = GET_OPTION(speakers_volume);
      log_info("Muted: saved %.0f%%, set to 0%% (verified: %.2f)", g_mute_saved_volume * 100.0, verify);
    } else {
      // Restore previous volume
      double restore_volume = g_mute_saved_volume > 0.0 ? g_mute_saved_volume : 0.5;
      options_set_double("speakers_volume", restore_volume);
      double verify = GET_OPTION(speakers_volume);
      log_info("Unmuted: restored %.0f%% (verified: %.2f)", restore_volume * 100.0, verify);
    }
    break;
  }

  // ===== RENDER MODE CONTROL =====
  case KEY_R:
  case 'R': {
    int current_mode = (int)GET_OPTION(render_mode);
    int next_mode = next_render_mode(current_mode);
    options_set_int("render_mode", next_mode);

    const char *mode_names[] = {"Foreground", "Background", "Half-block"};
    if (next_mode >= 0 && next_mode <= 2) {
      log_info("Render mode: %s", mode_names[next_mode]);
    }
    break;
  }

  // ===== COLOR FILTER CONTROL =====
  case KEY_F:
  case 'F': {
    int current_filter = (int)GET_OPTION(color_filter);
    int next_filter = next_color_filter(current_filter);
    options_set_int("color_filter", next_filter);

    const char *filter_names[] = {"None", "Black", "White", "Green", "Magenta", "Fuchsia", "Orange",
                                  "Teal", "Cyan",  "Pink",  "Red",   "Yellow",  "Rainbow"};
    if (next_filter >= 0 && next_filter < (int)COLOR_FILTER_COUNT) {
      log_info("Color filter: %s", filter_names[next_filter]);
    }
    break;
  }

  // ===== HORIZONTAL FLIP CONTROL =====
  case 'G':
  case 'g': {
    bool current_flip_x = (bool)GET_OPTION(flip_x);
    options_set_bool("flip_x", !current_flip_x);
    log_info("Horizontal flip: %s", !current_flip_x ? "enabled" : "disabled");
    break;
  }

  // ===== MATRIX RAIN EFFECT CONTROL =====
  case KEY_0: {
    bool current_matrix = (bool)GET_OPTION(matrix_rain);
    options_set_bool("matrix_rain", !current_matrix);
    log_info("Matrix rain effect: %s", !current_matrix ? "enabled" : "disabled");
    break;
  }

  // ===== FPS COUNTER TOGGLE =====
  case '-': {
    bool current = (bool)GET_OPTION(fps_counter);
    options_set_bool("fps_counter", !current);
    log_info("FPS counter: %s", !current ? "enabled" : "disabled");
    break;
  }

  // ===== LOCK DEBUG (debug builds only) =====
#ifndef NDEBUG
  case KEY_BACKTICK: {
    debug_sync_trigger_print();
    log_debug("Lock state dump triggered via backtick key");
    break;
  }
#endif

  default:
    // Unknown key - silently ignore
    break;
  }
}
