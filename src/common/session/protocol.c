#include "session/protocol.h"
#include "session/frame_validation.h"
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/audio.h>
#include <string.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/media/codecs.h>

static void receive_frame(const ascii_frame_packet_t *header, const void *data, size_t length, void *context) {
  session_protocol_t *protocol = context;
  acip_render_settings_t contract;
  uint32_t effects = 0;
  bool has_contract = (header->flags & FRAME_FLAG_RENDER_CONTRACT) != 0;
  if (header->flags & ~(FRAME_FLAG_RENDER_CONTRACT | FRAME_FLAG_IS_COMPRESSED | 1u)) {
    protocol->callback_result = SET_ERRNO(ERROR_INVALID_FRAME, "Unknown participant frame flags");
    return;
  }
  if (has_contract) {
    size_t prefix;
    protocol->callback_result = frame_render_contract_decode(data, length, &contract, &effects, &prefix);
    if (protocol->callback_result != ASCIICHAT_OK)
      return;
    data = (const uint8_t *)data + prefix;
    length -= prefix;
    if (contract.revision && contract.revision == protocol->settings_revision &&
        protocol->effective_render_settings.palette[0] &&
        strcmp(contract.palette, protocol->effective_render_settings.palette) != 0 &&
        protocol->effective_render_settings.caps.utf8_support) {
      protocol->callback_result = SET_ERRNO(ERROR_INVALID_FRAME, "Frame palette contradicts acknowledged revision");
      return;
    }
  }
  bool compressed = (header->flags & FRAME_FLAG_IS_COMPRESSED) != 0;
  size_t expected = compressed ? header->compressed_size : header->original_size;
  if (!header->original_size || expected > FRAME_RENDER_MAX_BYTES || length != expected ||
      (compressed && !header->compressed_size) || (!compressed && header->compressed_size)) {
    protocol->callback_result = SET_ERRNO(ERROR_INVALID_FRAME, "Invalid participant ASCII frame length");
    return;
  }
  // Bound decoded work before allocation/decompression, matching the UI frame budget.
  if (header->original_size > FRAME_RENDER_MAX_BYTES) {
    protocol->callback_result = SET_ERRNO(ERROR_INVALID_FRAME, "Participant ASCII frame exceeds byte budget");
    return;
  }
  asciichat_errno_scope_t decode_scope = errno_checkpoint();
  char *text;
  if (protocol->media.frame_workspace) {
    protocol->callback_result = packet_decode_frame_data_workspace(
        protocol->media.frame_workspace, data, length, compressed, header->original_size, header->compressed_size);
    if (protocol->callback_result != ASCIICHAT_OK)
      return;
    text = protocol->media.frame_workspace->data;
  } else {
    text = packet_decode_frame_data_malloc(data, length, compressed, header->original_size, header->compressed_size);
  }
  if (!text) {
    asciichat_error_context_t cause;
    protocol->callback_result = errno_peek_since(decode_scope, &cause)
                                    ? cause.code
                                    : SET_ERRNO(ERROR_INVALID_FRAME, "Could not decode participant ASCII frame");
    return;
  }
  if (asciichat_crc32(text, header->original_size) != header->checksum) {
    protocol->callback_result = SET_ERRNO(ERROR_INVALID_FRAME, "Participant ASCII frame checksum mismatch");
  } else if ((protocol->callback_result =
                  session_frame_validate_contract(text, header->original_size, header->width, header->height,
                                                  has_contract ? &contract : NULL, effects)) == ASCIICHAT_OK) {
    // A valid old frame may arrive after a resize/settings update. Skip it
    // without disconnecting the transport or disturbing audio and recovery.
    if (protocol->media.terminal_output) {
      // Redirected snapshots have no physical viewport; the UI fallback is
      // not a bound on their explicitly configured or default output size.
      terminal_size_t viewport =
          (ui_controller_is_presenting() || terminal_is_stdout_tty()) ? ui_controller_size() : (terminal_size_t){0};
      uint32_t width = terminal_get_effective_width(), height = terminal_get_effective_height();
      if ((viewport.cols > 0 && header->width > (uint32_t)viewport.cols) ||
          (viewport.rows > 0 && header->height > (uint32_t)viewport.rows) || header->width > width ||
          header->height > height) {
        protocol->frames_skipped_viewport++;
        goto done;
      }
    }
    protocol->frames_received++;
    if (protocol->media.on_frame)
      protocol->callback_result = protocol->media.on_frame(header, text, protocol->media.context);
  }
done:
  if (!protocol->media.frame_workspace)
    SAFE_FREE(text);
}

