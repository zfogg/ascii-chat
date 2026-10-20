#pragma once

#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>

/** A participant's rendering preferences/effective values. Security is session policy. */
typedef struct {
  uint32_t version;
  int16_t width, height;
  int8_t color_mode;
  uint8_t render_mode, palette_type;
  char palette_custom[64];
  uint8_t color_filter, fps;
  bool matrix_rain;
} session_settings_t;

void session_settings_init(session_settings_t *settings);
asciichat_error_t session_settings_from_options(session_settings_t *settings);
void session_settings_from_values(const options_t *values, session_settings_t *settings);
void session_settings_to_options(const session_settings_t *settings, options_t *values);
