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

#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/messages.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/packet_parsing.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
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

#define HANDLER_HASH_SIZE 32    // ~50% load factor for 14-16 entries
#define CLIENT_HANDLER_COUNT 19 // Added 5 crypto handshake handlers
#define SERVER_HANDLER_COUNT 19 // Added 3 crypto handshake handlers

/**
 * @brief Hash table entry for packet type to handler mapping
 */
typedef struct {
  packet_type_t key;   ///< Packet type (0 = empty slot)
  uint8_t handler_idx; ///< Handler index (0-based)
} handler_hash_entry_t;

// Hash function: simple modulo
#define HANDLER_HASH(type) ((type) % HANDLER_HASH_SIZE)

// Lookup function: linear probing, returns handler index or -1 if not found
static inline int handler_hash_lookup(const handler_hash_entry_t *table, packet_type_t type) {
  uint32_t h = HANDLER_HASH(type);
  log_dev_every(4500 * US_PER_MS_INT, "HANDLER_HASH_LOOKUP: type=%d, hash=%u", type, h);
  for (int i = 0; i < HANDLER_HASH_SIZE; i++) {
    uint32_t slot = (h + i) % HANDLER_HASH_SIZE;
    log_dev_every(4500 * US_PER_MS_INT, "  Checking slot %u: key=%d, handler_idx=%d", slot, table[slot].key,
                  table[slot].handler_idx);
    if (table[slot].key == 0) {
      log_dev_every(4500 * US_PER_MS_INT, "  Empty slot found - packet type %d not in hash table", type);
      return -1; // empty slot = not found
    }
    if (table[slot].key == type) {
      log_dev_every(4500 * US_PER_MS_INT, "  Found match at slot %u, handler_idx=%d", slot, table[slot].handler_idx);
      return table[slot].handler_idx;
    }
  }
  log_debug("  No match found after checking all slots");
  return -1;
}

// Client packet type -> handler index hash table
// Slots computed via linear probing: hash = type % 32
// clang-format off
static const handler_hash_entry_t g_client_handler_hash[HANDLER_HASH_SIZE] = {
   [0]  = {PACKET_TYPE_AUDIO_BATCH,                 1},   // hash(4000)=0
   [1]  = {PACKET_TYPE_AUDIO_OPUS_BATCH,            2},   // hash(4001)=1
   [7]  = {PACKET_TYPE_CRYPTO_AUTH_CHALLENGE,       15},  // hash(1104)=16, probed->7
   [8]  = {PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP,     16},  // hash(1107)=11, probed->8
   [9]  = {PACKET_TYPE_PING,                        6},   // hash(5001)=9
   [10] = {PACKET_TYPE_PONG,                        7},   // hash(5002)=10
   [11] = {PACKET_TYPE_CRYPTO_AUTH_FAILED,          17},  // hash(1108)=12, probed->11
   [14] = {PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE,   18},  // hash(1106)=18, probed->14
   [15] = {PACKET_TYPE_CLEAR_CONSOLE,               8},   // hash(5007)=15
   [16] = {PACKET_TYPE_SERVER_STATE,                3},   // hash(5008)=16
   [17] = {PACKET_TYPE_CRYPTO_REKEY_REQUEST,        9},   // hash(1201)=17
   [18] = {PACKET_TYPE_CRYPTO_REKEY_RESPONSE,       10},  // hash(1202)=18
   [19] = {PACKET_TYPE_ERROR_MESSAGE,               4},   // hash(2003)=19
   [20] = {PACKET_TYPE_REMOTE_LOG,                  5},   // hash(2004)=20
   [21] = {PACKET_TYPE_ACIP_SESSION_JOINED,         13},  // hash(6005)=21
   [22] = {PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT,    14},  // hash(1102)=14, probed->22
   [24] = {PACKET_TYPE_ASCII_FRAME,                 0},   // hash(3000)=24
   [25] = {PACKET_TYPE_ACIP_WEBRTC_SDP,             11},  // hash(6009)=25
   [26] = {PACKET_TYPE_ACIP_WEBRTC_ICE,             12},  // hash(6010)=26
};
// clang-format on

