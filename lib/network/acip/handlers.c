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

#include "network/acip/handlers.h"
#include "network/acip/messages.h"
#include "network/acip/acds.h"
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

typedef asciichat_error_t (*acip_client_handler_func_t)(const void *payload, size_t payload_len,
                                                        const acip_client_callbacks_t *callbacks);

typedef asciichat_error_t (*acip_server_handler_func_t)(const void *payload, size_t payload_len, void *client_ctx,
                                                        const acip_server_callbacks_t *callbacks);

// =============================================================================
// Packet Type to Handler Index Mapping (O(1) hash table lookup)
// =============================================================================
// Open-addressing hash table with linear probing. key=0 means empty slot.
// Stores handler_idx+1 so that 0 can indicate "not found" after lookup.

#define HANDLER_HASH_SIZE 32 // ~50% load factor for 14-16 entries
#define CLIENT_HANDLER_COUNT 14
#define SERVER_HANDLER_COUNT 16

typedef struct {
  packet_type_t key;    // 0 = empty slot
  uint8_t handler_idx;  // handler index (0-based)
} handler_hash_entry_t;

// Hash function: simple modulo
#define HANDLER_HASH(type) ((type) % HANDLER_HASH_SIZE)

// Lookup function: linear probing, returns handler index or -1 if not found
static inline int handler_hash_lookup(const handler_hash_entry_t *table, packet_type_t type) {
  uint32_t h = HANDLER_HASH(type);
  for (int i = 0; i < HANDLER_HASH_SIZE; i++) {
    uint32_t slot = (h + i) % HANDLER_HASH_SIZE;
    if (table[slot].key == 0) return -1;  // empty slot = not found
    if (table[slot].key == type) return table[slot].handler_idx;
  }
  return -1;
}

// Client packet type -> handler index hash table
// Slots computed via linear probing: hash = type % 32
// clang-format off
static const handler_hash_entry_t g_client_handler_hash[HANDLER_HASH_SIZE] = {
    [0]  = {PACKET_TYPE_AUDIO_BATCH,           1},   // hash(4000)=0
    [1]  = {PACKET_TYPE_AUDIO_OPUS_BATCH,      2},   // hash(4001)=1
    [9]  = {PACKET_TYPE_PING,                  6},   // hash(5001)=9
    [10] = {PACKET_TYPE_PONG,                  7},   // hash(5002)=10
    [15] = {PACKET_TYPE_CLEAR_CONSOLE,         8},   // hash(5007)=15
    [16] = {PACKET_TYPE_SERVER_STATE,          3},   // hash(5008)=16
    [17] = {PACKET_TYPE_CRYPTO_REKEY_REQUEST,  9},   // hash(1201)=17
    [18] = {PACKET_TYPE_CRYPTO_REKEY_RESPONSE, 10},  // hash(1202)=18
    [19] = {PACKET_TYPE_ERROR_MESSAGE,         4},   // hash(2003)=19
    [20] = {PACKET_TYPE_REMOTE_LOG,            5},   // hash(2004)=20
    [21] = {PACKET_TYPE_ACIP_SESSION_JOINED,   13},  // hash(6005)=21
    [24] = {PACKET_TYPE_ASCII_FRAME,           0},   // hash(3000)=24
    [25] = {PACKET_TYPE_ACIP_WEBRTC_SDP,       11},  // hash(6009)=25
    [26] = {PACKET_TYPE_ACIP_WEBRTC_ICE,       12},  // hash(6010)=26
};
// clang-format on