static void receive_samples(const audio_batch_packet_t *header, const float *samples, size_t count, void *context) {
  session_protocol_t *protocol = context;
  if (header && (header->sample_rate != AUDIO_SAMPLE_RATE || header->channels != 1)) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported participant audio format");
    return;
  }
  if (protocol->media.on_samples)
    protocol->callback_result = protocol->media.on_samples(samples, count, protocol->media.context);
}

static void receive_opus(const void *data, size_t length, void *context) {
  session_protocol_t *protocol = context;
  if (!protocol->media.decode_opus || !protocol->media.on_samples)
    return;
  float samples[5760]; // 120 ms at 48 kHz.
  size_t decoded = 0;
  protocol->callback_result =
      protocol->media.decode_opus(data, length, samples, 5760, &decoded, protocol->media.context);
  if (protocol->callback_result != ASCIICHAT_OK)
    return;
  if (decoded > 5760) {
    protocol->callback_result = SET_ERRNO(ERROR_AUDIO, "Participant decoder exceeded sample capacity");
    return;
  }
  if (decoded)
    receive_samples(NULL, samples, decoded, context);
}

static void receive_opus_batch(const void *data, size_t length, void *context) {
  session_protocol_t *protocol = context;
  const uint8_t *opus;
  const uint16_t *sizes;
  size_t opus_length;
  int rate, duration, count;
  protocol->callback_result =
      packet_parse_opus_batch(data, length, &opus, &opus_length, &sizes, &rate, &duration, &count);
  if (protocol->callback_result != ASCIICHAT_OK)
    return;
  if (rate != AUDIO_SAMPLE_RATE || duration < 1 || duration > 120 || count < 1 || count > 256) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid participant Opus batch format");
    return;
  }
  // Validate the whole batch before delivering any samples, including unaligned input.
  size_t total = 0;
  for (int i = 0; i < count; i++) {
    uint16_t size;
    memcpy(&size, (const uint8_t *)sizes + (size_t)i * sizeof(size), sizeof(size));
    size = NET_TO_HOST_U16(size);
    if (!size || size > opus_length - total) {
      protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated participant Opus batch");
      return;
    }
    total += size;
  }
  if (total != opus_length) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Trailing participant Opus batch data");
    return;
  }
  size_t offset = 0;
  for (int i = 0; i < count; i++) {
    uint16_t size;
    memcpy(&size, (const uint8_t *)sizes + (size_t)i * sizeof(size), sizeof(size));
    size = NET_TO_HOST_U16(size);
    receive_opus(opus + offset, size, context);
    if (protocol->callback_result != ASCIICHAT_OK)
      return;
    offset += size;
  }
}

static void receive_state(const server_state_packet_t *state, void *context) {
  session_protocol_t *protocol = context;
  uint32_t active = NET_TO_HOST_U32(state->active_client_count);
  if (!protocol->server_state_initialized || protocol->active_client_count != active)
    protocol->layout_changed = true;
  protocol->server_state_initialized = true;
  protocol->active_client_count = active;
  if (protocol->control.on_server_state)
    protocol->control.on_server_state(state, protocol->control.app_ctx);
}

static void receive_error(const error_packet_t *header, const char *message, void *context) {
  session_protocol_t *protocol = context;
  if (protocol->control.on_error)
    protocol->control.on_error(header, message, protocol->control.app_ctx);
  else
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK, "Participant peer error: %s", message);
}

void session_protocol_init(session_protocol_t *protocol, acip_transport_t *transport, session_protocol_media_t media,
                           acip_client_callbacks_t control) {
  *protocol = (session_protocol_t){.transport = transport, .media = media, .control = control};
  protocol->effective_render_settings.caps.utf8_support = GET_OPTION(force_utf8) != UTF8_SETTING_FALSE;
}

