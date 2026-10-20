#include <ascii-chat/debug/stats.h>
#include <ascii-chat/debug/errno.h>
/**
 * @file session/keyboard_handler.c
 * @brief Keyboard input handler implementation
 * @ingroup session
 */

#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/session.h>
#include "session/keyboard_handler.h"
#include "session/capture.h"
#include "session/display.h"
#include <ascii-chat/ui/keyboard_help.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/platform/terminal.h>
#include <string.h>
#include <signal.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/notice.h>
#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/media/input.h>
#include <ascii-chat/media/playlist.h>
#include <ascii-chat/log/search.h>

#ifndef NDEBUG
#include <ascii-chat/debug/sync.h>
#endif

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/** Muted volume state (used to restore volume when unmuting) - default 100% */
static double g_mute_saved_volume = 1.0;

/* ============================================================================
 * Keyboard Handler
 * ============================================================================ */

static void handle_keyboard_input(session_capture_ctx_t *capture, session_display_ctx_t *display, keyboard_key_t key) {
  if (ui_controller_current_screen() == UI_SCREEN_LOGS && ui_session_logs_key(key))
    return;
  ui_session_action_t action = ui_session_key(key, &g_mute_saved_volume);
  if (action == UI_ACTION_SETTINGS || action == UI_ACTION_VOLUME) {
    session_capture_redraw_media(capture);
    if (key == KEY_UP || key == KEY_DOWN || key == 'm' || key == 'M')
      ui_controller_show_volume();
    if (display && keyboard_help_is_active(display))
      keyboard_help_render(display);
    return;
  }

  // Debug: log all key codes to help identify unknown keys
  if (key != KEY_NONE) {
    log_info("KEYBOARD INPUT: code=%d (0x%02x) char='%c'", key, key, (key >= 32 && key < 127) ? key : '?');
  }

  switch (action) {
  // ===== HELP SCREEN TOGGLE =====
  case UI_ACTION_LOGS: {
    ui_controller_toggle_logs();
    break;
  }
  case UI_ACTION_HELP: {
    log_info("✓✓✓ USER PRESSED ? KEY - HELP SCREEN TOGGLE ✓✓✓");
    if (display) {
      log_info("Toggling help screen (display=%p)", (void *)display);
      keyboard_help_toggle(display);
      log_info("✓ Help screen toggle complete");
    } else {
      log_error("Cannot toggle help - display context is NULL");
    }
    break;
  }

  // ===== HELP SCREEN CLOSE / QUIT =====
  case UI_ACTION_CLOSE: {
    if (ui_controller_current_screen() == UI_SCREEN_LOGS) {
      ui_controller_hide_logs();
      break;
    }
    if (display && keyboard_help_is_active(display)) {
      // Close help screen if it's active
      keyboard_help_toggle(display);
      ui_controller_remove(UI_SCREEN_HELP);
    } else {
      // If help screen is not active, quit the app (like Ctrl-C)
      // The signal handler will gracefully shutdown all modes (client, server, mirror, etc.)
      raise(SIGINT);
    }
    break;
  }

  // ===== SEEK CONTROLS (file sources only) =====
  case UI_ACTION_SEEK_BACK: { // Seek backward 30 seconds
    if (capture) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(capture);
      if (session_capture_media_active(capture) && source && media_source_get_type(source) == MEDIA_SOURCE_FILE) {
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

  case UI_ACTION_SEEK_FORWARD: { // Seek forward 30 seconds
    if (capture) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(capture);
      if (session_capture_media_active(capture) && source && media_source_get_type(source) == MEDIA_SOURCE_FILE) {
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

  // ===== PLAY/PAUSE CONTROL =====
  case UI_ACTION_TOGGLE_MEDIA:
    session_capture_toggle_media(capture);
    if (capture && session_capture_get_audio_context(capture))
      audio_flush_playback_buffers((audio_context_t *)session_capture_get_audio_context(capture));
    break;
  case UI_ACTION_PAUSE: { // Toggle play/pause
    if (capture) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(capture);
      if (session_capture_media_active(capture) && source && media_source_get_type(source) == MEDIA_SOURCE_FILE) {
        media_source_toggle_pause(source);
        if (media_source_is_paused(source)) {
          log_info("Paused");
        } else {
          log_info("Playing");
        }
        void *audio_ctx = session_capture_get_audio_context(capture);
        if (audio_ctx) {
          audio_flush_playback_buffers((audio_context_t *)audio_ctx);
        }
      }
    }
    break;
  }

  // ===== LOCK DEBUG (debug builds only) =====
#ifndef NDEBUG
  case UI_ACTION_DEBUG: {
    debug_sync_trigger_print();
    debug_errno_trigger_print();
    debug_stats_trigger_print();
    log_debug("Lock state dump triggered via backtick key");
    break;
  }
#endif

  default:
    // Unknown key - log it for debugging
    if (key != KEY_NONE) {
      log_info("UNKNOWN KEY: code=%d (0x%02x) char='%c'", key, key, (key >= 32 && key < 127) ? key : '?');
    }
    break;
  }

  // Re-render help screen if it's active to show updated settings
  if (display && keyboard_help_is_active(display)) {
    keyboard_help_render(display);
  }
}

void session_handle_keyboard_input(session_capture_ctx_t *capture, session_display_ctx_t *display, keyboard_key_t key) {
  bool text_entry = log_search_is_entering();
  if (text_entry && key == KEY_PASTE) {
    char pasted[8192];
    if (ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK)
      for (const unsigned char *p = (const unsigned char *)pasted; *p; ++p)
        ui_session_logs_key((keyboard_key_t)(*p >= 32 && *p != 127 ? *p : ' '));
    CLEAR_ERRNO();
    return;
  }
  if (!text_entry && (key == '<' || key == '>')) {
    session_capture_skip_media(capture, key == '>' ? 1 : -1);
    return;
  }
  if (!text_entry && key == KEY_ESCAPE && session_capture_cancel_open(capture))
    return;
  if (!text_entry && (key == KEY_MEDIA_SUBMIT || key == KEY_PASTE || key == 'o' || key == 'O' || key == 15 ||
                      key == 'v' || key == 'V')) {
    if ((key == 'o' || key == 'O' || key == 15) && ui_controller_interactive_screen() == UI_SCREEN_PROMPT) {
      ui_prompt_remove();
      return;
    }
    char input[8192] = {0}, normalized[8192];
    asciichat_error_t result = ASCIICHAT_OK;
    if (key == KEY_MEDIA_SUBMIT)
      result = ui_input_media_take(input, sizeof(input));
    else if (key == KEY_PASTE)
      result = ui_input_take_paste(input, sizeof(input));
    else if (key == 'v' || key == 'V')
      result = platform_clipboard_read(input, sizeof(input));
    if (result == ASCIICHAT_OK) {
      if (key != KEY_MEDIA_SUBMIT && input[0]) {
        media_playlist_t list;
        result = media_playlist_parse(input, &list);
        if (result == ASCIICHAT_OK)
          result = media_playlist_format(&list, normalized, sizeof(normalized));
        if (result == ASCIICHAT_OK)
          memcpy(input, normalized, strlen(normalized) + 1);
      }
      if (result == ASCIICHAT_OK && key != KEY_MEDIA_SUBMIT)
        result = ui_input_media_begin(input);
      if (result == ASCIICHAT_OK && key == KEY_MEDIA_SUBMIT)
        result = session_capture_open_playlist(capture, input);
    }
    if (key == KEY_MEDIA_SUBMIT)
      ui_prompt_remove();
    if (result != ASCIICHAT_OK) {
      asciichat_error_context_t error;
      if (HAS_ERRNO(&error))
        NOTICE(WARNING, "MEDIA INPUT", "%s", error.context_message);
      CLEAR_ERRNO();
    }
    return;
  }
  session_capture_lock_source(capture);
  handle_keyboard_input(capture, display, key);
  session_capture_unlock_source(capture);
}
