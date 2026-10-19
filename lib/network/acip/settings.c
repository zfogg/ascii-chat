#include <ascii-chat/network/acip/settings.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <string.h>

void acip_render_settings_options(const acip_render_settings_t *state, options_t *values) {
  *values = options_t_new();
  values->width = state->width;
  values->height = state->height;
  values->fps = state->caps.desired_fps ? state->caps.desired_fps : 60;
  values->color_mode = (terminal_color_mode_t)state->caps.color_level;
  values->color_filter = state->caps.color_filter;
  values->render_mode = state->caps.render_mode;
  values->palette_type = state->caps.palette_type ? state->caps.palette_type : PALETTE_STANDARD;
  SAFE_STRNCPY(values->palette_custom, state->caps.palette_custom, sizeof(values->palette_custom));
  values->palette_custom_set = values->palette_custom[0] != 0;
  values->matrix_rain = (state->caps.capabilities & TERM_CAP_MATRIX_RAIN) != 0;
}

asciichat_error_t acip_render_settings_prepare(acip_render_settings_t *candidate, const options_t *values) {
  if (!candidate || !values || values->width < 1 || values->width > 4096 || values->height < 1 ||
      values->height > 4096 || (uint64_t)values->width * values->height > 1048576 || values->fps < 1 ||
      values->fps > 144 || values->color_mode < TERM_COLOR_NONE || values->color_mode > TERM_COLOR_TRUECOLOR ||
      values->color_filter < 0 || values->color_filter >= COLOR_FILTER_COUNT || values->render_mode < 0 ||
      values->render_mode > RENDER_MODE_HALF_BLOCK || values->palette_type < PALETTE_STANDARD ||
      values->palette_type >= PALETTE_COUNT || strlen(values->palette_custom) >= sizeof(candidate->caps.palette_custom))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid remote render settings");
  acip_render_settings_t next = *candidate;
  next.width = (uint16_t)values->width;
  next.height = (uint16_t)values->height;
  next.caps.desired_fps =
      candidate->fps_limit && values->fps > candidate->fps_limit ? candidate->fps_limit : values->fps;
  next.caps.color_level = values->color_mode;
  next.caps.color_count = values->color_mode == TERM_COLOR_TRUECOLOR ? 16777216
                          : values->color_mode == TERM_COLOR_256     ? 256
                          : values->color_mode == TERM_COLOR_16      ? 16
                                                                     : 2;
  next.caps.color_filter = values->color_filter;
  next.caps.render_mode = values->render_mode;
  next.caps.wants_background = values->render_mode == RENDER_MODE_BACKGROUND;
  next.caps.palette_type = values->palette_type;
  SAFE_STRNCPY(next.caps.palette_custom, values->palette_custom, sizeof(next.caps.palette_custom));
  if (values->matrix_rain)
    next.caps.capabilities |= TERM_CAP_MATRIX_RAIN;
  else
    next.caps.capabilities &= ~(uint32_t)TERM_CAP_MATRIX_RAIN;
  if (initialize_client_palette(next.caps.palette_type, values->palette_custom, next.palette, &next.palette_length,
                                next.luminance))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rendering palette");
  next.revision = candidate->revision + 1;
  *candidate = next;
  return ASCIICHAT_OK;
}

