/**
 * @file networking/webrtc/peer_manager.h
 * @brief WebRTC peer connection manager for session participants
 * @ingroup webrtc
 *
 * Manages WebRTC peer connections for ascii-chat session participants.
 * Handles SDP/ICE exchange, peer connection lifecycle, and integration
 * with ACIP transport layer.
 *
 * ## Role-Based Connection Management
 *
 * - **Session Creator (Server Role)**: Accepts offers, generates answers
 * - **Session Joiner (Client Role)**: Generates offers, receives answers
 *
 * ## Integration with ACDS Signaling
 *
 * - SDP/ICE messages arrive via ACDS relay (`PACKET_TYPE_ACIP_WEBRTC_*`)
 * - Manager creates peer connections and exchanges signaling data
 * - When DataChannel opens, wraps it in ACIP transport for packet handling
 *
 * ## Thread Safety
 *
 * All functions are thread-safe. The manager uses internal locking to
 * protect peer connection state during concurrent signaling operations.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../common.h"
#include "../../network/webrtc/webrtc.h"
#include "../../network/acip/transport.h"
#include "../../network/acip/messages.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup webrtc_peer_manager WebRTC Peer Manager
 * @brief Manage WebRTC peer connections for session participants
 * @{
 */

// Forward declarations
typedef struct webrtc_peer_manager webrtc_peer_manager_t;

/**
 * @brief Peer connection role (server or client)
 */
typedef enum {
  WEBRTC_ROLE_CREATOR = 0, ///< Session creator - accepts offers, generates answers
  WEBRTC_ROLE_JOINER = 1,  ///< Session joiner - generates offers, receives answers
} webrtc_peer_role_t;

/**
 * @brief Callback when DataChannel is ready and wrapped in ACIP transport
 * @param transport ACIP transport wrapping the WebRTC DataChannel
 * @param participant_id Remote participant UUID (16 bytes)
 * @param user_data User context pointer
 *
 * The callback receives ownership of the transport and must either:
 * - Use it for ACIP communication
 * - Call acip_transport_destroy() to free it
 */
typedef void (*webrtc_transport_ready_callback_t)(acip_transport_t *transport, const uint8_t participant_id[16],
                                                  void *user_data);

/**
 * @brief Callback when ICE gathering times out for a peer
 * @param participant_id Remote participant UUID (16 bytes)
 * @param timeout_ms Configured timeout in milliseconds
 * @param elapsed_ms Actual elapsed time in milliseconds
 * @param user_data User context pointer
 *
 * Called when a peer connection's ICE gathering exceeds the configured timeout.
 * The peer connection will be closed after this callback returns.
 */
typedef void (*webrtc_gathering_timeout_callback_t)(const uint8_t participant_id[16], uint32_t timeout_ms,
                                                    uint64_t elapsed_ms, void *user_data);

/**
 * @brief Peer manager configuration
 */
typedef struct {
  webrtc_peer_role_t role;                                  ///< Session role (creator or joiner)
  stun_server_t *stun_servers;                              ///< STUN servers for ICE
  size_t stun_count;                                        ///< Number of STUN servers
  turn_server_t *turn_servers;                              ///< TURN servers for relay
  size_t turn_count;                                        ///< Number of TURN servers
  webrtc_transport_ready_callback_t on_transport_ready;     ///< Called when DataChannel ready
  webrtc_gathering_timeout_callback_t on_gathering_timeout; ///< Called when ICE gathering times out
  void *user_data;                                          ///< Passed to callbacks
  crypto_context_t *crypto_ctx;                             ///< Crypto context for transports
} webrtc_peer_manager_config_t;

/**
 * @brief Callback to send SDP via ACDS signaling
 * @param session_id Session UUID (16 bytes)
 * @param recipient_id Recipient participant UUID (16 bytes, all zeros for broadcast)
 * @param sdp_type SDP type ("offer" or "answer")
 * @param sdp SDP string (null-terminated)
 * @param user_data User context pointer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Implementation should send PACKET_TYPE_ACIP_WEBRTC_SDP via ACDS.
 */
typedef asciichat_error_t (*webrtc_send_sdp_callback_t)(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                                        const char *sdp_type, const char *sdp, void *user_data);

/**
 * @brief Callback to send ICE candidate via ACDS signaling
 * @param session_id Session UUID (16 bytes)
 * @param recipient_id Recipient participant UUID (16 bytes, all zeros for broadcast)
 * @param candidate ICE candidate string (null-terminated)
 * @param mid Media stream ID (null-terminated)
 * @param user_data User context pointer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Implementation should send PACKET_TYPE_ACIP_WEBRTC_ICE via ACDS.
 */
