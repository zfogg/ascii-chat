/**
 * @file network/acip/handlers.c
 * @brief ACIP protocol packet handlers implementation
 *
 * Implements packet dispatching for both client and server sides.
 * Uses O(1) array-based dispatch instead of O(n) switch statements.
 * Parses packet payloads and dispatches to registered callbacks.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "networking/acip/handlers.h"
#include "networking/acip/messages.h"
#include "networking/acip/acds.h"
#include "network/packet.h"
#include "network/packet_parsing.h"
#include "audio/audio.h"
#include "util/endian.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include "common.h"
#include <string.h>

// =============================================================================
// Handler Function Pointer Types
// =============================================================================

/**
 * @brief Client-side packet handler function pointer type
 *
 * All client packet handlers follow this signature for O(1) array dispatch.
 *
 * @param payload Packet payload data
 * @param payload_len Payload length in bytes
 * @param callbacks Application callbacks structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
typedef asciichat_error_t (*acip_client_handler_func_t)(const void *payload, size_t payload_len,
                                                        const acip_client_callbacks_t *callbacks);

/**
 * @brief Server-side packet handler function pointer type
 *
 * All server packet handlers follow this signature for O(1) array dispatch.
 *
 * @param payload Packet payload data
 * @param payload_len Payload length in bytes
 * @param client_ctx Per-client context (e.g., client_info_t*)
 * @param callbacks Application callbacks structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
typedef asciichat_error_t (*acip_server_handler_func_t)(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks);

// =============================================================================
// Client-Side Packet Handlers
// =============================================================================

// Forward declarations for client handlers
static asciichat_error_t handle_client_ascii_frame(const void *payload, size_t payload_len,
                                                   const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_audio_batch(const void *payload, size_t payload_len,
                                                   const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_audio_opus(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_audio_opus_batch(const void *payload, size_t payload_len,
                                                        const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_server_state(const void *payload, size_t payload_len,
                                                    const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_error_message(const void *payload, size_t payload_len,
                                                     const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_remote_log(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_ping(const void *payload, size_t payload_len,
                                            const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_pong(const void *payload, size_t payload_len,
                                            const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_audio(const void *payload, size_t payload_len,
                                             const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_clear_console(const void *payload, size_t payload_len,
                                                     const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_crypto_rekey_request(const void *payload, size_t payload_len,
                                                            const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_crypto_rekey_response(const void *payload, size_t payload_len,
                                                             const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_webrtc_sdp(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_webrtc_ice(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks);

/**
 * @brief Client packet handler dispatch table (O(1) lookup)
 *
 * Array indexed by packet_type_t for constant-time handler dispatch.
 * NULL entries indicate unhandled packet types.
 */
static const acip_client_handler_func_t g_client_packet_handlers[200] = {
    [PACKET_TYPE_ASCII_FRAME] = handle_client_ascii_frame,
    [PACKET_TYPE_AUDIO_BATCH] = handle_client_audio_batch,
    [PACKET_TYPE_AUDIO_OPUS] = handle_client_audio_opus,
    [PACKET_TYPE_AUDIO_OPUS_BATCH] = handle_client_audio_opus_batch,
    [PACKET_TYPE_SERVER_STATE] = handle_client_server_state,
    [PACKET_TYPE_ERROR_MESSAGE] = handle_client_error_message,
    [PACKET_TYPE_REMOTE_LOG] = handle_client_remote_log,
    [PACKET_TYPE_PING] = handle_client_ping,
    [PACKET_TYPE_PONG] = handle_client_pong,
    [PACKET_TYPE_AUDIO] = handle_client_audio,
    [PACKET_TYPE_CLEAR_CONSOLE] = handle_client_clear_console,
    [PACKET_TYPE_CRYPTO_REKEY_REQUEST] = handle_client_crypto_rekey_request,
    [PACKET_TYPE_CRYPTO_REKEY_RESPONSE] = handle_client_crypto_rekey_response,
    [PACKET_TYPE_ACIP_WEBRTC_SDP] = handle_client_webrtc_sdp,
    [PACKET_TYPE_ACIP_WEBRTC_ICE] = handle_client_webrtc_ice,
};

asciichat_error_t acip_handle_client_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, const acip_client_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }

  (void)transport; // May be used in future for sending responses

  // O(1) array-based dispatch - bounds check packet type
  if (type >= 200) {
    log_warn("Invalid client packet type: %d (out of range)", type);
    return ASCIICHAT_OK;
  }

  // Lookup handler in dispatch table
  acip_client_handler_func_t handler = g_client_packet_handlers[type];
  if (!handler) {
    log_warn("Unhandled client packet type: %d", type);
    return ASCIICHAT_OK;
  }

  // Dispatch to handler
  return handler(payload, payload_len, callbacks);
}

