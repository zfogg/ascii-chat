/**
 * @file client/webrtc.h
 * @brief Client-side WebRTC signaling callback implementations
 *
 * Provides callback functions for sending SDP/ICE via ACDS when the peer
 * manager generates local descriptions and candidates. These callbacks bridge
 * the peer manager to the ACDS TCP connection.
 *
 * INTEGRATION POINTS:
 * ===================
 * - webrtc_peer_manager: Calls these callbacks when generating local SDP/ICE
 * - ACDS connection: These callbacks send ACIP packets via ACDS TCP transport
 * - client/main.c: Initializes peer manager with these callbacks
 *
 * LIFECYCLE:
 * ==========
 * 1. Client joins ACDS session (gets session_id, participant_id)
 * 2. Client initializes peer manager with these callbacks
 * 3. Peer manager generates local SDP/ICE
 * 4. Callbacks send SDP/ICE to ACDS server for relay to remote peers
 * 5. ACDS relays messages to target participants
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "networking/webrtc/peer_manager.h"
#include "networking/acip/transport.h"
#include "asciichat_errno.h"
#include <stdint.h>

/**
 * @brief Get signaling callbacks for WebRTC peer manager
 *
 * Returns a structure containing callback functions that send SDP/ICE
 * via the ACDS TCP connection. The callbacks are stateless - they use
 * the global ACDS transport to send signaling messages.
 *
 * @return Signaling callbacks structure (valid until program exit)
 *
 * @note The returned structure points to static callbacks, safe to use
 *       for the lifetime of the peer manager.
 * @note Callbacks will fail if ACDS connection is not active.
 */
webrtc_signaling_callbacks_t webrtc_get_signaling_callbacks(void);

/**
 * @brief Set the ACDS transport for signaling callbacks
 *
 * Configures the transport that will be used to send SDP/ICE messages.
 * Must be called before peer manager generates any local descriptions.
 *
 * @param transport ACDS TCP transport (NULL to clear)
 *
 * @note This is called automatically when connecting to ACDS.
 * @note Callbacks will fail with ERROR_INVALID_STATE if transport is NULL.
 */
void webrtc_set_acds_transport(acip_transport_t *transport);

/**
 * @brief Set session and participant IDs for signaling
 *
 * Configures the session context used when sending SDP/ICE messages.
 * Must be called after successful ACDS session join.
 *
 * @param session_id Session UUID (16 bytes, copied)
 * @param participant_id Local participant UUID (16 bytes, copied)
 *
 * @note This is called automatically when joining ACDS session.
 * @note Callbacks will fail with ERROR_INVALID_STATE if IDs are not set.
 */
void webrtc_set_session_context(const uint8_t session_id[16], const uint8_t participant_id[16]);