// Server packet type -> handler index hash table
// clang-format off
static const handler_hash_entry_t g_server_handler_hash[HANDLER_HASH_SIZE] = {
   [0]  = {PACKET_TYPE_AUDIO_BATCH,                2},   // hash(4000)=0
   [1]  = {PACKET_TYPE_PROTOCOL_VERSION,           0},   // hash(1)=1
   [2]  = {PACKET_TYPE_AUDIO_OPUS_BATCH,           3},   // hash(4001)=1, probed->2
   [8]  = {PACKET_TYPE_CLIENT_CAPABILITIES,        4},   // hash(5000)=8
   [9]  = {PACKET_TYPE_PING,                       5},   // hash(5001)=9
   [10] = {PACKET_TYPE_PONG,                       6},   // hash(5002)=10
   [11] = {PACKET_TYPE_CLIENT_JOIN,                7},   // hash(5003)=11
   [12] = {PACKET_TYPE_CLIENT_LEAVE,               8},   // hash(5004)=12
   [13] = {PACKET_TYPE_STREAM_START,               9},   // hash(5005)=13
   [14] = {PACKET_TYPE_STREAM_STOP,                10},  // hash(5006)=14
   [15] = {PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP,   16},  // hash(1103)=15
   [17] = {PACKET_TYPE_CRYPTO_REKEY_REQUEST,       13},  // hash(1201)=17
   [18] = {PACKET_TYPE_CRYPTO_REKEY_RESPONSE,      14},  // hash(1202)=18
   [19] = {PACKET_TYPE_ERROR_MESSAGE,              12},  // hash(2003)=19
   [20] = {PACKET_TYPE_REMOTE_LOG,                 11},  // hash(2004)=20
   [21] = {PACKET_TYPE_CRYPTO_REKEY_COMPLETE,      15},  // hash(1203)=19, probed->21
   [22] = {PACKET_TYPE_CRYPTO_AUTH_RESPONSE,       17},  // hash(1105)=17, probed->22
   [23] = {PACKET_TYPE_CRYPTO_NO_ENCRYPTION,       18},  // hash(1109)=21, probed->23
   [25] = {PACKET_TYPE_IMAGE_FRAME,                1},   // hash(3001)=25
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
static asciichat_error_t handle_client_crypto_key_exchange_init(const void *payload, size_t payload_len,
                                                                const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_crypto_auth_challenge(const void *payload, size_t payload_len,
                                                             const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_crypto_server_auth_resp(const void *payload, size_t payload_len,
                                                               const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_crypto_auth_failed(const void *payload, size_t payload_len,
                                                          const acip_client_callbacks_t *callbacks);
static asciichat_error_t handle_client_crypto_handshake_complete(const void *payload, size_t payload_len,
                                                                 const acip_client_callbacks_t *callbacks);

// Client handler dispatch table (indexed by client_handler_index())
static const acip_client_handler_func_t g_client_handlers[CLIENT_HANDLER_COUNT] = {
    handle_client_ascii_frame,               // 0
    handle_client_audio_batch,               // 1
    handle_client_audio_opus_batch,          // 2
    handle_client_server_state,              // 3
    handle_client_error_message,             // 4
    handle_client_remote_log,                // 5
    handle_client_ping,                      // 6
    handle_client_pong,                      // 7
    handle_client_clear_console,             // 8
    handle_client_crypto_rekey_request,      // 9
    handle_client_crypto_rekey_response,     // 10
    handle_client_webrtc_sdp,                // 11
    handle_client_webrtc_ice,                // 12
    handle_client_session_joined,            // 13
    handle_client_crypto_key_exchange_init,  // 14
    handle_client_crypto_auth_challenge,     // 15
    handle_client_crypto_server_auth_resp,   // 16
    handle_client_crypto_auth_failed,        // 17
    handle_client_crypto_handshake_complete, // 18
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

static asciichat_error_t handle_client_crypto_key_exchange_init(const void *payload, size_t payload_len,
                                                                const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_key_exchange_init) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_key_exchange_init(PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, payload, payload_len,
                                         callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_crypto_auth_challenge(const void *payload, size_t payload_len,
                                                             const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_auth_challenge) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_auth_challenge(PACKET_TYPE_CRYPTO_AUTH_CHALLENGE, payload, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_crypto_server_auth_resp(const void *payload, size_t payload_len,
                                                               const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_server_auth_resp) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_server_auth_resp(PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP, payload, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_crypto_auth_failed(const void *payload, size_t payload_len,
                                                          const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_auth_failed) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_auth_failed(PACKET_TYPE_CRYPTO_AUTH_FAILED, payload, payload_len, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_client_crypto_handshake_complete(const void *payload, size_t payload_len,
                                                                 const acip_client_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_handshake_complete) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_handshake_complete(PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE, payload, payload_len,
                                          callbacks->app_ctx);
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
static asciichat_error_t handle_server_crypto_key_exchange_resp(const void *payload, size_t payload_len,
                                                                void *client_ctx,
                                                                const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_crypto_auth_response(const void *payload, size_t payload_len, void *client_ctx,
                                                            const acip_server_callbacks_t *callbacks);
static asciichat_error_t handle_server_crypto_no_encryption(const void *payload, size_t payload_len, void *client_ctx,
                                                            const acip_server_callbacks_t *callbacks);

// Server handler dispatch table (indexed by server_handler_index())
static const acip_server_handler_func_t g_server_handlers[SERVER_HANDLER_COUNT] = {
    handle_server_protocol_version,         // 0
    handle_server_image_frame,              // 1
    handle_server_audio_batch,              // 2
    handle_server_audio_opus_batch,         // 3
    handle_server_capabilities,             // 4
    handle_server_ping,                     // 5
    handle_server_pong,                     // 6
    handle_server_client_join,              // 7
    handle_server_client_leave,             // 8
    handle_server_stream_start,             // 9
    handle_server_stream_stop,              // 10
    handle_server_remote_log,               // 11
    handle_server_error_message,            // 12
    handle_server_crypto_rekey_request,     // 13
    handle_server_crypto_rekey_response,    // 14
    handle_server_crypto_rekey_complete,    // 15
    handle_server_crypto_key_exchange_resp, // 16
    handle_server_crypto_auth_response,     // 17
    handle_server_crypto_no_encryption,     // 18
};

// Packet type names for debugging (matches handler table order)
static const char *g_packet_type_name(packet_type_t type) {
  switch (type) {
  case PACKET_TYPE_PROTOCOL_VERSION:
    return "PROTOCOL_VERSION";
  case PACKET_TYPE_IMAGE_FRAME:
    return "IMAGE_FRAME";
  case PACKET_TYPE_AUDIO_BATCH:
    return "AUDIO_BATCH";
  case PACKET_TYPE_AUDIO_OPUS_BATCH:
    return "AUDIO_OPUS_BATCH";
  case PACKET_TYPE_CLIENT_CAPABILITIES:
    return "CLIENT_CAPABILITIES";
  case PACKET_TYPE_PING:
    return "PING";
  case PACKET_TYPE_PONG:
    return "PONG";
  case PACKET_TYPE_CLIENT_JOIN:
    return "CLIENT_JOIN";
  case PACKET_TYPE_CLIENT_LEAVE:
    return "CLIENT_LEAVE";
  case PACKET_TYPE_STREAM_START:
    return "STREAM_START";
  case PACKET_TYPE_STREAM_STOP:
    return "STREAM_STOP";
  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP:
    return "CRYPTO_KEY_EXCHANGE_RESP";
  case PACKET_TYPE_CRYPTO_REKEY_REQUEST:
    return "CRYPTO_REKEY_REQUEST";
  case PACKET_TYPE_CRYPTO_REKEY_RESPONSE:
    return "CRYPTO_REKEY_RESPONSE";
  case PACKET_TYPE_ERROR_MESSAGE:
    return "ERROR_MESSAGE";
  case PACKET_TYPE_REMOTE_LOG:
    return "REMOTE_LOG";
  case PACKET_TYPE_CRYPTO_REKEY_COMPLETE:
    return "CRYPTO_REKEY_COMPLETE";
  case PACKET_TYPE_CRYPTO_AUTH_RESPONSE:
    return "CRYPTO_AUTH_RESPONSE";
  case PACKET_TYPE_CRYPTO_NO_ENCRYPTION:
    return "CRYPTO_NO_ENCRYPTION";
  default:
    return "UNKNOWN";
  }
}

asciichat_error_t acip_handle_server_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }
  (void)transport;

  // DEBUG: Log all packet types received
  log_info("ACIP_HANDLE: Received packet type=%d (0x%04x), payload_len=%zu", type, type, payload_len);

  // O(1) dispatch via hash table lookup
  int idx = handler_hash_lookup(g_server_handler_hash, type);
  if (idx < 0) {
    log_error("ACIP_HANDLER_NOT_FOUND: No handler for packet type=%d (0x%04x, name=%s)", type, type,
              g_packet_type_name(type));
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unhandled server packet type: %d", type);
  }

  log_info("📥 [HANDLER_DISPATCH] Calling handler: type=%d (%s), handler_idx=%d, payload=%zu bytes, client_ctx=%p",
           type, g_packet_type_name(type), idx, payload_len, client_ctx);
  asciichat_error_t result = g_server_handlers[idx](payload, payload_len, client_ctx, callbacks);

  if (result != ASCIICHAT_OK) {
    log_error("❌ [HANDLER_ERROR] Handler failed: type=%d (%s), result=%d", type, g_packet_type_name(type), result);
  } else {
    log_info("✅ [HANDLER_COMPLETE] Handler succeeded: type=%d (%s), payload=%zu bytes processed", type,
             g_packet_type_name(type), payload_len);
  }

  return result;
}

// =============================================================================
// Server Handler Implementations (extracted from switch cases)
// =============================================================================

static asciichat_error_t handle_server_image_frame(const void *payload, size_t payload_len, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  log_info("ACIP_IMAGE_FRAME_HANDLER: Received IMAGE_FRAME packet, payload_len=%zu, client_ctx=%p", payload_len,
           client_ctx);

  if (!callbacks->on_image_frame) {
    log_warn("ACIP_IMAGE_FRAME_HANDLER: No callback registered for on_image_frame");
    return ASCIICHAT_OK;
  }

  log_info("ACIP_IMAGE_FRAME_HANDLER: Callback is registered, checking payload size (need %zu bytes)",
           sizeof(image_frame_packet_t));

  if (payload_len < sizeof(image_frame_packet_t)) {
    log_error("ACIP_IMAGE_FRAME_HANDLER: Payload too small: %zu bytes (need %zu)", payload_len,
              sizeof(image_frame_packet_t));
    return SET_ERRNO(ERROR_INVALID_PARAM, "IMAGE_FRAME payload too small: %zu bytes (need %zu)", payload_len,
                     sizeof(image_frame_packet_t));
  }

  // Debug: Log raw payload bytes
  const uint8_t *payload_bytes = (const uint8_t *)payload;
  log_dev_every(4500 * US_PER_MS_INT, "IMAGE_FRAME payload (%zu bytes):", payload_len);
  char hex_buf[512];
  size_t hex_pos = 0;
  size_t max_bytes = (payload_len < 48) ? payload_len : 48;
  for (size_t i = 0; i < max_bytes && hex_pos < sizeof(hex_buf) - 4; i++) {
    hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02x ", payload_bytes[i]);
  }
  log_dev_every(4500 * US_PER_MS_INT, "   First bytes: %s", hex_buf);

  // Extract header
  image_frame_packet_t header;
  memcpy(&header, payload, sizeof(header));

  // Debug: Log raw header before byte order conversion
  log_dev_every(4500 * US_PER_MS_INT, "IMAGE_FRAME header (raw network order):");
  log_dev_every(4500 * US_PER_MS_INT, "   width=0x%08x height=0x%08x pixel_format=0x%08x compressed_size=0x%08x",
                header.width, header.height, header.pixel_format, header.compressed_size);

  // Convert from network byte order
  header.width = NET_TO_HOST_U32(header.width);
  header.height = NET_TO_HOST_U32(header.height);
  header.pixel_format = NET_TO_HOST_U32(header.pixel_format);
  header.compressed_size = NET_TO_HOST_U32(header.compressed_size);
  header.checksum = NET_TO_HOST_U32(header.checksum);
  header.timestamp = NET_TO_HOST_U32(header.timestamp);

  // Debug: Log header after byte order conversion
  log_dev_every(4500 * US_PER_MS_INT, "IMAGE_FRAME header (after host byte order):");
  log_dev_every(4500 * US_PER_MS_INT, "   width=%u height=%u pixel_format=%u compressed_size=%u", header.width,
                header.height, header.pixel_format, header.compressed_size);

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

  log_info(
      "📹 [IMAGE_FRAME_CALLBACK] Invoking on_image_frame callback: %ux%u pixels, format=%u, %zu bytes, client_ctx=%p",
      header.width, header.height, header.pixel_format, pixel_data_len, client_ctx);

  callbacks->on_image_frame(&header, pixel_data, pixel_data_len, client_ctx, callbacks->app_ctx);

  log_info("✅ [IMAGE_FRAME_DONE] on_image_frame callback returned successfully");
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
  log_debug("handle_server_capabilities: payload_len=%zu, client_ctx=%p, callbacks=%p", payload_len, client_ctx,
            (const void *)callbacks);

  if (!callbacks->on_capabilities) {
    log_error("on_capabilities callback is NULL!");
    return ASCIICHAT_OK;
  }

  log_error("Calling on_capabilities callback...");
  callbacks->on_capabilities(payload, payload_len, client_ctx, callbacks->app_ctx);
  log_error("on_capabilities callback returned");
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

static asciichat_error_t handle_server_crypto_key_exchange_resp(const void *payload, size_t payload_len,
                                                                void *client_ctx,
                                                                const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_key_exchange_resp) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_key_exchange_resp(PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, payload, payload_len, client_ctx,
                                         callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_crypto_auth_response(const void *payload, size_t payload_len, void *client_ctx,
                                                            const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_auth_response) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_auth_response(PACKET_TYPE_CRYPTO_AUTH_RESPONSE, payload, payload_len, client_ctx,
                                     callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_server_crypto_no_encryption(const void *payload, size_t payload_len, void *client_ctx,
                                                            const acip_server_callbacks_t *callbacks) {
  if (!callbacks->on_crypto_no_encryption) {
    return ASCIICHAT_OK;
  }

  // Dispatch to application callback with packet type
  callbacks->on_crypto_no_encryption(PACKET_TYPE_CRYPTO_NO_ENCRYPTION, payload, payload_len, client_ctx,
                                     callbacks->app_ctx);
  return ASCIICHAT_OK;
}
