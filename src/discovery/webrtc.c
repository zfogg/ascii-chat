/**
 * @file discovery/webrtc.c
 * @brief WebRTC P2P signaling for discovery mode failover
 *
 * Implements direct peer-to-peer SDP/ICE exchange for discovery mode migration.
 * Unlike client mode (which uses ACDS relay), failover uses direct TCP connections
 * to send WebRTC signaling messages.
 *
 * @ingroup discovery
 */

#include "webrtc.h"

#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/network/acip/protocol.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>

#include <string.h>

// =============================================================================
// Global State
// =============================================================================

/**
 * @brief TCP transport for sending SDP/ICE directly to peer
 *
 * Set when initiating P2P connection during failover.
 * Cleared when migration completes or connection closes.
 * Protected by g_webrtc_mutex.
 */
static acip_transport_t *g_tcp_transport = NULL;

/**
 * @brief Local session context for direct signaling
 *
 * Set before peer manager generates any local descriptions.
 * Protected by g_webrtc_mutex.
 */
static struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];
  bool is_set;
} g_session_context = {0};

/**
 * @brief Mutex protecting signaling state
 */
static mutex_t g_webrtc_mutex;
static bool g_webrtc_mutex_initialized = false;
static static_mutex_t g_webrtc_init_mutex = STATIC_MUTEX_INIT;

// =============================================================================
// Internal Helpers
// =============================================================================

/**
 * @brief Initialize mutex (called once)
 *
 * Uses static mutex to prevent TOCTOU race condition where multiple threads
 * might attempt to initialize the main mutex simultaneously.
 */
static void ensure_mutex_initialized(void) {
  static_mutex_lock(&g_webrtc_init_mutex);

  // Check again under lock to prevent race condition
  if (!g_webrtc_mutex_initialized) {
    mutex_init(&g_webrtc_mutex);
    g_webrtc_mutex_initialized = true;
  }

  static_mutex_unlock(&g_webrtc_init_mutex);
}

// =============================================================================
// Signaling Callback Implementations (Direct TCP)
// =============================================================================

/**
 * @brief Send SDP offer/answer directly over TCP to peer
 *
 * Constructs PACKET_TYPE_ACIP_WEBRTC_SDP and sends via direct TCP transport.
 * This is used during discovery mode failover to bypass ACDS.
 *
 * Packet format:
 * - Header: acip_webrtc_sdp_t (variable size)
 * - Variable payload: SDP string
 */
static asciichat_error_t discovery_send_sdp(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                            const char *sdp_type, const char *sdp, void *user_data) {
  (void)recipient_id; // Unused - direct connection, no need to route
  (void)user_data;    // Unused

  ensure_mutex_initialized();
  mutex_lock(&g_webrtc_mutex);

  // Verify state
  if (!g_tcp_transport) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_STATE, "TCP transport not set for direct signaling");
    return ERROR_INVALID_STATE;
  }

  if (!g_session_context.is_set) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_STATE, "Session context not set for direct signaling");
    return ERROR_INVALID_STATE;
  }

  // Verify inputs
  if (!sdp) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_PARAM, "SDP is NULL");
    return ERROR_INVALID_PARAM;
  }

  size_t sdp_len = strlen(sdp);
  if (sdp_len > 4096) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_PARAM, "SDP too large (>4096 bytes)");
    return ERROR_INVALID_PARAM;
  }

  // Construct packet
  // Header: acip_webrtc_sdp_t (defined in network/acip/acds.h)
  // Followed by: SDP string
  uint8_t sdp_msg_buf[sizeof(acip_webrtc_sdp_t) + 4096];
  acip_webrtc_sdp_t *sdp_msg = (acip_webrtc_sdp_t *)sdp_msg_buf;

  memcpy(sdp_msg->session_id, session_id, 16);
  memcpy(sdp_msg->sender_id, g_session_context.participant_id, 16);
  memset(sdp_msg->recipient_id, 0, 16);                          // Broadcast to all
  sdp_msg->sdp_type = (strcmp(sdp_type, "answer") == 0) ? 1 : 0; // 0=offer, 1=answer
  sdp_msg->sdp_len = (uint16_t)sdp_len;
  memcpy(sdp_msg_buf + sizeof(acip_webrtc_sdp_t), sdp, sdp_len);

  // Send via direct TCP transport (not ACDS)
  size_t msg_size = sizeof(acip_webrtc_sdp_t) + sdp_len;
  asciichat_error_t result =
      packet_send_via_transport(g_tcp_transport, PACKET_TYPE_ACIP_WEBRTC_SDP, sdp_msg_buf, msg_size);

  mutex_unlock(&g_webrtc_mutex);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to send SDP via direct TCP: %d", result);
    return result;
  }

  log_debug("Sent SDP %s directly to peer (TCP transport)", sdp_type);
  return ASCIICHAT_OK;
}

