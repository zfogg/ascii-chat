/**
 * @file network/acip/acds_handlers.c
 * @brief ACIP Discovery Server (ACDS) packet handlers with O(1) dispatch
 *
 * Implements O(1) array-based packet dispatching for ascii-chat Discovery Server.
 * Handles ACIP packets 100-150: session management, WebRTC signaling, discovery.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/network/acip/acds_handlers.h>
#include <ascii-chat/network/acip/messages.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

// =============================================================================
// ACDS Handler Function Types and Hash Table
// =============================================================================

// ACDS handler function signature
typedef asciichat_error_t (*acip_acds_handler_func_t)(const void *payload, size_t payload_len, int client_socket,
                                                      const char *client_ip, const acip_acds_callbacks_t *callbacks);

// Hash table for O(1) packet dispatch
#define ACDS_HASH_SIZE 32
#define ACDS_HANDLER_COUNT 11

/**
 * @brief Hash table entry for ACDS packet type to handler mapping
 */
typedef struct {
  packet_type_t key;   ///< Packet type (0 = empty slot)
  uint8_t handler_idx; ///< Handler index (0-based)
} acds_hash_entry_t;

#define ACDS_HASH(type) ((type) % ACDS_HASH_SIZE)

static inline int acds_handler_hash_lookup(const acds_hash_entry_t *table, packet_type_t type) {
  uint32_t h = ACDS_HASH(type);
  for (int i = 0; i < ACDS_HASH_SIZE; i++) {
    uint32_t slot = (h + i) % ACDS_HASH_SIZE;
    if (table[slot].key == 0)
      return -1;
    if (table[slot].key == type)
      return table[slot].handler_idx;
  }
  return -1;
}

// =============================================================================
// Forward Declarations
// =============================================================================

