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
#include "acds/protocol.h"

/**
 * @brief Relay SDP offer/answer to recipient
 *
 * @param registry Session registry
 * @param sdp SDP packet to relay
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t signaling_relay_sdp(session_registry_t *registry, const acip_webrtc_sdp_t *sdp);

/**
 * @brief Relay ICE candidate to recipient
 *
 * @param registry Session registry
 * @param ice ICE packet to relay
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t signaling_relay_ice(session_registry_t *registry, const acip_webrtc_ice_t *ice);

/**
 * @brief Broadcast packet to all session participants
 *
 * @param registry Session registry
 * @param session_id Session UUID
 * @param packet Packet data to broadcast
 * @param packet_len Packet length
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t signaling_broadcast(session_registry_t *registry, const uint8_t session_id[16], const void *packet,
                                      size_t packet_len);