// =============================================================================
// Client Handler Implementations (extracted from switch cases)
// =============================================================================

static asciichat_error_t handle_client_ascii_frame(const void *payload, size_t payload_len,
                                                   const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_ascii_frame) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(ascii_frame_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ASCII_FRAME payload too small");
  }

  // Extract header
  ascii_frame_packet_t header;
  memcpy(&header, payload, sizeof(header));

  // Convert from network byte order
  header.width = NET_TO_HOST_U32(header.width);
  header.height = NET_TO_HOST_U32(header.height);
  header.original_size = NET_TO_HOST_U32(header.original_size);
  header.compressed_size = NET_TO_HOST_U32(header.compressed_size);
  header.checksum = NET_TO_HOST_U32(header.checksum);
  header.flags = NET_TO_HOST_U32(header.flags);

  // Get frame data (after header)
  const void *frame_data = (const uint8_t *)payload + sizeof(ascii_frame_packet_t);
  size_t frame_data_len = payload_len - sizeof(ascii_frame_packet_t);

  callbacks->on_ascii_frame(&header, frame_data, frame_data_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_audio_batch(const void *payload, size_t payload_len,
                                                   const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_audio_batch) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(audio_batch_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "AUDIO_BATCH payload too small");
  }

  // Parse batch header
  const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)payload;
  uint32_t batch_count = NET_TO_HOST_U32(batch_header->batch_count);
  uint32_t total_samples = NET_TO_HOST_U32(batch_header->total_samples);
  uint32_t sample_rate = NET_TO_HOST_U32(batch_header->sample_rate);
  uint32_t channels = NET_TO_HOST_U32(batch_header->channels);

  (void)batch_count;
  (void)sample_rate;
  (void)channels;

  // Validate size
  size_t expected_size = sizeof(audio_batch_packet_t) + (total_samples * sizeof(uint32_t));
  if (payload_len != expected_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "AUDIO_BATCH size mismatch");
  }

  // Extract quantized samples
  const uint8_t *samples_ptr = (const uint8_t *)payload + sizeof(audio_batch_packet_t);

  // Dequantize samples to float
  float *samples = SAFE_MALLOC(total_samples * sizeof(float), float *);
  if (!samples) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio batch buffer");
  }

  asciichat_error_t result = audio_dequantize_samples(samples_ptr, total_samples, samples);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(samples);
    return result;
  }

  // Copy header for callback
  audio_batch_packet_t header_copy = *batch_header;
  header_copy.batch_count = batch_count;
  header_copy.total_samples = total_samples;
  header_copy.sample_rate = sample_rate;
  header_copy.channels = channels;

  callbacks->on_audio_batch(&header_copy, samples, total_samples, callbacks->app_ctx);

  SAFE_FREE(samples);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_audio_opus(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_audio_opus) {
    return ASCIICHAT_OK;
  }

  // Raw Opus data (no header parsing)
  callbacks->on_audio_opus(payload, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_audio_opus_batch(const void *payload, size_t payload_len,
                                                        const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_audio_opus_batch) {
    return ASCIICHAT_OK;
  }

  // Opus batch data (header + sizes + opus frames)
  callbacks->on_audio_opus_batch(payload, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_server_state(const void *payload, size_t payload_len,
                                                    const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_server_state) {
    return ASCIICHAT_OK;
  }

  if (payload_len != sizeof(server_state_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "SERVER_STATE size mismatch");
  }

  callbacks->on_server_state((const server_state_packet_t *)payload, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_error_message(const void *payload, size_t payload_len,
                                                     const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_error) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(error_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ERROR_MESSAGE payload too small");
  }

  const error_packet_t *header = (const error_packet_t *)payload;
  const char *message = (const char *)payload + sizeof(error_packet_t);
  size_t msg_len = payload_len - sizeof(error_packet_t);

  // Ensure null-terminated message
  char msg_buffer[MAX_ERROR_MESSAGE_LENGTH + 1];
  size_t copy_len = msg_len < MAX_ERROR_MESSAGE_LENGTH ? msg_len : MAX_ERROR_MESSAGE_LENGTH;
  memcpy(msg_buffer, message, copy_len);
  msg_buffer[copy_len] = '\0';

  callbacks->on_error(header, msg_buffer, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_remote_log(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_remote_log) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(remote_log_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "REMOTE_LOG payload too small");
  }

  const remote_log_packet_t *header = (const remote_log_packet_t *)payload;
  const char *message = (const char *)payload + sizeof(remote_log_packet_t);
  size_t msg_len = payload_len - sizeof(remote_log_packet_t);

  // Ensure null-terminated message
  char msg_buffer[512];
  size_t copy_len = msg_len < sizeof(msg_buffer) - 1 ? msg_len : sizeof(msg_buffer) - 1;
  memcpy(msg_buffer, message, copy_len);
  msg_buffer[copy_len] = '\0';

  callbacks->on_remote_log(header, msg_buffer, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_ping(const void *payload, size_t payload_len,
                                            const acip_client_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (callbacks->on_ping) {
    callbacks->on_ping(callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_pong(const void *payload, size_t payload_len,
                                            const acip_client_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (callbacks->on_pong) {
    callbacks->on_pong(callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_audio(const void *payload, size_t payload_len,
                                             const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_audio) {
    return ASCIICHAT_OK;
  }

  // Raw audio data (float samples)
  callbacks->on_audio(payload, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_clear_console(const void *payload, size_t payload_len,
                                                     const acip_client_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (callbacks->on_clear_console) {
    callbacks->on_clear_console(callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_crypto_rekey_request(const void *payload, size_t payload_len,
                                                            const acip_client_callbacks_t *callbacks) {
  if (callbacks->on_crypto_rekey_request) {
    callbacks->on_crypto_rekey_request(payload, payload_len, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_crypto_rekey_response(const void *payload, size_t payload_len,
                                                             const acip_client_callbacks_t *callbacks) {
  if (callbacks->on_crypto_rekey_response) {
    callbacks->on_crypto_rekey_response(payload, payload_len, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_webrtc_sdp(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_webrtc_sdp) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_webrtc_sdp_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "WEBRTC_SDP payload too small");
  }

  const acip_webrtc_sdp_t *sdp = (const acip_webrtc_sdp_t *)payload;
  callbacks->on_webrtc_sdp(sdp, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_webrtc_ice(const void *payload, size_t payload_len,
                                                  const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_webrtc_ice) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_webrtc_ice_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "WEBRTC_ICE payload too small");
  }

  const acip_webrtc_ice_t *ice = (const acip_webrtc_ice_t *)payload;
  callbacks->on_webrtc_ice(ice, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

// =============================================================================
// Server-Side Packet Handlers
// =============================================================================

// Server handler function signature (includes client_ctx)
typedef asciichat_error_t (*acip_server_handler_func_t)(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks);

// Forward declarations for server handlers
static asciichat_error_t handle_server_image_frame(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_audio_batch(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_audio_opus(const void *payload, size_t payload_len, void *client_ctx,
                                                  const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_audio_opus_batch(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_client_join(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_client_leave(const void *payload, size_t payload_len, void *client_ctx,
                                                    const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_stream_start(const void *payload, size_t payload_len, void *client_ctx,
                                                    const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_stream_stop(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_capabilities(const void *payload, size_t payload_len, void *client_ctx,
                                                    const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_ping(const void *payload, size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_remote_log(const void *payload, size_t payload_len, void *client_ctx,
                                                  const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_protocol_version(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_audio(const void *payload, size_t payload_len, void *client_ctx,
                                             const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_pong(const void *payload, size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_error_message(const void *payload, size_t payload_len, void *client_ctx,
                                                     const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_crypto_rekey_request(const void *payload, size_t payload_len, void *client_ctx,
                                                            const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_crypto_rekey_response(const void *payload, size_t payload_len, void *client_ctx,
                                                             const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_crypto_rekey_complete(const void *payload, size_t payload_len, void *client_ctx,
                                                             const acip_server_callbacks_t *callbacks);

// Server packet dispatch table (O(1) lookup by packet type)
static const acip_server_handler_func_t g_server_packet_handlers[200] = {
    [PACKET_TYPE_IMAGE_FRAME] = handle_server_image_frame,
    [PACKET_TYPE_AUDIO_BATCH] = handle_server_audio_batch,
    [PACKET_TYPE_AUDIO_OPUS] = handle_server_audio_opus,
    [PACKET_TYPE_AUDIO_OPUS_BATCH] = handle_server_audio_opus_batch,
    [PACKET_TYPE_CLIENT_JOIN] = handle_server_client_join,
    [PACKET_TYPE_CLIENT_LEAVE] = handle_server_client_leave,
    [PACKET_TYPE_STREAM_START] = handle_server_stream_start,
    [PACKET_TYPE_STREAM_STOP] = handle_server_stream_stop,
    [PACKET_TYPE_CLIENT_CAPABILITIES] = handle_server_capabilities,
    [PACKET_TYPE_PING] = handle_server_ping,
    [PACKET_TYPE_REMOTE_LOG] = handle_server_remote_log,
    [PACKET_TYPE_PROTOCOL_VERSION] = handle_server_protocol_version,
    [PACKET_TYPE_AUDIO] = handle_server_audio,
    [PACKET_TYPE_PONG] = handle_server_pong,
    [PACKET_TYPE_ERROR_MESSAGE] = handle_server_error_message,
    [PACKET_TYPE_CRYPTO_REKEY_REQUEST] = handle_server_crypto_rekey_request,
    [PACKET_TYPE_CRYPTO_REKEY_RESPONSE] = handle_server_crypto_rekey_response,
    [PACKET_TYPE_CRYPTO_REKEY_COMPLETE] = handle_server_crypto_rekey_complete,
};

asciichat_error_t acip_handle_server_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }

  (void)transport; // May be used in future for sending responses

  // O(1) array-based dispatch - bounds check packet type
  if (type >= 200) {
    log_warn("Invalid server packet type: %d (out of range)", type);
    return ASCIICHAT_OK;
  }

  // Lookup handler in dispatch table
  acip_server_handler_func_t handler = g_server_packet_handlers[type];
  if (!handler) {
    log_warn("Unhandled server packet type: %d", type);
    return ASCIICHAT_OK;
  }

  // Dispatch to handler
  return handler(payload, payload_len, client_ctx, callbacks);
}

// =============================================================================
// Server Handler Implementations (extracted from switch cases)
// =============================================================================

static asciichat_error_t handle_server_image_frame(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_image_frame) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(image_frame_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "IMAGE_FRAME payload too small");
  }

  // Extract header
  image_frame_packet_t header;
  memcpy(&header, payload, sizeof(header));

  // Convert from network byte order
  header.width = NET_TO_HOST_U32(header.width);
  header.height = NET_TO_HOST_U32(header.height);
  header.pixel_format = NET_TO_HOST_U32(header.pixel_format);
  header.compressed_size = NET_TO_HOST_U32(header.compressed_size);
  header.checksum = NET_TO_HOST_U32(header.checksum);
  header.timestamp = NET_TO_HOST_U32(header.timestamp);

  // Get pixel data (after header)
  const void *pixel_data = (const uint8_t *)payload + sizeof(image_frame_packet_t);
  size_t pixel_data_len = payload_len - sizeof(image_frame_packet_t);

  callbacks->on_image_frame(&header, pixel_data, pixel_data_len, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_audio_batch(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_audio_batch) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(audio_batch_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "AUDIO_BATCH payload too small");
  }

  // Parse batch header
  const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)payload;
  uint32_t batch_count = NET_TO_HOST_U32(batch_header->batch_count);
  uint32_t total_samples = NET_TO_HOST_U32(batch_header->total_samples);
  uint32_t sample_rate = NET_TO_HOST_U32(batch_header->sample_rate);
  uint32_t channels = NET_TO_HOST_U32(batch_header->channels);

  (void)batch_count;
  (void)sample_rate;
  (void)channels;

  // Validate size
  size_t expected_size = sizeof(audio_batch_packet_t) + (total_samples * sizeof(uint32_t));
  if (payload_len != expected_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "AUDIO_BATCH size mismatch");
  }

  // Extract quantized samples
  const uint8_t *samples_ptr = (const uint8_t *)payload + sizeof(audio_batch_packet_t);

  // Dequantize samples to float
  float *samples = SAFE_MALLOC(total_samples * sizeof(float), float *);
  if (!samples) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio batch buffer");
  }

  asciichat_error_t result = audio_dequantize_samples(samples_ptr, total_samples, samples);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(samples);
    return result;
  }

  // Copy header for callback
  audio_batch_packet_t header_copy = *batch_header;
  header_copy.batch_count = batch_count;
  header_copy.total_samples = total_samples;
  header_copy.sample_rate = sample_rate;
  header_copy.channels = channels;

  callbacks->on_audio_batch(&header_copy, samples, total_samples, client_ctx, callbacks->app_ctx);

  SAFE_FREE(samples);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_audio_opus(const void *payload, size_t payload_len, void *client_ctx,
                                                  const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_audio_opus) {
    return ASCIICHAT_OK;
  }

  callbacks->on_audio_opus(payload, payload_len, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_audio_opus_batch(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_audio_opus_batch) {
    return ASCIICHAT_OK;
  }

  callbacks->on_audio_opus_batch(payload, payload_len, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_client_join(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_client_join) {
    return ASCIICHAT_OK;
  }

  callbacks->on_client_join(payload, payload_len, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_client_leave(const void *payload, size_t payload_len, void *client_ctx,
                                                    const acip_server_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (!callbacks->on_client_leave) {
    return ASCIICHAT_OK;
  }

  callbacks->on_client_leave(client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_stream_start(const void *payload, size_t payload_len, void *client_ctx,
                                                    const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_stream_start) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(uint8_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "STREAM_START payload too small");
  }

  uint8_t stream_types = *(const uint8_t *)payload;
  callbacks->on_stream_start(stream_types, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_stream_stop(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_stream_stop) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(uint8_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "STREAM_STOP payload too small");
  }

  uint8_t stream_types = *(const uint8_t *)payload;
  callbacks->on_stream_stop(stream_types, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_capabilities(const void *payload, size_t payload_len, void *client_ctx,
                                                    const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_capabilities) {
    return ASCIICHAT_OK;
  }

  callbacks->on_capabilities(payload, payload_len, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_ping(const void *payload, size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (callbacks->on_ping) {
    callbacks->on_ping(client_ctx, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_remote_log(const void *payload, size_t payload_len, void *client_ctx,
                                                  const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_remote_log) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(remote_log_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "REMOTE_LOG payload too small");
  }

  const remote_log_packet_t *header = (const remote_log_packet_t *)payload;
  const char *message = (const char *)payload + sizeof(remote_log_packet_t);
  size_t msg_len = payload_len - sizeof(remote_log_packet_t);

  // Ensure null-terminated message
  char msg_buffer[512];
  size_t copy_len = msg_len < sizeof(msg_buffer) - 1 ? msg_len : sizeof(msg_buffer) - 1;
  memcpy(msg_buffer, message, copy_len);
  msg_buffer[copy_len] = '\0';

  callbacks->on_remote_log(header, msg_buffer, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_protocol_version(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_protocol_version) {
    return ASCIICHAT_OK;
  }

  if (payload_len != sizeof(protocol_version_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "PROTOCOL_VERSION size mismatch");
  }

  callbacks->on_protocol_version((const protocol_version_packet_t *)payload, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_audio(const void *payload, size_t payload_len, void *client_ctx,
                                             const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_audio) {
    return ASCIICHAT_OK;
  }

  // Raw audio data (float samples)
  callbacks->on_audio(payload, payload_len, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_pong(const void *payload, size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (callbacks->on_pong) {
    callbacks->on_pong(client_ctx, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_error_message(const void *payload, size_t payload_len, void *client_ctx,
                                                     const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_error) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(error_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ERROR_MESSAGE payload too small");
  }

  const error_packet_t *header = (const error_packet_t *)payload;
  const char *message = (const char *)payload + sizeof(error_packet_t);
  size_t msg_len = payload_len - sizeof(error_packet_t);

  // Ensure null-terminated message
  char msg_buffer[MAX_ERROR_MESSAGE_LENGTH + 1];
  size_t copy_len = msg_len < MAX_ERROR_MESSAGE_LENGTH ? msg_len : MAX_ERROR_MESSAGE_LENGTH;
  memcpy(msg_buffer, message, copy_len);
  msg_buffer[copy_len] = '\0';

  callbacks->on_error(header, msg_buffer, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_crypto_rekey_request(const void *payload, size_t payload_len, void *client_ctx,
                                                            const acip_server_callbacks_t *callbacks) {
  if (callbacks->on_crypto_rekey_request) {
    callbacks->on_crypto_rekey_request(payload, payload_len, client_ctx, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_crypto_rekey_response(const void *payload, size_t payload_len, void *client_ctx,
                                                             const acip_server_callbacks_t *callbacks) {
  if (callbacks->on_crypto_rekey_response) {
    callbacks->on_crypto_rekey_response(payload, payload_len, client_ctx, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_crypto_rekey_complete(const void *payload, size_t payload_len, void *client_ctx,
                                                             const acip_server_callbacks_t *callbacks) {
  if (callbacks->on_crypto_rekey_complete) {
    callbacks->on_crypto_rekey_complete(payload, payload_len, client_ctx, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}