static asciichat_error_t handle_acds_session_create(const void *payload, size_t payload_len, int client_socket,
                                                    const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_session_lookup(const void *payload, size_t payload_len, int client_socket,
                                                    const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_session_join(const void *payload, size_t payload_len, int client_socket,
                                                  const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_session_leave(const void *payload, size_t payload_len, int client_socket,
                                                   const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_webrtc_sdp(const void *payload, size_t payload_len, int client_socket,
                                                const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_webrtc_ice(const void *payload, size_t payload_len, int client_socket,
                                                const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_discovery_ping(const void *payload, size_t payload_len, int client_socket,
                                                    const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_ping(const void *payload, size_t payload_len, int client_socket,
                                          const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_pong(const void *payload, size_t payload_len, int client_socket,
                                          const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_host_announcement(const void *payload, size_t payload_len, int client_socket,
                                                       const char *client_ip, const acip_acds_callbacks_t *callbacks);
static asciichat_error_t handle_acds_host_lost(const void *payload, size_t payload_len, int client_socket,
                                               const char *client_ip, const acip_acds_callbacks_t *callbacks);

// ACDS handler dispatch table
static const acip_acds_handler_func_t g_acds_handlers[ACDS_HANDLER_COUNT] = {
    handle_acds_ping,              // 0
    handle_acds_pong,              // 1
    handle_acds_session_create,    // 2
    handle_acds_session_lookup,    // 3
    handle_acds_session_join,      // 4
    handle_acds_session_leave,     // 5
    handle_acds_webrtc_sdp,        // 6
    handle_acds_webrtc_ice,        // 7
    handle_acds_discovery_ping,    // 8
    handle_acds_host_announcement, // 9
    handle_acds_host_lost,         // 10
};

// ACDS packet type -> handler index hash table
// clang-format off
static const acds_hash_entry_t g_acds_handler_hash[ACDS_HASH_SIZE] = {
    [9]  = {PACKET_TYPE_PING,                   0},   // hash(5001)=9
    [10] = {PACKET_TYPE_PONG,                   1},   // hash(5002)=10
    [13] = {PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT, 9},   // hash(6061)=13
    [16] = {PACKET_TYPE_ACIP_SESSION_CREATE,    2},   // hash(6000)=16
    [17] = {PACKET_TYPE_ACIP_HOST_LOST,         10},  // hash(6065)=17
    [18] = {PACKET_TYPE_ACIP_SESSION_LOOKUP,    3},   // hash(6002)=18
    [20] = {PACKET_TYPE_ACIP_SESSION_JOIN,      4},   // hash(6004)=20
    [21] = {PACKET_TYPE_ACIP_DISCOVERY_PING,    8},   // hash(6100)=20, probed->21
    [22] = {PACKET_TYPE_ACIP_SESSION_LEAVE,     5},   // hash(6006)=22
    [25] = {PACKET_TYPE_ACIP_WEBRTC_SDP,        6},   // hash(6009)=25
    [26] = {PACKET_TYPE_ACIP_WEBRTC_ICE,        7},   // hash(6010)=26
};
// clang-format on

// =============================================================================
// Public API
// =============================================================================

asciichat_error_t acip_handle_acds_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                          size_t payload_len, int client_socket, const char *client_ip,
                                          const acip_acds_callbacks_t *callbacks) {
  if (!callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid callbacks");
  }
  (void)transport;

  // O(1) dispatch via hash table lookup
  int idx = acds_handler_hash_lookup(g_acds_handler_hash, type);
  if (idx < 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unhandled ACDS packet type: %d from %s", type, client_ip);
  }

  return g_acds_handlers[idx](payload, payload_len, client_socket, client_ip, callbacks);
}

// =============================================================================
// ACDS Handler Implementations
// =============================================================================

static asciichat_error_t handle_acds_session_create(const void *payload, size_t payload_len, int client_socket,
                                                    const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_session_create) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_session_create_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "SESSION_CREATE payload too small from %s", client_ip);
  }

  const acip_session_create_t *req = (const acip_session_create_t *)payload;

  // Validate session parameters
  if (req->max_participants == 0 || req->max_participants > 32) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid max_participants: %u from %s (expected: 1-32)",
                     req->max_participants, client_ip);
  }

  if (req->session_type > 1) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session_type: %u from %s (expected: 0=DIRECT_TCP or 1=WEBRTC)",
                     req->session_type, client_ip);
  }

  // Validate server port (0 = system assigned, 1-65535 = valid)
  // Allow 0 for auto-assignment during WEBRTC mode
  if (req->session_type == 0 && req->server_port == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "DIRECT_TCP session requires valid server_port from %s", client_ip);
  }

  callbacks->on_session_create(req, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_session_lookup(const void *payload, size_t payload_len, int client_socket,
                                                    const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_session_lookup) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_session_lookup_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "SESSION_LOOKUP payload too small from %s", client_ip);
  }

  const acip_session_lookup_t *req = (const acip_session_lookup_t *)payload;
  callbacks->on_session_lookup(req, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_session_join(const void *payload, size_t payload_len, int client_socket,
                                                  const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_session_join) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_session_join_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "SESSION_JOIN payload too small from %s", client_ip);
  }

  const acip_session_join_t *req = (const acip_session_join_t *)payload;
  callbacks->on_session_join(req, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_session_leave(const void *payload, size_t payload_len, int client_socket,
                                                   const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_session_leave) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_session_leave_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "SESSION_LEAVE payload too small from %s", client_ip);
  }

  const acip_session_leave_t *req = (const acip_session_leave_t *)payload;
  callbacks->on_session_leave(req, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_webrtc_sdp(const void *payload, size_t payload_len, int client_socket,
                                                const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_webrtc_sdp) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_webrtc_sdp_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "WEBRTC_SDP payload too small from %s", client_ip);
  }

  const acip_webrtc_sdp_t *sdp = (const acip_webrtc_sdp_t *)payload;

  // Validate sdp_len against actual payload size (convert from network byte order)
  uint16_t sdp_len_host = NET_TO_HOST_U16(sdp->sdp_len);
  size_t expected_size = sizeof(acip_webrtc_sdp_t) + sdp_len_host;
  if (expected_size > payload_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "WEBRTC_SDP size mismatch from %s: claims %u bytes but payload is %zu",
                     client_ip, sdp_len_host, payload_len);
  }

  callbacks->on_webrtc_sdp(sdp, payload_len, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_webrtc_ice(const void *payload, size_t payload_len, int client_socket,
                                                const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_webrtc_ice) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_webrtc_ice_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "WEBRTC_ICE payload too small from %s", client_ip);
  }

  const acip_webrtc_ice_t *ice = (const acip_webrtc_ice_t *)payload;

  // Validate candidate_len against actual payload size (convert from network byte order)
  uint16_t candidate_len_host = NET_TO_HOST_U16(ice->candidate_len);
  size_t expected_size = sizeof(acip_webrtc_ice_t) + candidate_len_host;
  if (expected_size > payload_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "WEBRTC_ICE size mismatch from %s: claims %u bytes but payload is %zu",
                     client_ip, candidate_len_host, payload_len);
  }

  callbacks->on_webrtc_ice(ice, payload_len, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_discovery_ping(const void *payload, size_t payload_len, int client_socket,
                                                    const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;

  if (callbacks->on_discovery_ping) {
    callbacks->on_discovery_ping(payload, payload_len, client_socket, client_ip, callbacks->app_ctx);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_ping(const void *payload, size_t payload_len, int client_socket,
                                          const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;
  (void)callbacks;

  log_debug("ACDS keepalive: Received PING from %s, responding with PONG", client_ip);

  // Respond with PONG to keep connection alive
  asciichat_error_t result = packet_send(client_socket, PACKET_TYPE_PONG, NULL, 0);
  if (result != ASCIICHAT_OK) {
    log_warn("ACDS keepalive: Failed to send PONG to %s: %s", client_ip, asciichat_error_string(result));
    return result;
  }

  log_debug("ACDS keepalive: Sent PONG to %s", client_ip);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_pong(const void *payload, size_t payload_len, int client_socket,
                                          const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  (void)payload;
  (void)payload_len;
  (void)client_socket;
  (void)callbacks;

  log_debug("ACDS keepalive: Received PONG from %s", client_ip);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_host_announcement(const void *payload, size_t payload_len, int client_socket,
                                                       const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_host_announcement) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_host_announcement_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "HOST_ANNOUNCEMENT payload too small from %s", client_ip);
  }

  const acip_host_announcement_t *announcement = (const acip_host_announcement_t *)payload;
  callbacks->on_host_announcement(announcement, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}

static asciichat_error_t handle_acds_host_lost(const void *payload, size_t payload_len, int client_socket,
                                               const char *client_ip, const acip_acds_callbacks_t *callbacks) {
  if (!callbacks->on_host_lost) {
    return ASCIICHAT_OK;
  }

  if (payload_len < sizeof(acip_host_lost_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "HOST_LOST payload too small from %s", client_ip);
  }

  const acip_host_lost_t *host_lost = (const acip_host_lost_t *)payload;
  callbacks->on_host_lost(host_lost, client_socket, client_ip, callbacks->app_ctx);
  return ASCIICHAT_OK;
}
