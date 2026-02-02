/**
 * @file discovery/webrtc.h
 * @brief WebRTC P2P signaling for discovery mode failover
 * @ingroup discovery
 *
 * Provides WebRTC peer connection management for discovery mode failover scenarios.
 * When a host dies and participants need to connect to the pre-elected future host,
 * they establish P2P connections using WebRTC DataChannels.
 *
 * **Key Difference from Client Mode**:
 * - Client mode: SDP/ICE exchanged through ACDS relay (client connected to host for media)
 * - Discovery failover: SDP/ICE exchanged directly over TCP to new host (P2P connection)
 * - Both use the same WebRTC library (libdatachannel) but with different signaling paths
 *
 * ## Architecture
 *
 * During migration:
 * 1. Participant detects host disconnect
 * 2. Participant knows new host's address from pre-election (FUTURE_HOST_ELECTED)
 * 3. Participant connects TCP to new host
 * 4. Participant sends SDP OFFER directly over TCP (not through ACDS)
 * 5. New host sends SDP ANSWER directly over TCP
 * 6. Both exchange ICE candidates directly
 * 7. WebRTC P2P connection established
 * 8. Media flows peer-to-peer (or through TURN)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/network/acip/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create signaling callbacks for direct peer-to-peer connection during failover
 *
 * Returns callbacks that send SDP/ICE directly over TCP to the peer (not through ACDS).
 * Used when participants reconnect to a new host after migration.
 *
 * @param tcp_transport TCP transport connected to the new host
 * @param session_id Session UUID (16 bytes, copied)
 * @param participant_id Local participant UUID (16 bytes, copied)
 * @return Signaling callbacks for use with peer manager
 *
 * @note The returned structure points to static callbacks and is valid for program lifetime.
 * @note TCP transport must be connected and valid when callbacks are invoked.
 * @note Set tcp_transport to NULL to disable the callbacks.
 */
webrtc_signaling_callbacks_t discovery_webrtc_get_direct_signaling_callbacks(acip_transport_t *tcp_transport,
                                                                             const uint8_t session_id[16],
                                                                             const uint8_t participant_id[16]);

/**
 * @brief Set the TCP transport for direct peer-to-peer signaling
 *
 * Configures the transport that will be used to send SDP/ICE messages directly
 * to the peer (bypassing ACDS). Must be called before peer manager generates
 * any local descriptions.
 *
 * @param transport TCP transport to new host (NULL to disable)
 *
 * @note This is separate from the ACDS transport used in client mode.
 * @note Callbacks will fail with ERROR_INVALID_STATE if transport is NULL.
 */
void discovery_webrtc_set_tcp_transport(acip_transport_t *transport);

/**
 * @brief Set session and participant IDs for direct signaling
 *
 * Configures the session context used when sending SDP/ICE messages directly.
 * Must be called before peer manager generates any local descriptions.
 *
 * @param session_id Session UUID (16 bytes, copied)
 * @param participant_id Local participant UUID (16 bytes, copied)
 *
 * @note Callbacks will fail with ERROR_INVALID_STATE if IDs are not set.
 */
void discovery_webrtc_set_session_context(const uint8_t session_id[16], const uint8_t participant_id[16]);

/**
 * @brief Cleanup and release the direct peer-to-peer transport
 *
 * Clears the TCP transport used for direct signaling. Called on migration completion
 * or when the P2P connection is torn down.
 *
 * @note Actual transport cleanup (closing connections, freeing resources)
 *       should be done by caller before calling this.
 * @note Thread-safe.
 */
void discovery_webrtc_cleanup_transport(void);

#ifdef __cplusplus
}
#endif
