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

/**
 * @brief Parse comma-separated STUN server URLs into stun_server_t array
 *
 * Parses a comma-separated string of STUN server URLs (e.g., "stun:server1:3478,stun:server2:3478")
 * into an array of stun_server_t structs. If the input string is empty or NULL, uses the
 * default endpoints from the options system.
 *
 * **Example Input Formats**:
 * - Empty: "" (will use defaults from OPT_STUN_SERVERS_DEFAULT)
 * - Single: "stun:stun.example.com:3478"
 * - Multiple: "stun:server1:3478,stun:server2:19302,stun:server3:5349"
 *
 * @param csv_servers Comma-separated STUN server URLs (can be empty)
 * @param default_csv Default servers to use if csv_servers is empty
 * @param out_servers Output array of stun_server_t structs (must be pre-allocated)
 * @param max_count Maximum number of servers to parse
 * @return Number of servers parsed (0-max_count), or -1 on error
 *
 * @note Each server URL is limited to STUN_MAX_URL_LEN (64) characters
 * @note Function gracefully stops at max_count or if a server exceeds max length
 *
 * @ingroup stun
 */
int stun_servers_parse(const char *csv_servers, const char *default_csv, stun_server_t *out_servers, int max_count);

#ifdef _WIN32
#pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif

/** @} */ /* stun */
