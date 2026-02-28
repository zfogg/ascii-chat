/**
 * @file session/settings.c
 * @brief ⚙️ Session settings serialization implementation
 * @ingroup session
 *
 * Implements session settings serialization, deserialization, and
 * synchronization with the global options system.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "session/settings.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/network.h>

#include <string.h>
#include <time.h>

/* ============================================================================
 * Session Settings Functions
 * ============================================================================ */

void session_settings_init(session_settings_t *settings) {
  if (!settings) {
    return;
  }

  memset(settings, 0, sizeof(session_settings_t));

  // Read all defaults from the RCU options system
  settings->version = 0;
  settings->width = (int16_t)terminal_get_effective_width();
  settings->height = (int16_t)terminal_get_effective_height();
  settings->color_mode = (uint8_t)GET_OPTION(color_mode);
  settings->render_mode = (uint8_t)GET_OPTION(render_mode);
  settings->palette_type = (uint8_t)GET_OPTION(palette_type);

  // Custom palette
  const char *palette = GET_OPTION(palette_custom);
  if (palette && palette[0] != '\0') {
    SAFE_STRNCPY(settings->palette_custom, palette, sizeof(settings->palette_custom));
  }

  settings->audio_enabled = (uint8_t)GET_OPTION(audio_enabled);
  settings->encryption_required = GET_OPTION(no_encrypt) ? 0 : 1;
}

asciichat_error_t session_settings_serialize(const session_settings_t *settings, uint8_t *buffer, size_t *len) {
  if (!settings || !buffer || !len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_settings_serialize: NULL parameter");
  }

  // Fixed-size serialization format
  size_t offset = 0;

  // Version (4 bytes, network byte order)
  uint32_t version_net = htonl(settings->version);
  memcpy(buffer + offset, &version_net, sizeof(version_net));
  offset += sizeof(version_net);

  // Width (2 bytes, network byte order)
  uint16_t width_net = htons((uint16_t)(settings->width & 0xFFFF));
  memcpy(buffer + offset, &width_net, sizeof(width_net));
  offset += sizeof(width_net);

  // Height (2 bytes, network byte order)
  uint16_t height_net = htons((uint16_t)(settings->height & 0xFFFF));
  memcpy(buffer + offset, &height_net, sizeof(height_net));
  offset += sizeof(height_net);

  // Color mode (1 byte)
  buffer[offset++] = settings->color_mode;

  // Render mode (1 byte)
  buffer[offset++] = settings->render_mode;

  // Palette type (1 byte)
  buffer[offset++] = settings->palette_type;

  // Custom palette (32 bytes, null-padded)
  memcpy(buffer + offset, settings->palette_custom, 32);
  offset += 32;

  // Audio enabled (1 byte)
  buffer[offset++] = settings->audio_enabled;

  // Encryption required (1 byte)
  buffer[offset++] = settings->encryption_required;

  // Reserved (16 bytes)
  memcpy(buffer + offset, settings->reserved, 16);
  offset += 16;

  *len = offset;
  return ASCIICHAT_OK;
}