asciichat_error_t session_protocol_dispatch(session_protocol_t *protocol, packet_type_t type, const void *payload,
                                            size_t length) {
  if (!protocol || (!payload && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant protocol packet");
  protocol->callback_result = ASCIICHAT_OK;
  if (type == PACKET_TYPE_ACIP_SETTINGS_ACK) {
    acip_settings_header_t header;
    const char *json;
    size_t json_length;
    asciichat_error_t result = acip_settings_decode(payload, length, &header, &json, &json_length);
    if (result != ASCIICHAT_OK)
      return result;
    if ((header.request && header.request < protocol->settings_request) ||
        header.revision < protocol->settings_revision)
      return ASCIICHAT_OK;
    options_t *current = SAFE_MALLOC(sizeof(*current), options_t *),
              *candidate = SAFE_MALLOC(sizeof(*candidate), options_t *);
    acip_render_settings_options(&protocol->effective_render_settings, current);
    result = options_prepare_json(current, json, json_length, candidate, true);
    if (result == ASCIICHAT_OK)
      result = acip_render_settings_prepare(&protocol->effective_render_settings, candidate);
    SAFE_FREE(current);
    SAFE_FREE(candidate);
    if (result != ASCIICHAT_OK)
      return result;
    protocol->effective_render_settings.revision = header.revision;
    if (header.request)
      protocol->settings_request = header.request;
    protocol->settings_revision = header.revision;
    protocol->settings_status = header.status;
    if (header.status)
      log_warn("Remote rendering update rejected (request=%u, status=%u)", header.request, header.status);
    else
      log_info("Remote rendering update accepted (request=%u, revision=%u)", header.request, header.revision);
    return ASCIICHAT_OK;
  }
  acip_client_callbacks_t media = {.on_ascii_frame = receive_frame,
                                   .on_audio_batch = receive_samples,
                                   .on_audio_opus_batch = receive_opus_batch,
                                   .on_server_state = receive_state,
                                   .on_error = receive_error,
                                   .app_ctx = protocol};
  bool audio = type == PACKET_TYPE_AUDIO_BATCH || type == PACKET_TYPE_AUDIO_OPUS_BATCH;
  bool common =
      audio || type == PACKET_TYPE_ASCII_FRAME || type == PACKET_TYPE_SERVER_STATE || type == PACKET_TYPE_ERROR_MESSAGE;
  asciichat_error_t result;
  if (type == PACKET_TYPE_PING && !protocol->control.on_ping)
    result = acip_send_pong(protocol->transport);
  else
    result =
        acip_handle_client_packet(protocol->transport, type, payload, length, common ? &media : &protocol->control);
  if (result == ASCIICHAT_OK)
    result = protocol->callback_result;
  if (result == ASCIICHAT_OK) {
    protocol->packets_received++;
    if (audio) {
      protocol->audio_packets_received++;
      if (protocol->media.on_audio_packet)
        protocol->media.on_audio_packet(length, protocol->media.context);
    }
  }
  return result;
}

void session_protocol_build_capabilities(unsigned short width, unsigned short height,
                                         terminal_capabilities_packet_t *packet) {
  const options_t *opts = options_get();
  // Log the dimensions being sent to server (helps debug dimension mismatch issues)
  log_debug("Sending terminal size to server: %ux%u (auto_width=%d, auto_height=%d)", width, height, opts->auto_width,
            opts->auto_height);

  // Connection and transport availability is checked by threaded_send_packet()

  // Build terminal capabilities packet locally
  // Detect terminal capabilities automatically
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Set wants_padding based on snapshot mode and TTY status
  // Disable padding when:
  // - In snapshot mode (one frame and exit)
  // - When stdout is not a TTY (piped/redirected output)
  // Enable padding for interactive terminal sessions
  bool is_snapshot_mode = opts->snapshot_mode;
  bool is_interactive = terminal_is_interactive();
  caps.wants_padding = is_interactive && !is_snapshot_mode;

  log_debug("Client capabilities: wants_padding=%d (snapshot=%d, interactive=%d, stdin_tty=%d, stdout_tty=%d)",
            caps.wants_padding, is_snapshot_mode, is_interactive, terminal_is_stdin_tty(), terminal_is_stdout_tty());

  // Apply user's color mode override
  caps = apply_color_mode_override(caps);

  // Check if detection was reliable, use fallback only for auto-detection
  if (!caps.detection_reliable && opts->color_mode == COLOR_MODE_AUTO) {
    log_warn("Terminal capability detection not reliable, using fallback");
    SAFE_MEMSET(&caps, sizeof(caps), 0, sizeof(caps));
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities = 0;
    SAFE_STRNCPY(caps.term_type, "unknown", sizeof(caps.term_type));
    SAFE_STRNCPY(caps.colorterm, "", sizeof(caps.colorterm));
    caps.detection_reliable = 0;
    // Preserve wants_padding even in fallback mode
    caps.wants_padding = is_interactive && !is_snapshot_mode;
  }

  // Convert to network packet format with proper byte order
  terminal_capabilities_packet_t net_packet = {0};
  if (opts->matrix_rain)
    caps.capabilities |= TERM_CAP_MATRIX_RAIN;
  else
    caps.capabilities &= ~(uint32_t)TERM_CAP_MATRIX_RAIN;
  net_packet.capabilities = HOST_TO_NET_U32(caps.capabilities);
  net_packet.color_level = HOST_TO_NET_U32(caps.color_level);
  net_packet.color_count = HOST_TO_NET_U32(caps.color_count);
  net_packet.render_mode = HOST_TO_NET_U32(opts->render_mode);
  net_packet.color_filter = (uint8_t)opts->color_filter;
  net_packet.width = HOST_TO_NET_U16(width);
  net_packet.height = HOST_TO_NET_U16(height);
  net_packet.palette_type = HOST_TO_NET_U32(opts->palette_type);
  net_packet.utf8_support = HOST_TO_NET_U32(caps.utf8_support ? 1 : 0);

  if (opts->palette_type == PALETTE_CUSTOM && opts->palette_custom_set) {
    const char *palette_custom = opts && opts->palette_custom_set ? opts->palette_custom : "";
    SAFE_STRNCPY(net_packet.palette_custom, palette_custom, sizeof(net_packet.palette_custom));
    net_packet.palette_custom[sizeof(net_packet.palette_custom) - 1] = '\0';
  } else {
    SAFE_MEMSET(net_packet.palette_custom, sizeof(net_packet.palette_custom), 0, sizeof(net_packet.palette_custom));
  }

  // Set desired FPS
  int fps = opts->fps;
  if (fps > 0) {
    net_packet.desired_fps = (uint8_t)(fps > 144 ? 144 : fps);
  } else {
    net_packet.desired_fps = caps.desired_fps;
  }

  if (net_packet.desired_fps == 0) {
    net_packet.desired_fps = DEFAULT_MAX_FPS;
  }

  SAFE_STRNCPY(net_packet.term_type, caps.term_type, sizeof(net_packet.term_type));
  net_packet.term_type[sizeof(net_packet.term_type) - 1] = '\0';

  SAFE_STRNCPY(net_packet.colorterm, caps.colorterm, sizeof(net_packet.colorterm));
  net_packet.colorterm[sizeof(net_packet.colorterm) - 1] = '\0';

  net_packet.detection_reliable = caps.detection_reliable;
  // Send UTF-8 support flag: true for AUTO (default) and TRUE settings, false for FALSE setting
  net_packet.utf8_support = (opts->force_utf8 != UTF8_SETTING_FALSE) ? 1 : 0;

  // Set wants_padding flag (1=padding enabled, 0=no padding for snapshot/piped modes)
  net_packet.wants_padding = caps.wants_padding ? 1 : 0;

  // Set codec capabilities (client supports all codecs by default)
  net_packet.codec_capabilities_video = HOST_TO_NET_U32(VIDEO_CODEC_CAP_ALL);
  net_packet.codec_capabilities_audio = HOST_TO_NET_U32(AUDIO_CODEC_CAP_ALL);

  *packet = net_packet;
}

asciichat_error_t session_protocol_update_settings(terminal_size_t *advertised, uint64_t *advertised_revision,
                                                   session_protocol_send_settings_fn send, void *context) {
  if (!advertised || !advertised_revision || !send)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Settings update requires state and a send callback");
  if (advertised->cols > 0 && advertised->rows > 0 && *advertised_revision == options_revision())
    return ASCIICHAT_OK;
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  asciichat_error_t result = options_read_snapshot(snapshot);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(snapshot);
    return result;
  }
  terminal_size_t size = {.cols = snapshot->values.width, .rows = snapshot->values.height};
  if (size.cols <= 0 || size.rows <= 0) {
    SAFE_FREE(snapshot);
    return ASCIICHAT_OK;
  }
  if (snapshot->values.color_mode == TERM_COLOR_AUTO) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    snapshot->values.color_mode = caps.detection_reliable ? caps.color_level : TERM_COLOR_NONE;
  }
  result = send(&snapshot->values, (uint32_t)snapshot->revision, context);
  if (result == ASCIICHAT_OK) {
    *advertised = size;
    *advertised_revision = snapshot->revision;
  }
  SAFE_FREE(snapshot);
  return result;
}
