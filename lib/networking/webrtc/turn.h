/**
 * @file networking/webrtc/turn.h
 * @brief TURN (Traversal Using Relays around NAT) Protocol Support
 * @ingroup webrtc
 * @addtogroup turn
 * @{
 *
 * This module defines TURN server configuration structures for WebRTC
 * connectivity. TURN servers act as relay servers when direct peer-to-peer
 * connections are not possible due to restrictive NAT or firewalls.
 *
 * **RFC 5766**: Traversal Using Relays around NAT (TURN)
 * **RFC 8656**: TURN Extensions for IPv6
 *
 * TURN SERVER ROLE:
 * =================
 * - Relays media traffic when direct P2P connection fails
 * - Fallback mechanism when STUN-based connectivity doesn't work
 * - Requires authentication (username/credential)
 * - Uses server bandwidth (unlike STUN which is free)
 *
 * USAGE IN ACDS:
 * ==============
 * - Discovery server provides TURN server list in SESSION_CREATED response
 * - Credentials are time-limited for security
 * - Clients use TURN as last resort for ICE connectivity
 *
 * SECURITY CONSIDERATIONS:
 * ========================
 * - Credentials should be time-limited (ephemeral)
 * - TURN servers should enforce rate limiting
 * - Use TURN over TLS (TURNS) when possible for privacy
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
 * @brief TURN server configuration with credentials
 *
 * Used in ACDS SESSION_CREATED response to provide WebRTC relay information.
 * Includes time-limited credentials for secure TURN access.
 *
 * **Protocol Format**:
 * - url_len: 1 byte (length of URL string)
 * - url: 64 bytes (null-terminated TURN URL)
 * - username_len: 1 byte (length of username)
 * - username: 32 bytes (null-terminated username)
 * - credential_len: 1 byte (length of credential)
 * - credential: 64 bytes (null-terminated time-limited credential)
 *
 * **URL Format Examples**:
 * - `turn:discovery.ascii.chat:3478` (standard TURN)
 * - `turns:discovery.ascii.chat:5349` (TURN over TLS)
 * - `turn:relay.example.com:3478?transport=udp` (UDP transport)
 * - `turn:relay.example.com:3478?transport=tcp` (TCP transport)
 *
 * **Credential Types**:
 * - **Short-term**: Simple username/password (less secure)
 * - **Long-term**: HMAC-based with timestamp (recommended)
 * - **OAuth**: Token-based authentication
 *
 * **Wire Format** (163 bytes total):
 * @code
 * +---------+----------------+---------------+----------------+--------------+
 * | url_len |    url[64]     | username_len  |  username[32]  |credential_len|
 * +---------+----------------+---------------+----------------+--------------+
 * | 1 byte  |   64 bytes     |    1 byte     |   32 bytes     |   1 byte     |
 * +---------+----------------+---------------+----------------+--------------+
 * |                  credential[64]                                          |
 * +--------------------------------------------------------------------------+
 * |                      64 bytes                                            |
 * +--------------------------------------------------------------------------+
 * @endcode
 *
 * @ingroup turn
 */
typedef struct __attribute__((packed)) {
  uint8_t url_len;        ///< Length of URL string (actual length)
  char url[64];           ///< TURN server URL (e.g., "turn:discovery.ascii.chat:3478")
  uint8_t username_len;   ///< Length of username string
  char username[32];      ///< TURN authentication username
  uint8_t credential_len; ///< Length of credential string
  char credential[64];    ///< Time-limited TURN credential/password
} turn_server_t;

/**
 * @name TURN Protocol Constants
 * @{
 * @ingroup turn
 */

/** @brief Standard TURN port (RFC 5766) */
#define TURN_DEFAULT_PORT 3478

/** @brief TURN over TLS port (RFC 5766) */
#define TURN_TLS_DEFAULT_PORT 5349

/** @brief Maximum TURN URL length */
#define TURN_MAX_URL_LEN 64

/** @brief Maximum TURN username length */
#define TURN_MAX_USERNAME_LEN 32

/** @brief Maximum TURN credential length */
#define TURN_MAX_CREDENTIAL_LEN 64

/** @brief Recommended credential expiration time (seconds) */
#define TURN_CREDENTIAL_EXPIRY_SECS (24 * 60 * 60) // 24 hours

/** @} */

#ifdef _WIN32
#pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif

/** @} */ /* turn */
