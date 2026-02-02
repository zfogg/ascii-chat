/**
 * @file network/webrtc/ice.h
 * @brief ICE (Interactive Connectivity Establishment) for WebRTC
 * @ingroup webrtc
 *
 * Handles ICE candidate gathering, exchange, and connectivity checking.
 * ICE is the mechanism that allows WebRTC peers to discover and connect
 * across NATs and firewalls using STUN and TURN servers.
 *
 * ## ICE Candidate Types
 *
 * **Host candidates**: Local IP addresses
 * - Direct connection if both peers on same network
 *
 * **Server-reflexive (srflx) candidates**: NAT-discovered addresses
 * - Obtained from STUN server
 * - Allows hole punching across NATs
 *
 * **Peer-reflexive (prflx) candidates**: Discovered during connectivity checks
 * - May emerge when direct STUN-based connection works
 *
 * **Relay candidates**: TURN relay server addresses
 * - Guaranteed to work through any firewall
 * - Last resort for restrictive networks
 *
 * ## ICE Candidate Exchange
 *
 * 1. **Gathering phase**: Collect candidates from all sources
 *    - Host candidates (network interfaces)
 *    - STUN server response
 *    - TURN relay addresses
 *
 * 2. **Signaling phase**: Send candidates to peer via ACDS relay
 *    - Each candidate sent as ACIP_WEBRTC_ICE packet
 *    - Order: host first, then srflx, then relay
 *
 * 3. **Connectivity checking**: Verify which candidates work
 *    - WebRTC agent tests each candidate pair
 *    - Selects best pair (lowest latency, highest bandwidth)
 *
 * 4. **Connection**: Data flows through selected candidate pair
 *    - May change during session (ICE restart)
 *
 * ## Integration with ACDS
 *
 * ICE candidates are sent via ACDS signaling relay:
 * - PACKET_TYPE_ACIP_WEBRTC_ICE for each candidate
 * - Peer decodes and adds to peer connection
 * - Peer agent tests connectivity with received candidates
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ICE Candidate Types
 * ============================================================================ */

/**
 * @brief ICE candidate type enumeration (RFC 5245)
 */
typedef enum {
  ICE_CANDIDATE_HOST = 0,  ///< Host candidate (local IP address)
  ICE_CANDIDATE_SRFLX = 1, ///< Server-reflexive (NAT-discovered via STUN)
  ICE_CANDIDATE_PRFLX = 2, ///< Peer-reflexive (discovered during checks)
  ICE_CANDIDATE_RELAY = 3, ///< Relay candidate (TURN server)
} ice_candidate_type_t;

/**
 * @brief ICE candidate transport protocol
 */
typedef enum {
  ICE_PROTOCOL_UDP = 0,
  ICE_PROTOCOL_TCP = 1,
} ice_protocol_t;

/**
 * @brief ICE candidate TCP type (if applicable)
 */
typedef enum {
  ICE_TCP_TYPE_ACTIVE = 0,  ///< Actively opens connection
  ICE_TCP_TYPE_PASSIVE = 1, ///< Passively waits for connection
  ICE_TCP_TYPE_SO = 2,      ///< Simultaneous open
} ice_tcp_type_t;

/**
 * @brief Single ICE candidate for connectivity
 *
 * Represents one possible address/port combination for connection.
 * The WebRTC agent will test all candidate pairs to find working path.
 *
 * ## String Format (attribute line)
 * ```
 * a=candidate:foundation 1 udp priority 192.168.1.100 12345 typ host
 * a=candidate:foundation 2 udp priority 203.0.113.45 54321 typ srflx raddr 192.168.1.100 rport 12345
 * a=candidate:foundation 3 udp priority 198.51.100.7 3478 typ relay raddr 203.0.113.45 rport 54321
 * ```
 */
typedef struct {
  /* Core candidate info */
  char foundation[32];     ///< Unique identifier for candidate (for pairing)
  uint32_t component_id;   ///< Component (1=RTP, 2=RTCP; usually 1)
  ice_protocol_t protocol; ///< UDP or TCP
  uint32_t priority;       ///< Candidate priority (used for preference)

  /* Address info */
  char ip_address[64]; ///< IP address (IPv4 or IPv6)
  uint16_t port;       ///< Port number

  /* Candidate type and attributes */
  ice_candidate_type_t type; ///< host, srflx, prflx, or relay

  /* For srflx/prflx candidates: original address before NAT */
  char raddr[64]; ///< Related address (for srflx/prflx)
  uint16_t rport; ///< Related port

  /* For TCP candidates */
  ice_tcp_type_t tcp_type; ///< active, passive, so

  /* Extensions */
  char extensions[256]; ///< Additional extensions (e.g., "tcptype passive")
} ice_candidate_t;

/* ============================================================================
 * ICE Gathering and Exchange
 * ============================================================================ */

