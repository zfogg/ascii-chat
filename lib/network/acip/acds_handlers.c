/**
 * @file network/acip/acds_handlers.c
 * @brief ACIP Discovery Server (ACDS) packet handlers with O(1) dispatch
 *
 * Implements O(1) array-based packet dispatching for ASCII Chat Discovery Server.
 * Handles ACIP packets 100-150: session management, WebRTC signaling, discovery.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "network/acip/acds_handlers.h"
#include "network/acip/messages.h"
#include "network/packet.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include "common.h"
#include "util/endian.h"
#include <string.h>

// =============================================================================
// ACDS Handler Function Types
// =============================================================================

// ACDS handler function signature
typedef asciichat_error_t (*acip_acds_handler_func_t)(const void *payload, size_t payload_len, int client_socket,
                                                      const char *client_ip, const acip_acds_callbacks_t *callbacks);

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

// =============================================================================
// ACDS Packet Dispatch Table (O(1) lookup)
// =============================================================================

static const acip_acds_handler_func_t g_acds_packet_handlers[200] = {
    [PACKET_TYPE_PING] = handle_acds_ping,
    [PACKET_TYPE_PONG] = handle_acds_pong,
    [PACKET_TYPE_ACIP_SESSION_CREATE] = handle_acds_session_create,
    [PACKET_TYPE_ACIP_SESSION_LOOKUP] = handle_acds_session_lookup,
    [PACKET_TYPE_ACIP_SESSION_JOIN] = handle_acds_session_join,
    [PACKET_TYPE_ACIP_SESSION_LEAVE] = handle_acds_session_leave,
    [PACKET_TYPE_ACIP_WEBRTC_SDP] = handle_acds_webrtc_sdp,
    [PACKET_TYPE_ACIP_WEBRTC_ICE] = handle_acds_webrtc_ice,
    [PACKET_TYPE_ACIP_DISCOVERY_PING] = handle_acds_discovery_ping,
};

// =============================================================================
// Public API
// =============================================================================

asciichat_error_t acip_handle_acds_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                          size_t payload_len, int client_socket, const char *client_ip,
                                          const acip_acds_callbacks_t *callbacks) {
  if (!callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid callbacks");
  }

  // TODO: Implement transport abstraction for ACDS responses or future protocol features
  (void)transport;

  // O(1) array-based dispatch - bounds check packet type
  if (type >= 200) {
    log_warn("Invalid ACDS packet type: %d (out of range)", type);
    return ASCIICHAT_OK;
  }

  // Lookup handler in dispatch table
  acip_acds_handler_func_t handler = g_acds_packet_handlers[type];
  if (!handler) {
    log_warn("Unhandled ACDS packet type: %d from %s", type, client_ip);
    return ASCIICHAT_OK;
  }

  // Dispatch to handler
  return handler(payload, payload_len, client_socket, client_ip, callbacks);
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