// Server packet type -> handler index hash table
// clang-format off
static const handler_hash_entry_t g_server_handler_hash[HANDLER_HASH_SIZE] = {
    [0]  = {PACKET_TYPE_AUDIO_BATCH,           2},   // hash(4000)=0
    [1]  = {PACKET_TYPE_PROTOCOL_VERSION,      0},   // hash(1)=1
    [2]  = {PACKET_TYPE_AUDIO_OPUS_BATCH,      3},   // hash(4001)=1, probed->2
    [8]  = {PACKET_TYPE_CLIENT_CAPABILITIES,   4},   // hash(5000)=8
    [9]  = {PACKET_TYPE_PING,                  5},   // hash(5001)=9
    [10] = {PACKET_TYPE_PONG,                  6},   // hash(5002)=10
    [11] = {PACKET_TYPE_CLIENT_JOIN,           7},   // hash(5003)=11
    [12] = {PACKET_TYPE_CLIENT_LEAVE,          8},   // hash(5004)=12
    [13] = {PACKET_TYPE_STREAM_START,          9},   // hash(5005)=13
    [14] = {PACKET_TYPE_STREAM_STOP,           10},  // hash(5006)=14
    [17] = {PACKET_TYPE_CRYPTO_REKEY_REQUEST,  13},  // hash(1201)=17
    [18] = {PACKET_TYPE_CRYPTO_REKEY_RESPONSE, 14},  // hash(1202)=18
    [19] = {PACKET_TYPE_ERROR_MESSAGE,         12},  // hash(2003)=19
    [20] = {PACKET_TYPE_REMOTE_LOG,            11},  // hash(2004)=20
    [21] = {PACKET_TYPE_CRYPTO_REKEY_COMPLETE, 15},  // hash(1203)=19, probed->21
    [25] = {PACKET_TYPE_IMAGE_FRAME,           1},   // hash(3001)=25
};
// clang-format on

// =============================================================================
// Client-Side Packet Handlers
// =============================================================================

// Forward declarations for client handlers
static asciichat_error_t handle_client_ascii_frame(const void *payload, size_t payload_len,
                                                   const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_audio_batch(const void *payload, size_t payload_len,
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
static asciichat_error_t handle_client_session_joined(const void *payload, size_t payload_len,
                                                      const acip_client_callbacks_t *callbacks);

// Client handler dispatch table (indexed by client_handler_index())
static const acip_client_handler_func_t g_client_handlers[CLIENT_HANDLER_COUNT] = {
    handle_client_ascii_frame,          // 0
    handle_client_audio_batch,          // 1
    handle_client_audio_opus_batch,     // 2
    handle_client_server_state,         // 3
    handle_client_error_message,        // 4
    handle_client_remote_log,           // 5
    handle_client_ping,                 // 6
    handle_client_pong,                 // 7
    handle_client_clear_console,        // 8
    handle_client_crypto_rekey_request, // 9
    handle_client_crypto_rekey_response,// 10
    handle_client_webrtc_sdp,           // 11
    handle_client_webrtc_ice,           // 12
    handle_client_session_joined,       // 13
};

asciichat_error_t acip_handle_client_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, const acip_client_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }
  (void)transport;

  // O(1) dispatch via hash table lookup
  int idx = handler_hash_lookup(g_client_handler_hash, type);
  if (idx < 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unhandled client packet type: %d", type);
  }

  return g_client_handlers[idx](payload, payload_len, callbacks);
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

  // Validate frame dimensions to prevent DoS and buffer overflow attacks
  if (header.width == 0 || header.height == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid frame dimensions: %ux%u (width and height must be > 0)",
                     header.width, header.height);
  }

  // Sanity check: prevent unreasonably large frames (e.g., > 16K resolution)
  // This protects against resource exhaustion attacks
  const uint32_t MAX_WIDTH = 16384;  // 16K
  const uint32_t MAX_HEIGHT = 16384; // 16K
  if (header.width > MAX_WIDTH || header.height > MAX_HEIGHT) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Frame dimensions too large: %ux%u (max: %ux%u)", header.width, header.height,
                     MAX_WIDTH, MAX_HEIGHT);
  }

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

  // Validate sample rate and channels
  // Supported sample rates: 8000, 16000, 24000, 32000, 44100, 48000, 96000, 192000 Hz
  if (sample_rate == 0 || (sample_rate < 8000 || sample_rate > 192000) ||
      (sample_rate != 8000 && sample_rate != 16000 && sample_rate != 24000 && sample_rate != 32000 &&
       sample_rate != 44100 && sample_rate != 48000 && sample_rate != 96000 && sample_rate != 192000)) {
    return SET_ERRNO(
        ERROR_INVALID_PARAM,
        "Invalid audio sample rate: %u Hz (expected: 8000, 16000, 24000, 32000, 44100, 48000, 96000, or 192000)",
        sample_rate);
  }

  // Validate channel count (1-8 channels supported)
  if (channels == 0 || channels > 8) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio channel count: %u (expected: 1-8)", channels);
  }

  // Validate batch count
  (void)batch_count;

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

