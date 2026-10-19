#include "session/settings.h"
#include <ascii-chat/options/runtime.h>
#include <ascii-chat/common.h>
#include <string.h>

void session_settings_from_values(const options_t *values, session_settings_t *settings) {
  memset(settings, 0, sizeof(*settings));
  settings->width = (int16_t)values->width;
  settings->height = (int16_t)values->height;
  settings->color_mode = (int8_t)values->color_mode;
  settings->render_mode = (uint8_t)values->render_mode;
  settings->palette_type = (uint8_t)values->palette_type;
  settings->color_filter = (uint8_t)values->color_filter;
  settings->fps = (uint8_t)values->fps;
  settings->matrix_rain = values->matrix_rain;
  SAFE_STRNCPY(settings->palette_custom, values->palette_custom, sizeof(settings->palette_custom));
}
void session_settings_to_options(const session_settings_t *settings, options_t *values) {
  *values = options_t_new();
  values->width = settings->width;
  values->height = settings->height;
  values->color_mode = settings->color_mode;
  values->render_mode = settings->render_mode;
  values->palette_type = settings->palette_type;
  values->color_filter = settings->color_filter;
  values->fps = settings->fps;
  values->matrix_rain = settings->matrix_rain;
  SAFE_STRNCPY(values->palette_custom, settings->palette_custom, sizeof(values->palette_custom));
}
void session_settings_init(session_settings_t *settings) {
  if (settings)
    session_settings_from_values(options_get(), settings);
}
asciichat_error_t session_settings_from_options(session_settings_t *settings) {
  if (!settings)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing settings");
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  asciichat_error_t result = options_read_snapshot(snapshot);
  if (result == ASCIICHAT_OK) {
    session_settings_from_values(&snapshot->values, settings);
    settings->version = (uint32_t)snapshot->revision;
  }
  SAFE_FREE(snapshot);
  return result;
}
