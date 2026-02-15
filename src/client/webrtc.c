/**
 * @file client/webrtc.c
 * @brief Client-side WebRTC signaling callback implementations
 *
 * Implements the send_sdp and send_ice callbacks that the peer manager
 * uses to transmit local SDP/ICE to remote peers via ACDS.
 *
 * PACKET CONSTRUCTION:
 * ====================
 * Both callbacks construct ACIP packets following the wire format:
 * - Fixed header (acip_webrtc_sdp_t or acip_webrtc_ice_t)
 * - Variable payload (SDP string or ICE candidate+mid)
 *
 * THREAD SAFETY:
 * ==============
 * Callbacks use mutex protection for accessing shared state (ACDS transport,
 * session context). Safe to call from peer manager threads.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "webrtc.h"
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/protocol.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>

#include <string.h>

// =============================================================================
// Global State
// =============================================================================

/**
 * @brief ACDS transport for sending signaling messages
 *
 * Set when connecting to ACDS, cleared on disconnect.
 * Protected by g_signaling_mutex.
 */
static acip_transport_t *g_acds_transport = NULL;

/**
 * @brief WebRTC data channel transport for peer-to-peer connection
 *
 * Set when WebRTC connection is established, cleared on disconnect.
 * This is the actual data channel transport, separate from ACDS signaling.
 * Protected by g_signaling_mutex.
 */
static acip_transport_t *g_webrtc_transport = NULL;

/**
 * @brief Local session context
 *
 * Set when joining ACDS session, cleared on leave.
 * Protected by g_signaling_mutex.
 */
static struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];
  bool is_set;
} g_session_context = {0};

/**
 * @brief Mutex protecting signaling state
 */
static mutex_t g_signaling_mutex;
static bool g_signaling_mutex_initialized = false;
static static_mutex_t g_signaling_init_mutex = STATIC_MUTEX_INIT;

// =============================================================================
// Internal Helpers
// =============================================================================

/**
 * @brief Initialize signaling mutex (called once)
 *
 * Uses static mutex to prevent TOCTOU race condition where multiple threads
 * might attempt to initialize the main mutex simultaneously.
 */
static void ensure_mutex_initialized(void) {
  static_mutex_lock(&g_signaling_init_mutex);

  // Check again under lock to prevent race condition
  if (!g_signaling_mutex_initialized) {
    mutex_init(&g_signaling_mutex);
    g_signaling_mutex_initialized = true;
  }

  static_mutex_unlock(&g_signaling_init_mutex);
}

// =============================================================================
// Signaling Callback Implementations
// =============================================================================

/**
 * @brief Send SDP offer/answer via ACDS
 *
 * Constructs PACKET_TYPE_ACIP_WEBRTC_SDP and sends via ACDS transport.
 *
 * Packet format:
 * - Header: acip_webrtc_sdp_t (50 bytes)
 * - Payload: SDP string (variable length)
 */
static asciichat_error_t client_send_sdp(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                         const char *sdp_type, const char *sdp, void *user_data) {
  (void)user_data;

  if (!session_id || !recipient_id || !sdp_type || !sdp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP parameters");
  }

  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  // Check if ACDS connection is active
  if (!g_acds_transport) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "ACDS transport not available");
  }

  // Check if session context is set
  if (!g_session_context.is_set) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session context not set");
  }

  // Calculate SDP length
  size_t sdp_len = strlen(sdp);
  if (sdp_len == 0 || sdp_len >= 8192) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP length: %zu", sdp_len);
  }

  // Allocate packet buffer (header + SDP string)
  size_t total_len = sizeof(acip_webrtc_sdp_t) + sdp_len;
  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate SDP packet");
  }

  // Fill header
  acip_webrtc_sdp_t *header = (acip_webrtc_sdp_t *)packet;
  memcpy(header->session_id, session_id, 16);
  memcpy(header->sender_id, g_session_context.participant_id, 16);
  memcpy(header->recipient_id, recipient_id, 16);
  header->sdp_type = (strcmp(sdp_type, "offer") == 0) ? 0 : 1;
  header->sdp_len = HOST_TO_NET_U16((uint16_t)sdp_len);

  log_debug("SDP packet: session_id=%02x%02x%02x%02x..., sender=%02x%02x%02x%02x..., recipient=%02x%02x%02x%02x...",
            session_id[0], session_id[1], session_id[2], session_id[3], g_session_context.participant_id[0],
            g_session_context.participant_id[1], g_session_context.participant_id[2],
            g_session_context.participant_id[3], recipient_id[0], recipient_id[1], recipient_id[2], recipient_id[3]);

  // Copy SDP string after header
  memcpy(packet + sizeof(acip_webrtc_sdp_t), sdp, sdp_len);

  log_info("Sending WebRTC SDP %s to participant (%.8s...) via ACDS", sdp_type, (const char *)recipient_id);

  // Send via ACDS transport using generic packet sender
  asciichat_error_t result =
      packet_send_via_transport(g_acds_transport, PACKET_TYPE_ACIP_WEBRTC_SDP, packet, total_len, 0);

  mutex_unlock(&g_signaling_mutex);

  SAFE_FREE(packet);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to send SDP via ACDS");
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Send ICE candidate via ACDS
 *
 * Constructs PACKET_TYPE_ACIP_WEBRTC_ICE and sends via ACDS transport.
 *
 * Packet format:
 * - Header: acip_webrtc_ice_t (50 bytes)
 * - Payload: candidate string (null-terminated) + mid string (null-terminated)
 */
