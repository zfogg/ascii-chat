#pragma once

#include <ascii-chat/options/runtime.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>

#define ACIP_SETTINGS_SCHEMA 1
#define ACIP_SETTINGS_HEADER_SIZE 20
#define ACIP_SETTINGS_MAX_SIZE 4116

/** Owned by a single participant. Its owner serializes reads and writes. */
typedef struct {
  terminal_capabilities_t caps;
  uint16_t width, height;
  uint32_t revision;
  uint16_t fps_limit;
  char palette[256];
  char luminance[256];
  size_t palette_length;
} acip_render_settings_t;

/** Host-order header; wire encoding uses five network-order uint32 values. */
typedef struct {
  uint32_t schema, request, expected_revision, revision, status;
} acip_settings_header_t;

asciichat_error_t acip_render_settings_capabilities(acip_render_settings_t *state,
                                                    const terminal_capabilities_packet_t *packet);
asciichat_error_t acip_render_settings_prepare(acip_render_settings_t *candidate, const options_t *values);
void acip_render_settings_options(const acip_render_settings_t *state, options_t *values);
asciichat_error_t acip_settings_decode(const void *payload, size_t length, acip_settings_header_t *header,
                                       const char **json, size_t *json_length);
asciichat_error_t acip_settings_encode(const acip_settings_header_t *header, const char *json, uint8_t *buffer,
                                       size_t capacity, size_t *length);
/** A rejected request returns current effective values without changing state. */
asciichat_error_t acip_settings_request(acip_render_settings_t *state, const void *payload, size_t length,
                                        uint8_t *reply, size_t capacity, size_t *reply_length);
asciichat_error_t acip_settings_send(acip_transport_t *transport, const options_t *values, uint32_t request,
                                     uint32_t expected_revision);
