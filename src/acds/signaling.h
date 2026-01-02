#pragma once

/**
 * @file acds/signaling.h
 * @brief 🎬 WebRTC SDP/ICE signaling relay
 *
 * Relays SDP offers/answers and ICE candidates between session participants.
 * The discovery server acts as a pure relay - no media processing.
 */

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "acds/session.h"
#include "networking/acip/acds.h"
#include "networking/tcp/server.h"

/**
 * @brief Relay SDP offer/answer to recipient
 *
 * Relays SDP packet to specific participant or broadcasts to all participants
 * in the session if recipient_id is all zeros.
 *
 * @param registry Session registry
 * @param tcp_server TCP server with client registry
 * @param sdp SDP packet to relay (with variable-length SDP data following struct)
 * @param total_packet_len Total packet length including variable data
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t signaling_relay_sdp(session_registry_t *registry, tcp_server_t *tcp_server,
                                      const acip_webrtc_sdp_t *sdp, size_t total_packet_len);

/**
 * @brief Relay ICE candidate to recipient
 *
 * Relays ICE packet to specific participant or broadcasts to all participants
 * in the session if recipient_id is all zeros.
 *
 * @param registry Session registry
 * @param tcp_server TCP server with client registry
 * @param ice ICE packet to relay (with variable-length candidate data following struct)
 * @param total_packet_len Total packet length including variable data
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t signaling_relay_ice(session_registry_t *registry, tcp_server_t *tcp_server,
                                      const acip_webrtc_ice_t *ice, size_t total_packet_len);

/**
 * @brief Broadcast packet to all session participants
 *
 * Sends packet to all participants in a session. Used internally by
 * SDP/ICE relay when recipient_id is all zeros.
 *
 * @param registry Session registry
 * @param tcp_server TCP server with client registry
 * @param session_id Session UUID
 * @param packet_type Packet type to send
 * @param packet Packet data to broadcast
 * @param packet_len Packet length
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t signaling_broadcast(session_registry_t *registry, tcp_server_t *tcp_server,
                                      const uint8_t session_id[16], packet_type_t packet_type, const void *packet,
                                      size_t packet_len);