static asciichat_error_t handle_client_session_joined(const void *payload, size_t payload_len,
                                                      const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_session_joined) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_session_joined_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "SESSION_JOINED payload too small (got %zu, need %zu)", payload_len,
                     sizeof(acip_session_joined_t));
  }

  // Parse the session_joined response
  const acip_session_joined_t *joined = (const acip_session_joined_t *)payload;

  // Log the result
  if (joined->success) {
    log_debug("Session join succeeded: session_id=%.16s, participant_id=%.16s, server=%s:%u, type=%s",
              (const char *)joined->session_id, (const char *)joined->participant_id, joined->server_address,
              joined->server_port, joined->session_type == 1 ? "WebRTC" : "DirectTCP");
  } else {
    log_warn("Session join failed: error %d: %s", joined->error_code, joined->error_message);
  }

  // Dispatch to application callback
  callbacks->on_session_joined(joined, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

// =============================================================================
// Server-Side Packet Handlers
// =============================================================================

// Forward declarations for server handlers
static asciichat_error_t handle_server_image_frame(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_audio_batch(const void *payload, size_t payload_len, void *client_ctx,
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

// Server handler dispatch table (indexed by server_handler_index())
static const acip_server_handler_func_t g_server_handlers[SERVER_HANDLER_COUNT] = {
    handle_server_protocol_version,       // 0
    handle_server_image_frame,            // 1
    handle_server_audio_batch,            // 2
    handle_server_audio_opus_batch,       // 3
    handle_server_capabilities,           // 4
    handle_server_ping,                   // 5
    handle_server_pong,                   // 6
    handle_server_client_join,            // 7
    handle_server_client_leave,           // 8
    handle_server_stream_start,           // 9
    handle_server_stream_stop,            // 10
    handle_server_remote_log,             // 11
    handle_server_error_message,          // 12
    handle_server_crypto_rekey_request,   // 13
    handle_server_crypto_rekey_response,  // 14
    handle_server_crypto_rekey_complete,  // 15
};

asciichat_error_t acip_handle_server_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }
  (void)transport;

  // O(1) dispatch via hash table lookup
  int idx = handler_hash_lookup(g_server_handler_hash, type);
  if (idx < 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unhandled server packet type: %d", type);
  }

  return g_server_handlers[idx](payload, payload_len, client_ctx, callbacks);
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

  // Validate frame dimensions to prevent DoS and buffer overflow attacks
  if (header.width == 0 || header.height == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid image dimensions: %ux%u (width and height must be > 0)",
                     header.width, header.height);
  }

  // Sanity check: prevent unreasonably large frames (e.g., > 8K resolution for RGB)
  // This protects against resource exhaustion attacks
  const uint32_t MAX_WIDTH = 8192;  // 8K for RGB data
  const uint32_t MAX_HEIGHT = 8192; // 8K for RGB data
  if (header.width > MAX_WIDTH || header.height > MAX_HEIGHT) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions too large: %ux%u (max: %ux%u)", header.width, header.height,
                     MAX_WIDTH, MAX_HEIGHT);
  }

  // Validate pixel format
  // Valid formats: RGB24 (3), RGBA32 (4), YUV420 (1.5)
  if (header.pixel_format == 0 || header.pixel_format > 4) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pixel format: %u (expected: 1-4)", header.pixel_format);
  }

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

  if (payload_len < sizeof(uint32_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "STREAM_START payload too small (got %zu, expected %zu)", payload_len,
                     sizeof(uint32_t));
  }

  // Read stream_types as uint32_t (network byte order)
  uint32_t stream_types_net;
  memcpy(&stream_types_net, payload, sizeof(uint32_t));
  uint32_t stream_types = NET_TO_HOST_U32(stream_types_net);

  callbacks->on_stream_start(stream_types, client_ctx, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_stream_stop(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_stream_stop) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(uint32_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "STREAM_STOP payload too small (got %zu, expected %zu)", payload_len,
                     sizeof(uint32_t));
  }

  // Read stream_types as uint32_t (network byte order)
  uint32_t stream_types_net;
  memcpy(&stream_types_net, payload, sizeof(uint32_t));
  uint32_t stream_types = NET_TO_HOST_U32(stream_types_net);

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