/**
 * @brief Callback for sending ICE candidate to peer
 *
 * Called by ICE agent when a new candidate is discovered.
 * Implementation should send via ACDS relay.
 *
 * @param candidate Discovered candidate
 * @param mid Media stream ID (e.g., "0" for audio, "1" for video)
 * @param user_data User context pointer
 * @return ASCIICHAT_OK on success, error code on failure
 */
typedef asciichat_error_t (*ice_send_candidate_callback_t)(const ice_candidate_t *candidate, const char *mid,
                                                           void *user_data);

/**
 * @brief ICE gathering configuration
 */
typedef struct {
  const char *ufrag;                           ///< Username fragment for ICE (from offer)
  const char *pwd;                             ///< Password for ICE (from offer)
  ice_send_candidate_callback_t send_callback; ///< Called for each gathered candidate
  void *user_data;                             ///< Passed to callback
} ice_config_t;

/**
 * @brief Start ICE candidate gathering
 *
 * Initiates gathering of candidates from all sources:
 * - Host candidates (network interfaces)
 * - STUN server (server-reflexive)
 * - TURN relay
 *
 * Candidates are reported via callback as they're discovered.
 *
 * @param config ICE gathering configuration
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ice_gather_candidates(const ice_config_t *config);

/* ============================================================================
 * ICE Candidate Parsing and Formatting
 * ============================================================================ */

/**
 * @brief Parse ICE candidate from attribute string
 *
 * Converts SDP candidate attribute line to ice_candidate_t structure.
 *
 * Format: a=candidate:foundation component protocol priority ip port typ type [raddr rport] [extensions]
 *
 * @param line SDP candidate line (without "a=candidate:" prefix)
 * @param candidate Parsed candidate (output)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ice_parse_candidate(const char *line, ice_candidate_t *candidate);

/**
 * @brief Format ICE candidate to attribute string
 *
 * Converts ice_candidate_t structure to SDP attribute line.
 *
 * @param candidate Candidate to format
 * @param line Output buffer for SDP line (without "a=candidate:" prefix)
 * @param line_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ice_format_candidate(const ice_candidate_t *candidate, char *line, size_t line_size);

/**
 * @brief Calculate candidate priority
 *
 * Implements RFC 5245 priority formula:
 * priority = (2^24 * typePreference) + (2^8 * localPreference) + (256 - componentID)
 *
 * @param type Candidate type (determines typePreference)
 * @param local_preference Local preference (0-65535, higher = preferred)
 * @param component_id Component ID (1 or 2)
 * @return Calculated priority
 */
uint32_t ice_calculate_priority(ice_candidate_type_t type, uint16_t local_preference, uint8_t component_id);

/* ============================================================================
 * ICE Connectivity
 * ============================================================================ */

/* Forward declaration */
struct webrtc_peer_connection;

/**
 * @brief Add remote candidate to peer connection
 *
 * Called when receiving candidate from peer.
 * The WebRTC agent will test connectivity with this candidate.
 *
 * @param pc Peer connection to add candidate to
 * @param candidate Remote candidate from peer
 * @param mid Media stream ID
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ice_add_remote_candidate(struct webrtc_peer_connection *pc, const ice_candidate_t *candidate,
                                           const char *mid);

/**
 * @brief Check ICE connection state
 *
 * @param pc Peer connection to check
 * @return true if connected, false otherwise
 */
bool ice_is_connected(struct webrtc_peer_connection *pc);

/**
 * @brief Get the internal libdatachannel peer connection ID
 *
 * Helper function for C++ code that needs access to internal rtc_id
 * without exposing the full structure definition.
 *
 * @param pc Peer connection to query
 * @return libdatachannel peer connection ID, or -1 if pc is NULL
 */
int webrtc_get_rtc_id(struct webrtc_peer_connection *pc);

/**
 * @brief Get selected candidate pair
 *
 * Returns the local and remote candidates that were selected for data flow.
 * Uses libdatachannel's rtcGetSelectedCandidatePair() API via C++ bindings.
 *
 * @param pc Peer connection to query
 * @param local_candidate Selected local candidate (output, may be NULL)
 * @param remote_candidate Selected remote candidate (output, may be NULL)
 * @return ASCIICHAT_OK if pair selected, ERROR_INVALID_STATE if no pair selected yet
 */
asciichat_error_t ice_get_selected_pair(struct webrtc_peer_connection *pc, ice_candidate_t *local_candidate,
                                        ice_candidate_t *remote_candidate);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get human-readable candidate type name
 * @param type Candidate type
 * @return Static string (e.g., "host", "srflx", "relay")
 */
const char *ice_candidate_type_name(ice_candidate_type_t type);

/**
 * @brief Get human-readable protocol name
 * @param protocol Protocol type
 * @return Static string (e.g., "udp", "tcp")
 */
const char *ice_protocol_name(ice_protocol_t protocol);

#ifdef __cplusplus
}
#endif