/**
 * @brief Send ICE candidate directly over TCP to peer
 *
 * Constructs PACKET_TYPE_ACIP_WEBRTC_ICE and sends via direct TCP transport.
 */
static asciichat_error_t discovery_send_ice(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                            const char *candidate, const char *mid, void *user_data) {
  (void)recipient_id; // Unused - direct connection, no need to route
  (void)user_data;    // Unused

  ensure_mutex_initialized();
  mutex_lock(&g_webrtc_mutex);

  // Verify state
  if (!g_tcp_transport) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_STATE, "TCP transport not set for direct signaling");
    return ERROR_INVALID_STATE;
  }

  if (!g_session_context.is_set) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_STATE, "Session context not set for direct signaling");
    return ERROR_INVALID_STATE;
  }

  // Verify inputs
  if (!candidate) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_PARAM, "Candidate is NULL");
    return ERROR_INVALID_PARAM;
  }

  size_t candidate_len = strlen(candidate);

  if (candidate_len > 4096) {
    mutex_unlock(&g_webrtc_mutex);
    SET_ERRNO(ERROR_INVALID_PARAM, "ICE candidate too large (>4096 bytes)");
    return ERROR_INVALID_PARAM;
  }

  // Construct packet
  // Header: acip_webrtc_ice_t (defined in network/acip/acds.h)
  // Followed by: candidate string (mid is already included in candidate string)
  // Note: ICE candidate string format is: "candidate:..." (mid is separate in WebRTC but here we just send the
  // candidate)
  uint8_t ice_msg_buf[sizeof(acip_webrtc_ice_t) + 4096];
  acip_webrtc_ice_t *ice_msg = (acip_webrtc_ice_t *)ice_msg_buf;

  memcpy(ice_msg->session_id, session_id, 16);
  memcpy(ice_msg->sender_id, g_session_context.participant_id, 16);
  memset(ice_msg->recipient_id, 0, 16); // Broadcast to all
  ice_msg->candidate_len = (uint16_t)candidate_len;
  memcpy(ice_msg_buf + sizeof(acip_webrtc_ice_t), candidate, candidate_len);

  // Send via direct TCP transport (not ACDS)
  size_t msg_size = sizeof(acip_webrtc_ice_t) + candidate_len;
  asciichat_error_t result =
      packet_send_via_transport(g_tcp_transport, PACKET_TYPE_ACIP_WEBRTC_ICE, ice_msg_buf, msg_size);

  mutex_unlock(&g_webrtc_mutex);

  (void)mid; // Unused - mid is part of the candidate string in libdatachannel

  if (result != ASCIICHAT_OK) {
    log_error("Failed to send ICE candidate via direct TCP: %d", result);
    return result;
  }

  log_debug("Sent ICE candidate directly to peer (TCP transport)");
  return ASCIICHAT_OK;
}

// =============================================================================
// Public API
// =============================================================================

webrtc_signaling_callbacks_t discovery_webrtc_get_direct_signaling_callbacks(acip_transport_t *tcp_transport,
                                                                             const uint8_t session_id[16],
                                                                             const uint8_t participant_id[16]) {
  ensure_mutex_initialized();
  mutex_lock(&g_webrtc_mutex);

  g_tcp_transport = tcp_transport;

  if (session_id && participant_id) {
    memcpy(g_session_context.session_id, session_id, 16);
    memcpy(g_session_context.participant_id, participant_id, 16);
    g_session_context.is_set = true;
  }

  mutex_unlock(&g_webrtc_mutex);

  webrtc_signaling_callbacks_t callbacks = {
      .send_sdp = discovery_send_sdp,
      .send_ice = discovery_send_ice,
      .user_data = NULL, // Unused - we use global state
  };

  return callbacks;
}

void discovery_webrtc_set_tcp_transport(acip_transport_t *transport) {
  ensure_mutex_initialized();
  mutex_lock(&g_webrtc_mutex);
  g_tcp_transport = transport;
  mutex_unlock(&g_webrtc_mutex);
}

void discovery_webrtc_set_session_context(const uint8_t session_id[16], const uint8_t participant_id[16]) {
  ensure_mutex_initialized();
  mutex_lock(&g_webrtc_mutex);

  if (session_id && participant_id) {
    memcpy(g_session_context.session_id, session_id, 16);
    memcpy(g_session_context.participant_id, participant_id, 16);
    g_session_context.is_set = true;
  } else {
    g_session_context.is_set = false;
  }

  mutex_unlock(&g_webrtc_mutex);
}

void discovery_webrtc_cleanup_transport(void) {
  ensure_mutex_initialized();
  mutex_lock(&g_webrtc_mutex);

  g_tcp_transport = NULL;
  g_session_context.is_set = false;

  mutex_unlock(&g_webrtc_mutex);
}
