/**
 * @file options.c
 * @brief WASM shared option setters and getters
 * @ingroup web_common
 *
 * Provides unified option access for all WASM modes.
 * These functions are compiled into both mirror.wasm and client.wasm.
 */

#include "options.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <stdlib.h>

// ============================================================================
// Dimension Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_width(int value) {
  if (value <= 0 || value > 1000)
    return -1;
  asciichat_error_t err = options_set_int("width", value);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_width(void) {
  return GET_OPTION(width);
}

EMSCRIPTEN_KEEPALIVE
int set_height(int value) {
  if (value <= 0 || value > 1000)
    return -1;
  asciichat_error_t err = options_set_int("height", value);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_height(void) {
  return GET_OPTION(height);
}

// ============================================================================
// Color Mode Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_color_mode(int mode) {
  asciichat_error_t err = options_set_int("color_mode", mode);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_color_mode(void) {
  return GET_OPTION(color_mode);
}

// ============================================================================
// Color Filter Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_color_filter(int filter) {
  asciichat_error_t err = options_set_int("color_filter", filter);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_color_filter(void) {
  return GET_OPTION(color_filter);
}

// ============================================================================
// Palette Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_palette(const char *palette_name) {
  if (!palette_name)
    return -1;

  palette_type_t palette_value;
  char *error_msg = NULL;

  if (!parse_palette_type(palette_name, &palette_value, &error_msg)) {
    if (error_msg) {
      log_error("Failed to parse palette '%s': %s", palette_name, error_msg);
      free(error_msg);
    }
    return -1;
  }

  asciichat_error_t err = options_set_int("palette_type", (int)palette_value);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_palette(void) {
  return GET_OPTION(palette_type);
}

EMSCRIPTEN_KEEPALIVE
int set_palette_chars(const char *chars) {
  if (!chars)
    return -1;
  asciichat_error_t err = options_set_string("palette_custom", chars);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
const char *get_palette_chars(void) {
  return GET_OPTION(palette_custom);
}

// ============================================================================
// Matrix Rain Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_matrix_rain(int enabled) {
  asciichat_error_t err = options_set_bool("matrix_rain", enabled != 0);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_matrix_rain(void) {
  return GET_OPTION(matrix_rain) ? 1 : 0;
}

// ============================================================================
// Horizontal Flip Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_flip_x(int enabled) {
  asciichat_error_t err = options_set_bool("flip_x", enabled != 0);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_flip_x(void) {
  return GET_OPTION(flip_x) ? 1 : 0;
}

// ============================================================================
// Render Mode Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_render_mode(int mode) {
  asciichat_error_t err = options_set_int("render_mode", mode);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_render_mode(void) {
  return GET_OPTION(render_mode);
}

// ============================================================================
// Target FPS Accessors
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int set_target_fps(int fps) {
  if (fps < 15 || fps > 60)
    return -1;
  asciichat_error_t err = options_set_int("fps", fps);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int get_target_fps(void) {
  return GET_OPTION(fps);
}

// ============================================================================
// Help Text API
// ============================================================================

/**
 * Get help text for a CLI option in a specific mode
 * Exported to WASM for JavaScript access via FFI
 *
 * @param mode The mode (asciichat_mode_t as int)
 * @param option_name The long name of the option
 * @return Help text string or NULL if not applicable
 */
EMSCRIPTEN_KEEPALIVE
const char *get_help_text(int mode, const char *option_name) {
  if (!option_name || !option_name[0])
    return NULL;
  asciichat_mode_t mode_enum = (asciichat_mode_t)mode;
  return options_get_help_text(mode_enum, option_name);
}
