/**
 * @file networking/webrtc/stun.h
 * @brief STUN (Session Traversal Utilities for NAT) Protocol Support
 * @ingroup webrtc
 * @addtogroup stun
 * @{
 *
 * This module defines STUN server configuration structures for WebRTC
 * connectivity. STUN servers help clients discover their public IP addresses
 * and port mappings when behind NAT.
 *
 * **RFC 5389**: Session Traversal Utilities for NAT (STUN)
 * **RFC 8489**: Session Traversal Utilities for NAT (STUN) - updated
 *
 * STUN SERVER ROLE:
 * =================
 * - Helps clients discover their public-facing IP address and port
 * - Essential for WebRTC peer-to-peer connection establishment
 * - No relay (unlike TURN) - only provides connectivity information
 *
 * USAGE IN ACDS:
 * ==============
 * - Discovery server provides STUN server list in SESSION_CREATED response
 * - Clients use STUN servers for ICE candidate gathering
 * - Multiple STUN servers can be provided for redundancy
 *
 * @note All structures are packed with __attribute__((packed)) for wire format
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pack network protocol structures tightly for wire format
#ifdef _WIN32
#pragma pack(push, 1)
#endif

/**
 * @brief STUN server configuration
 *
 * Used in ACDS SESSION_CREATED response to provide WebRTC connectivity information.
 *
 * **Protocol Format**:
 * - host_len: 1 byte (length of host string)
 * - host: 64 bytes (null-terminated STUN URL)
 *
 * **URL Format Examples**:
 * - `stun:discovery.ascii.chat:3478` (standard port)
 * - `stun:stun.l.google.com:19302` (Google public STUN)
 * - `stun:stun.example.com:5349` (custom port)
 *
 * **Wire Format** (65 bytes total):
 * @code
 * +----------+----------------------------------------+
 * | host_len |              host[64]                  |
 * +----------+----------------------------------------+
 * | 1 byte   |              64 bytes                  |
 * +----------+----------------------------------------+
 * @endcode
 *
 * @ingroup stun
 */
typedef struct __attribute__((packed)) {
  uint8_t host_len; ///< Length of host string (actual URL length)
  char host[64];    ///< STUN server URL (e.g., "stun:discovery.ascii.chat:3478")
} stun_server_t;

/**
 * @name STUN Protocol Constants
 * @{
 * @ingroup stun
 */

/** @brief Standard STUN port (RFC 5389) */
#define STUN_DEFAULT_PORT 3478

/** @brief STUN over TLS port (RFC 5389) */
#define STUN_TLS_DEFAULT_PORT 5349

/** @brief Maximum STUN URL length */
#define STUN_MAX_URL_LEN 64

/** @} */

#ifdef _WIN32
#pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif

/** @} */ /* stun */
