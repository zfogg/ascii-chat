#include <ascii-chat/options/runtime.h>
#include <ascii-chat/common.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/rgba/color_filter.h>
/** Color filter names - must match color_filter_t enum exactly */
static const char *COLOR_FILTER_NAMES[COLOR_FILTER_COUNT] = {
    "None",    // COLOR_FILTER_NONE = 0
    "Black",   // COLOR_FILTER_BLACK = 1
    "White",   // COLOR_FILTER_WHITE = 2
    "Green",   // COLOR_FILTER_GREEN = 3
    "Magenta", // COLOR_FILTER_MAGENTA = 4
    "Fuchsia", // COLOR_FILTER_FUCHSIA = 5
    "Orange",  // COLOR_FILTER_ORANGE = 6
    "Teal",    // COLOR_FILTER_TEAL = 7
    "Cyan",    // COLOR_FILTER_CYAN = 8
    "Pink",    // COLOR_FILTER_PINK = 9
    "Red",     // COLOR_FILTER_RED = 10
    "Yellow",  // COLOR_FILTER_YELLOW = 11
    "Rainbow", // COLOR_FILTER_RAINBOW = 12
};

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

static asciichat_error_t toggle_mute(double *saved_volume) {
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  asciichat_error_t result;
  for (;;) {
    result = options_read_snapshot(snapshot);
    if (result != ASCIICHAT_OK)
      break;
    double current = snapshot->values.speakers_volume;
    double restore = *saved_volume > 0.0 ? *saved_volume : 0.5;
    options_edit_t edit = {
        .name = "speakers_volume", .type = OPTIONS_VALUE_DOUBLE, .value.number = current > 0.01 ? 0.0 : restore};
    options_patch_t patch = {.edits = &edit, .count = 1, .expected_revision = snapshot->revision};
    result = options_apply_patch(&patch, NULL);
    if (result == ERROR_INVALID_STATE) {
      CLEAR_ERRNO();
      continue;
    }
    if (result == ASCIICHAT_OK) {
      if (current > 0.01)
        *saved_volume = current;
      log_info("%s: %.0f%%", current > 0.01 ? "Muted" : "Unmuted", edit.value.number * 100.0);
    }
    break;
  }
  SAFE_FREE(snapshot);
  return result;
}

