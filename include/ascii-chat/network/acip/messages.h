/**
 * @file network/acip/messages.h
 * @brief ACIP protocol message structures and helpers
 *
 * This header provides a centralized location for all ACIP message structures
 * used in packets 1-199. It includes packet structures from packet.h and adds
 * ACIP-specific helpers and type definitions.
 *
 * ACIP PACKET RANGES:
 * ===================
 * - Packets 1-35: Core ascii-chat protocol (video, audio, control)
 * - Packets 100-199: ACDS discovery service protocol
 *
 * DESIGN RATIONALE:
 * =================
 * Rather than duplicating structures, we include packet.h which already
 * defines all core protocol structures (ascii_frame_packet_t, etc.).
 * This header adds convenience helpers and ACIP-specific functionality.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "../../network/packet.h"
#include "../../network/acip/acds.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ACIP Packet Classification Helpers
// =============================================================================

/**
 * @brief Check if packet type is a media packet (audio/video)
 *
 * @param type Packet type from packet_type_t enum
 * @return true if packet contains media data
 */
static inline bool acip_is_media_packet(uint16_t type) {
  return (type == PACKET_TYPE_ASCII_FRAME || type == PACKET_TYPE_IMAGE_FRAME || type == PACKET_TYPE_AUDIO_BATCH ||
          type == PACKET_TYPE_AUDIO_OPUS_BATCH);
}

/**
 * @brief Check if packet type is a control packet
 *
 * @param type Packet type from packet_type_t enum
 * @return true if packet is control/signaling
 */
static inline bool acip_is_control_packet(uint16_t type) {
  return (type == PACKET_TYPE_CLIENT_JOIN || type == PACKET_TYPE_CLIENT_LEAVE || type == PACKET_TYPE_STREAM_START ||
          type == PACKET_TYPE_STREAM_STOP || type == PACKET_TYPE_PING || type == PACKET_TYPE_PONG ||
          type == PACKET_TYPE_CLEAR_CONSOLE || type == PACKET_TYPE_SERVER_STATE ||
          type == PACKET_TYPE_CLIENT_CAPABILITIES || type == PACKET_TYPE_PROTOCOL_VERSION);
}

/**
 * @brief Check if packet type is a crypto handshake packet
 *
 * @param type Packet type from packet_type_t enum
 * @return true if packet is part of crypto handshake
 */
static inline bool acip_is_crypto_packet(uint16_t type) {
  return packet_is_handshake_type(type);
}

/**
 * @brief Check if packet type is a message packet
 *
 * @param type Packet type from packet_type_t enum
 * @return true if packet contains text/error messages
 */
static inline bool acip_is_message_packet(uint16_t type) {
  return (type == PACKET_TYPE_SIZE_MESSAGE || type == PACKET_TYPE_AUDIO_MESSAGE || type == PACKET_TYPE_TEXT_MESSAGE ||
          type == PACKET_TYPE_ERROR_MESSAGE || type == PACKET_TYPE_REMOTE_LOG);
}

// =============================================================================
// Packet Size Helpers
// =============================================================================

/**
 * @brief Get minimum valid size for a packet type
 *
 * Returns the minimum payload size expected for each packet type.
 * Used for validation to reject malformed packets.
 *
 * @param type Packet type
 * @return Minimum size in bytes, or 0 if variable/unknown
 */
static inline size_t acip_get_min_packet_size(uint16_t type) {
  switch (type) {
  case PACKET_TYPE_PROTOCOL_VERSION:
    return sizeof(protocol_version_packet_t);
  case PACKET_TYPE_ASCII_FRAME:
    return sizeof(ascii_frame_packet_t);
  case PACKET_TYPE_IMAGE_FRAME:
    return sizeof(image_frame_packet_t);
  case PACKET_TYPE_AUDIO_BATCH:
    return sizeof(audio_batch_packet_t);
  case PACKET_TYPE_SERVER_STATE:
    return sizeof(server_state_packet_t);
  case PACKET_TYPE_ERROR_MESSAGE:
    return sizeof(error_packet_t);
  case PACKET_TYPE_REMOTE_LOG:
    return sizeof(remote_log_packet_t);
  case PACKET_TYPE_PING:
  case PACKET_TYPE_PONG:
    return 0; // No payload
  case PACKET_TYPE_ACIP_SESSION_CREATE:
    return sizeof(acip_session_create_t);
  case PACKET_TYPE_ACIP_SESSION_CREATED:
    return sizeof(acip_session_created_t);
  case PACKET_TYPE_ACIP_SESSION_LOOKUP:
    return sizeof(acip_session_lookup_t);
  case PACKET_TYPE_ACIP_SESSION_INFO:
    return sizeof(acip_session_info_t);
  case PACKET_TYPE_ACIP_SESSION_JOIN:
    return sizeof(acip_session_join_t);
  case PACKET_TYPE_ACIP_SESSION_JOINED:
    return sizeof(acip_session_joined_t);
  case PACKET_TYPE_ACIP_WEBRTC_SDP:
    return sizeof(acip_webrtc_sdp_t);
  case PACKET_TYPE_ACIP_WEBRTC_ICE:
    return sizeof(acip_webrtc_ice_t);
  case PACKET_TYPE_ACIP_ERROR:
    return sizeof(acip_error_t);
  default:
    return 0; // Variable or unknown size
  }
}

// =============================================================================
// Message Type Strings (for logging/debugging)
// =============================================================================

/**
 * @brief Get human-readable name for packet type
 *
 * @param type Packet type from packet_type_t enum
 * @return String name or "UNKNOWN"
 */
const char *acip_packet_type_name(uint16_t type);