static asciichat_error_t prepare_for_viewer(acip_render_settings_t *state, options_t *values) {
  if (values->palette_type < PALETTE_STANDARD || values->palette_type >= PALETTE_COUNT)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rendering palette");
  values->palette_type = select_compatible_palette(values->palette_type, state->caps.utf8_support);
  if (values->palette_type == PALETTE_CUSTOM && !state->caps.utf8_support &&
      palette_requires_utf8_encoding(values->palette_custom, strlen(values->palette_custom)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Custom palette requires UTF-8 support");
  return acip_render_settings_prepare(state, values);
}

asciichat_error_t acip_render_settings_capabilities(acip_render_settings_t *state,
                                                    const terminal_capabilities_packet_t *packet) {
  if (!state || !packet || !memchr(packet->palette_custom, 0, sizeof(packet->palette_custom)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid render capabilities");
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  acip_render_settings_options(state, values);
  values->width = NET_TO_HOST_U16(packet->width);
  values->height = NET_TO_HOST_U16(packet->height);
  values->fps = packet->desired_fps ? packet->desired_fps : 60;
  values->color_mode = NET_TO_HOST_U32(packet->color_level);
  values->color_filter = packet->color_filter;
  values->render_mode = NET_TO_HOST_U32(packet->render_mode);
  values->palette_type = NET_TO_HOST_U32(packet->palette_type);
  if (values->palette_type == PALETTE_UNSET)
    values->palette_type = PALETTE_STANDARD;
  SAFE_STRNCPY(values->palette_custom, packet->palette_custom, sizeof(values->palette_custom));
  values->matrix_rain = (NET_TO_HOST_U32(packet->capabilities) & TERM_CAP_MATRIX_RAIN) != 0;
  acip_render_settings_t next = *state;
  next.caps.capabilities = NET_TO_HOST_U32(packet->capabilities);
  next.caps.utf8_support = NET_TO_HOST_U32(packet->utf8_support) != 0;
  next.caps.wants_padding = packet->wants_padding != 0;
  asciichat_error_t result = prepare_for_viewer(&next, values);
  if (result == ASCIICHAT_OK)
    *state = next;
  SAFE_FREE(values);
  return result;
}

asciichat_error_t acip_settings_decode(const void *payload, size_t length, acip_settings_header_t *header,
                                       const char **json, size_t *json_length) {
  if (!payload || !header || !json || !json_length || length < ACIP_SETTINGS_HEADER_SIZE ||
      length > ACIP_SETTINGS_MAX_SIZE)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid settings packet length");
  uint32_t fields[5];
  memcpy(fields, payload, sizeof(fields));
  *header = (acip_settings_header_t){NET_TO_HOST_U32(fields[0]), NET_TO_HOST_U32(fields[1]), NET_TO_HOST_U32(fields[2]),
                                     NET_TO_HOST_U32(fields[3]), NET_TO_HOST_U32(fields[4])};
  if (header->schema != ACIP_SETTINGS_SCHEMA)
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "Unsupported settings schema");
  *json = (const char *)payload + ACIP_SETTINGS_HEADER_SIZE;
  *json_length = length - ACIP_SETTINGS_HEADER_SIZE;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_settings_encode(const acip_settings_header_t *header, const char *json, uint8_t *buffer,
                                       size_t capacity, size_t *length) {
  if (!header || !json || !buffer || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing settings encoding argument");
  size_t size = strlen(json);
  if (size > 4096 || capacity < ACIP_SETTINGS_HEADER_SIZE + size)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Settings output too small");
  uint32_t fields[5] = {HOST_TO_NET_U32(header->schema), HOST_TO_NET_U32(header->request),
                        HOST_TO_NET_U32(header->expected_revision), HOST_TO_NET_U32(header->revision),
                        HOST_TO_NET_U32(header->status)};
  memcpy(buffer, fields, sizeof(fields));
  memcpy(buffer + sizeof(fields), json, size);
  *length = sizeof(fields) + size;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_settings_request(acip_render_settings_t *state, const void *payload, size_t length,
                                        uint8_t *reply, size_t capacity, size_t *reply_length) {
  acip_settings_header_t header = {0};
  const char *json = NULL;
  size_t json_length = 0;
  asciichat_error_t result = acip_settings_decode(payload, length, &header, &json, &json_length);
  if (result != ASCIICHAT_OK)
    return result;
  asciichat_errno_scope_t request_scope = asciichat_errno_scope_begin();
  options_t *current = SAFE_MALLOC(sizeof(*current), options_t *),
            *candidate = SAFE_MALLOC(sizeof(*candidate), options_t *);
  acip_render_settings_options(state, current);
  if (header.status)
    result = SET_ERRNO(ERROR_INVALID_PARAM, "Unexpected settings request status");
  else if (header.expected_revision != UINT32_MAX && header.expected_revision != state->revision)
    result = SET_ERRNO(ERROR_INVALID_STATE, "Remote settings changed; refresh and retry");
  else if (json_length) {
    result = options_prepare_json(current, json, json_length, candidate, true);
    if (result == ASCIICHAT_OK)
      result = prepare_for_viewer(state, candidate);
  }
  header.status = result;
  header.revision = state->revision;
  // The rejection is represented by the response status, preserving older errors.
  asciichat_errno_scope_end(request_scope, ASCIICHAT_ERRNO_HANDLED);
  acip_render_settings_options(state, current);
  char *effective = options_values_json(current, true);
  asciichat_error_t encoded =
      effective ? acip_settings_encode(&header, effective, reply, capacity, reply_length) : ERROR_MEMORY;
  SAFE_FREE(effective);
  SAFE_FREE(candidate);
  SAFE_FREE(current);
  return encoded;
}

asciichat_error_t acip_settings_send(acip_transport_t *transport, const options_t *values, uint32_t request,
                                     uint32_t expected_revision) {
  char *json = options_values_json(values, true);
  if (!json)
    return SET_ERRNO(ERROR_MEMORY, "Cannot encode render preferences");
  uint8_t buffer[ACIP_SETTINGS_MAX_SIZE];
  size_t length = 0;
  acip_settings_header_t header = {
      .schema = ACIP_SETTINGS_SCHEMA, .request = request, .expected_revision = expected_revision};
  asciichat_error_t result = acip_settings_encode(&header, json, buffer, sizeof(buffer), &length);
  if (result == ASCIICHAT_OK)
    result = packet_send_via_transport(transport, PACKET_TYPE_ACIP_SETTINGS_SYNC, buffer, length, 0);
  SAFE_FREE(json);
  return result;
}