ui_session_action_t ui_session_key(keyboard_key_t key, double *saved_volume) {
  switch ((int)key) {
  case KEY_QUESTION:
    return UI_ACTION_HELP;
  case '[':
    return UI_ACTION_TOGGLE_MEDIA;
  case ']':
    return UI_ACTION_LOGS;
  case KEY_ESCAPE:
    return UI_ACTION_CLOSE;
  case KEY_LEFT:
    return UI_ACTION_SEEK_BACK;
  case KEY_RIGHT:
    return UI_ACTION_SEEK_FORWARD;
  case KEY_SPACE:
    return UI_ACTION_PAUSE;
  case KEY_BACKTICK:
    return UI_ACTION_DEBUG;
  // ===== VOLUME CONTROLS =====
  case KEY_DOWN: { // Decrease volume 10%
    double current_volume = GET_OPTION(speakers_volume);
    double new_volume = clamp_volume(current_volume - 0.1);
    options_adjust("speakers_volume", key == KEY_DOWN ? -0.1 : 0.1);

    double verify_volume = GET_OPTION(speakers_volume);
    log_info("Volume DOWN: %.0f%% → %.0f%% (verified: %.0f%%)", current_volume * 100.0, new_volume * 100.0,
             verify_volume * 100.0);
    return UI_ACTION_VOLUME;
  }

  case KEY_UP: { // Increase volume 10%
    double current_volume = GET_OPTION(speakers_volume);
    double new_volume = clamp_volume(current_volume + 0.1);
    options_adjust("speakers_volume", key == KEY_DOWN ? -0.1 : 0.1);

    double verify_volume = GET_OPTION(speakers_volume);
    log_info("Volume UP: %.0f%% → %.0f%% (verified: %.0f%%)", current_volume * 100.0, new_volume * 100.0,
             verify_volume * 100.0);
    return UI_ACTION_SETTINGS;
  }

  // ===== COLOR MODE CONTROL =====
  case KEY_C:
  case 'C': {
    int current_mode = (int)GET_OPTION(color_mode);
    int next_mode = next_color_mode(current_mode);
    options_cycle("color_mode", 4);

    const char *mode_names[] = {"Mono", "16-color", "256-color", "Truecolor"};
    if (next_mode >= 0 && next_mode <= 3) {
      log_info("Color mode: %s", mode_names[next_mode]);
    }
    return UI_ACTION_SETTINGS;
  }

  // ===== MUTE CONTROL =====
  case KEY_M:
  case 'M': {
    if (toggle_mute(saved_volume) != ASCIICHAT_OK)
      log_warn("Could not toggle mute");

    return UI_ACTION_SETTINGS;
  }

  // ===== RENDER MODE CONTROL =====
  case KEY_R:
  case 'R': {
    int current_mode = (int)GET_OPTION(render_mode);
    int next_mode = next_render_mode(current_mode);
    options_cycle("render_mode", 3);

    const char *mode_names[] = {"Foreground", "Background", "Half-block"};
    if (next_mode >= 0 && next_mode <= 2) {
      log_info("Render mode: %s", mode_names[next_mode]);
    }
    return UI_ACTION_SETTINGS;
  }

  // ===== COLOR FILTER CONTROL =====
  case KEY_F:
  case 'F': {
    int current_filter = (int)GET_OPTION(color_filter);
    int next_filter = next_color_filter(current_filter);
    options_cycle("color_filter", 13);

    if (next_filter >= 0 && next_filter < (int)COLOR_FILTER_COUNT) {
      log_info("Color filter: %s", COLOR_FILTER_NAMES[next_filter]);
    }
    return UI_ACTION_SETTINGS;
  }

  // ===== HORIZONTAL FLIP CONTROL =====
  case 'X':
  case 'x': {
    log_info("✓ X KEY PRESSED - handling horizontal flip");
    bool current_flip_x = (bool)GET_OPTION(flip_x);
    options_toggle("flip_x");
    log_info("Horizontal flip: %s (was: %s)", !current_flip_x ? "enabled" : "disabled",
             current_flip_x ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  // ===== VERTICAL FLIP CONTROL =====
  case 'Y':
  case 'y': {
    bool current_flip_y = (bool)GET_OPTION(flip_y);
    options_toggle("flip_y");
    log_info("Vertical flip: %s", !current_flip_y ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  // ===== MATRIX RAIN EFFECT CONTROL =====
  case KEY_1: {
    bool current_matrix = (bool)GET_OPTION(matrix_rain);
    if (options_toggle("matrix_rain") != ASCIICHAT_OK)
      return UI_ACTION_SETTINGS;
    log_info("Matrix rain effect: %s", !current_matrix ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  case KEY_2: {
    if (!GET_OPTION(audio_enabled)) {
      log_info("Audio waveform requires audio to be enabled");
      return UI_ACTION_SETTINGS;
    }
    bool enabled = !GET_OPTION(waveform);
    if (options_toggle("waveform") != ASCIICHAT_OK)
      return UI_ACTION_SETTINGS;
    log_info("Audio waveform: %s", enabled ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  case KEY_3: {
    if (!GET_OPTION(audio_enabled)) {
      log_info("Audio frequency display requires audio to be enabled");
      return UI_ACTION_SETTINGS;
    }
    bool enabled = !GET_OPTION(fft);
    if (options_toggle("fft") != ASCIICHAT_OK)
      return UI_ACTION_SETTINGS;
    log_info("Audio frequency display: %s", enabled ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  case 'w':
  case 'W': {
    if (options_toggle("face_follow") == ASCIICHAT_OK)
      log_info("Face follow: %s", GET_OPTION(face_follow) ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  case 'z':
  case 'Z': {
    if (options_set_int("zalgo", GET_OPTION(zalgo) ? 0 : 8) == ASCIICHAT_OK)
      log_info("Zalgo intensity: %d", GET_OPTION(zalgo));
    return UI_ACTION_SETTINGS;
  }

  // ===== FPS COUNTER TOGGLE =====
  case KEY_MINUS: {
    bool current = (bool)GET_OPTION(fps_counter);
    options_toggle("fps_counter");
    log_info("FPS counter: %s", !current ? "enabled" : "disabled");
    return UI_ACTION_SETTINGS;
  }

  default:
    return UI_ACTION_NONE;
  }
}