asciichat_error_t session_settings_deserialize(const uint8_t *buffer, size_t len, session_settings_t *settings) {
  if (!buffer || !settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_settings_deserialize: NULL parameter");
  }

  // Check minimum buffer size
  if (len < SESSION_SETTINGS_SERIALIZED_SIZE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_settings_deserialize: buffer too small (%zu < %d)", len,
                     SESSION_SETTINGS_SERIALIZED_SIZE);
  }

  memset(settings, 0, sizeof(session_settings_t));
  size_t offset = 0;

  // Version (4 bytes, network byte order)
  uint32_t version_net;
  memcpy(&version_net, buffer + offset, sizeof(version_net));
  settings->version = ntohl(version_net);
  offset += sizeof(version_net);

  // Width (2 bytes, network byte order)
  uint16_t width_net;
  memcpy(&width_net, buffer + offset, sizeof(width_net));
  settings->width = (int16_t)ntohs(width_net);
  offset += sizeof(width_net);

  // Height (2 bytes, network byte order)
  uint16_t height_net;
  memcpy(&height_net, buffer + offset, sizeof(height_net));
  settings->height = (int16_t)ntohs(height_net);
  offset += sizeof(height_net);

  // Color mode (1 byte)
  settings->color_mode = buffer[offset++];

  // Render mode (1 byte)
  settings->render_mode = buffer[offset++];

  // Palette type (1 byte)
  settings->palette_type = buffer[offset++];

  // Custom palette (32 bytes)
  memcpy(settings->palette_custom, buffer + offset, 32);
  settings->palette_custom[31] = '\0'; // Ensure null termination
  offset += 32;

  // Audio enabled (1 byte)
  settings->audio_enabled = buffer[offset++];

  // Encryption required (1 byte)
  settings->encryption_required = buffer[offset++];

  // Reserved (16 bytes)
  memcpy(settings->reserved, buffer + offset, 16);
  // offset += 16; // Unused, commented to avoid warning

  return ASCIICHAT_OK;
}

asciichat_error_t session_settings_from_options(session_settings_t *settings) {
  if (!settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_settings_from_options: NULL settings");
  }

  // Initialize to defaults
  session_settings_init(settings);

  // Get current options
  const options_t *opts = options_get();
  if (!opts) {
    return SET_ERRNO(ERROR_CONFIG, "Options not initialized");
  }

  // Set version to current time for ordering
  settings->version = (uint32_t)time(NULL);

  // Copy dimension settings
  settings->width = (int16_t)opts->width;
  settings->height = (int16_t)opts->height;

  // Copy display settings
  settings->color_mode = (uint8_t)opts->color_mode;
  settings->render_mode = (uint8_t)opts->render_mode;
  settings->palette_type = (uint8_t)opts->palette_type;

  // Copy custom palette if set
  if (opts->palette_custom_set && opts->palette_custom[0] != '\0') {
    SAFE_STRNCPY(settings->palette_custom, opts->palette_custom, sizeof(settings->palette_custom));
  }

  // Copy audio/encryption settings
  settings->audio_enabled = (uint8_t)opts->audio_enabled;
  settings->encryption_required = opts->no_encrypt ? 0 : 1;

  return ASCIICHAT_OK;
}

asciichat_error_t session_settings_apply_to_options(const session_settings_t *settings) {
  if (!settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_settings_apply_to_options: NULL settings");
  }

  // Update dimensions if specified
  if (settings->width > 0 && settings->height > 0) {
    asciichat_error_t width_result = options_set_int("width", (int)settings->width);
    asciichat_error_t height_result = options_set_int("height", (int)settings->height);
    if (width_result != ASCIICHAT_OK || height_result != ASCIICHAT_OK) {
      log_warn("Failed to apply dimension settings: width=%d, height=%d", width_result, height_result);
    }
  }

  // Note: Other options would require additional RCU update functions
  // For now, dimensions are the primary use case for runtime updates
  // Color mode, render mode, and palette typically don't change mid-session

  return ASCIICHAT_OK;
}

bool session_settings_needs_update(uint32_t local_version, uint32_t remote_version) {
  // Higher version wins (newer settings)
  return remote_version > local_version;
}

bool session_settings_equal(const session_settings_t *a, const session_settings_t *b) {
  if (!a || !b) {
    return false;
  }

  // Compare all fields except version
  return a->width == b->width && a->height == b->height && a->color_mode == b->color_mode &&
         a->render_mode == b->render_mode && a->palette_type == b->palette_type &&
         strncmp(a->palette_custom, b->palette_custom, sizeof(a->palette_custom)) == 0 &&
         a->audio_enabled == b->audio_enabled && a->encryption_required == b->encryption_required;
}