typedef asciichat_error_t (*webrtc_send_ice_callback_t)(const uint8_t session_id[16], const uint8_t recipient_id[16],
                                                        const char *candidate, const char *mid, void *user_data);

/**
 * @brief Signaling callbacks for sending SDP/ICE
 */
typedef struct {
  webrtc_send_sdp_callback_t send_sdp; ///< Send SDP via ACDS
  webrtc_send_ice_callback_t send_ice; ///< Send ICE via ACDS
  void *user_data;                     ///< Passed to callbacks
} webrtc_signaling_callbacks_t;

// ============================================================================
// Peer Manager Lifecycle
// ============================================================================

/**
 * @brief Create a WebRTC peer manager
 * @param config Manager configuration
 * @param signaling_callbacks Callbacks for sending SDP/ICE
 * @param manager_out Output parameter for manager handle
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Creates a manager for WebRTC peer connections. The manager handles:
 * - Peer connection creation and lifecycle
 * - SDP offer/answer generation and processing
 * - ICE candidate exchange
 * - ACIP transport wrapping when DataChannel opens
 */
asciichat_error_t webrtc_peer_manager_create(const webrtc_peer_manager_config_t *config,
                                             const webrtc_signaling_callbacks_t *signaling_callbacks,
                                             webrtc_peer_manager_t **manager_out);

/**
 * @brief Destroy peer manager and close all connections
 * @param manager Peer manager to destroy
 *
 * Closes all active peer connections gracefully and frees resources.
 * Safe to call with NULL pointer.
 */
void webrtc_peer_manager_destroy(webrtc_peer_manager_t *manager);

// ============================================================================
// Signaling Message Processing
// ============================================================================

/**
 * @brief Handle incoming SDP message from ACDS
 * @param manager Peer manager
 * @param sdp SDP message received from ACDS
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Processes SDP offer or answer from remote peer:
 * - **Creator role + offer**: Create peer connection, set remote SDP, generate answer
 * - **Joiner role + answer**: Set remote SDP on existing peer connection
 *
 * Sends response SDP via signaling callbacks.
 */
asciichat_error_t webrtc_peer_manager_handle_sdp(webrtc_peer_manager_t *manager, const acip_webrtc_sdp_t *sdp);

/**
 * @brief Handle incoming ICE candidate from ACDS
 * @param manager Peer manager
 * @param ice ICE candidate message received from ACDS
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Adds remote ICE candidate to the appropriate peer connection.
 * If peer connection doesn't exist yet, queues candidate for later.
 */
asciichat_error_t webrtc_peer_manager_handle_ice(webrtc_peer_manager_t *manager, const acip_webrtc_ice_t *ice);

// ============================================================================
// Connection Initiation (Joiner Role)
// ============================================================================

/**
 * @brief Initiate connection to remote peer (joiner role only)
 * @param manager Peer manager
 * @param session_id Session UUID (16 bytes)
 * @param participant_id Remote participant UUID to connect to (16 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Creates peer connection, generates SDP offer, and sends via signaling.
 * Only valid for WEBRTC_ROLE_JOINER (session joiners initiate connections).
 *
 * Creator role uses webrtc_peer_manager_handle_sdp() to accept offers instead.
 */
asciichat_error_t webrtc_peer_manager_connect(webrtc_peer_manager_t *manager, const uint8_t session_id[16],
                                              const uint8_t participant_id[16]);

// ============================================================================
// Connection Health Monitoring
// ============================================================================

/**
 * @brief Check all peer connections for ICE gathering timeouts
 * @param manager Peer manager
 * @param timeout_ms Timeout threshold in milliseconds
 * @return Number of peer connections that timed out and were closed
 *
 * Iterates through all active peer connections and checks if ICE gathering
 * has exceeded the specified timeout. For each timed-out connection:
 * - Calls on_gathering_timeout callback (if configured)
 * - Closes the peer connection
 * - Removes it from the manager
 *
 * This should be called periodically (e.g., every 100ms) during connection
 * establishment to detect and handle gathering failures.
 *
 * @note Thread-safe - uses internal locking
 */
int webrtc_peer_manager_check_gathering_timeouts(webrtc_peer_manager_t *manager, uint32_t timeout_ms);

/** @} */ // end of webrtc_peer_manager group

#ifdef __cplusplus
}
#endif