static asciichat_error_t client_send_ice(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                         const char *candidate, const char *mid, void *user_data) {
  (void)user_data;

  if (!session_id || !recipient_id || !candidate || !mid) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ICE parameters");
  }

  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  // Check if ACDS connection is active
  if (!g_acds_transport) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "ACDS transport not available");
  }

  // Check if session context is set
  if (!g_session_context.is_set) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session context not set");
  }

  // Calculate payload length (candidate + null + mid + null)
  size_t candidate_len = strlen(candidate);
  size_t mid_len = strlen(mid);
  size_t payload_len = candidate_len + 1 + mid_len + 1;

  if (payload_len >= 8192) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_INVALID_PARAM, "ICE payload too large: %zu", payload_len);
  }

  // Allocate packet buffer (header + payload)
  size_t total_len = sizeof(acip_webrtc_ice_t) + payload_len;
  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    mutex_unlock(&g_signaling_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ICE packet");
  }

  // Fill header
  acip_webrtc_ice_t *header = (acip_webrtc_ice_t *)packet;
  memcpy(header->session_id, session_id, 16);
  memcpy(header->sender_id, g_session_context.participant_id, 16);
  memcpy(header->recipient_id, recipient_id, 16);
  header->candidate_len = HOST_TO_NET_U16((uint16_t)candidate_len); // FIXED: Use candidate length, not total payload

  // Copy candidate and mid after header
  uint8_t *payload = packet + sizeof(acip_webrtc_ice_t);
  memcpy(payload, candidate, candidate_len);
  payload[candidate_len] = '\0';
  memcpy(payload + candidate_len + 1, mid, mid_len);
  payload[candidate_len + 1 + mid_len] = '\0';

  log_debug("Sending WebRTC ICE candidate to participant (%.8s..., mid=%s) via ACDS", (const char *)recipient_id, mid);

  // Send via ACDS transport using generic packet sender
  asciichat_error_t result =
      packet_send_via_transport(g_acds_transport, PACKET_TYPE_ACIP_WEBRTC_ICE, packet, total_len, 0);

  mutex_unlock(&g_signaling_mutex);

  SAFE_FREE(packet);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to send ICE via ACDS");
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// Public API
// =============================================================================

webrtc_signaling_callbacks_t webrtc_get_signaling_callbacks(void) {
  ensure_mutex_initialized();

  webrtc_signaling_callbacks_t callbacks = {
      .send_sdp = client_send_sdp, .send_ice = client_send_ice, .user_data = NULL};

  return callbacks;
}

void webrtc_set_acds_transport(acip_transport_t *transport) {
  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  g_acds_transport = transport;

  if (transport) {
    log_debug("ACDS transport set for WebRTC signaling");
  } else {
    log_debug("ACDS transport cleared for WebRTC signaling");
  }

  mutex_unlock(&g_signaling_mutex);
}

void webrtc_set_session_context(const uint8_t session_id[16], const uint8_t participant_id[16]) {
  if (!session_id || !participant_id) {
    log_error("Invalid session context parameters");
    return;
  }

  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  memcpy(g_session_context.session_id, session_id, 16);
  memcpy(g_session_context.participant_id, participant_id, 16);
  g_session_context.is_set = true;

  log_info("Session context set for WebRTC signaling (session=%.8s..., participant=%.8s...)", (const char *)session_id,
           (const char *)participant_id);

  mutex_unlock(&g_signaling_mutex);
}

/**
 * @brief Set the WebRTC data channel transport
 *
 * Stores the active WebRTC transport that will be used for peer-to-peer communication.
 * This is different from the ACDS signaling transport - it's the actual data channel.
 *
 * @param transport WebRTC transport (NULL to clear)
 *
 * @note Called when WebRTC connection is established or when falling back to TCP
 * @note If NULL is passed, the transport is cleared (e.g., on disconnect)
 */
void webrtc_set_transport(acip_transport_t *transport) {
  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  g_webrtc_transport = transport;

  if (transport) {
    log_debug("WebRTC data channel transport set");
  } else {
    log_debug("WebRTC data channel transport cleared");
  }

  mutex_unlock(&g_signaling_mutex);
}

/**
 * @brief Get the current WebRTC data channel transport
 *
 * Retrieves the active WebRTC transport for peer-to-peer communication.
 *
 * @return Current WebRTC transport pointer (NULL if not set)
 *
 * @note Caller must not free the returned pointer
 * @note Pointer is valid only until webrtc_set_transport(NULL) is called
 */
acip_transport_t *webrtc_get_transport(void) {
  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  acip_transport_t *transport = g_webrtc_transport;

  mutex_unlock(&g_signaling_mutex);

  return transport;
}

/**
 * @brief Cleanup and release the WebRTC data channel transport
 *
 * Closes and releases the WebRTC transport. Called when disconnecting
 * or falling back to a different transport type.
 *
 * @note This is a convenience function that sets transport to NULL
 * @note Actual transport cleanup (closing sockets, freeing peer manager)
 *       should be done by the caller before calling this
 */
void webrtc_cleanup_transport(void) {
  ensure_mutex_initialized();
  mutex_lock(&g_signaling_mutex);

  if (g_webrtc_transport) {
    log_debug("Cleaning up WebRTC data channel transport");
    g_webrtc_transport = NULL;
  }

  mutex_unlock(&g_signaling_mutex);
}
