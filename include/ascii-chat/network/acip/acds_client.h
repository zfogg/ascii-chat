#pragma once

/**
 * @file network/acip/client.h
 * @brief ACIP client-side protocol library
 * @ingroup acip
 *
 * Client-side ACIP (ascii-chat IP Protocol) implementation for:
 * - Session discovery and management (create, lookup, join, leave)
 * - WebRTC signaling relay (SDP, ICE candidates)
 * - String reservation (future feature)
 *
 * **ACIP Protocol Overview:**
 * - Binary TCP protocol (not HTTP/JSON)
 * - Packet-based with CRC32 validation
 * - Ed25519 identity signatures
 * - Optional password protection
 *
 * **Primary Use Case:**
 * Connecting to ACDS (ascii-chat Discovery Service) servers for
 * session discovery and WebRTC peer coordination.
 *
 * **Integration:**
 * This library is part of libasciichat and can be used by any
 * application needing ACIP/ACDS integration.
 *
 * @see network/acip/acds.h for ACDS message structures
 * @see network/acip/protocol.h for ACIP packet types
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../asciichat_errno.h"
#include "../../platform/socket.h"
#include "../../network/acip/acds.h"

// ============================================================================
// ACDS Client Configuration
// ============================================================================

// Forward declaration of callback type from parallel_connect.h
typedef bool (*parallel_connect_should_exit_fn)(void *user_data);

/**
 * @brief ACDS client connection configuration
 */
typedef struct {
  char server_address[256]; ///< ACDS server address (e.g., "discovery.ascii.chat" or "127.0.0.1")
  uint16_t server_port;     ///< ACDS server port (default: 27225)
  uint32_t timeout_ms;      ///< Connection timeout in milliseconds

  // Optional: callback to check if connection should be abandoned (e.g., shutdown signal)
  parallel_connect_should_exit_fn should_exit_callback;
  void *callback_data;
} acds_client_config_t;

/**
 * @brief ACDS client connection handle
 */
typedef struct {
  acds_client_config_t config;
  socket_t socket; ///< TCP socket to ACDS server
  bool connected;  ///< Connection status
} acds_client_t;

// ============================================================================
// Connection Management
// ============================================================================

/**
 * @brief Connect to ACDS server
 *
 * Establishes TCP connection to the discovery server.
 *
 * @param client Client handle (output)
 * @param config Connection configuration
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acds_client_connect(acds_client_t *client, const acds_client_config_t *config);

/**
 * @brief Disconnect from ACDS server
 *
 * Closes the connection and cleans up resources.
 *
 * @param client Client handle
 */
void acds_client_disconnect(acds_client_t *client);

// ============================================================================
// Session Management
// ============================================================================

/**
 * @brief Session creation request parameters
 */
typedef struct {
  uint8_t identity_pubkey[32]; ///< Ed25519 public key (host identity)
  uint8_t identity_seckey[64]; ///< Ed25519 secret key (for signing)
  uint8_t capabilities;        ///< Bit 0: video, Bit 1: audio
  uint8_t max_participants;    ///< Maximum participants (1-8)
  bool has_password;           ///< Password protection enabled
  char password[128];          ///< Optional password (if has_password)
  bool acds_expose_ip;         ///< Explicitly allow public IP disclosure (--acds-expose-ip opt-in)
  uint8_t session_type;        ///< acds_session_type_t: 0=DIRECT_TCP (default), 1=WEBRTC
  const char *reserved_string; ///< Optional reserved string (NULL = auto-generate)
  char server_address[64];     ///< Server address where clients should connect
  uint16_t server_port;        ///< Server port where clients should connect
} acds_session_create_params_t;

/**
 * @brief Session creation result
 */
typedef struct {
  char session_string[49]; ///< Generated session string (null-terminated)
  uint8_t session_id[16];  ///< Session UUID
  uint64_t expires_at;     ///< Expiration timestamp (Unix ms)
} acds_session_create_result_t;

/**
 * @brief Create a new session on the discovery server
 *
 * Sends SESSION_CREATE packet and receives SESSION_CREATED response.
 *
 * @param client Connected ACDS client
 * @param params Session creation parameters
 * @param result Session creation result (output)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acds_session_create(acds_client_t *client, const acds_session_create_params_t *params,
                                      acds_session_create_result_t *result);

/**
 * @brief Session lookup result
 *
 * NOTE: Does NOT include server connection information (IP/port).
 * Server address is only revealed after successful authentication via acds_session_join().
 */
typedef struct {
  bool found;                   ///< Session exists
  uint8_t session_id[16];       ///< Session UUID (if found)
  uint8_t host_pubkey[32];      ///< Host's Ed25519 public key
  uint8_t capabilities;         ///< Session capabilities
  uint8_t max_participants;     ///< Maximum participants
  uint8_t current_participants; ///< Current participant count
  bool has_password;            ///< Password required to join
  uint64_t created_at;          ///< Creation timestamp (Unix ms)
  uint64_t expires_at;          ///< Expiration timestamp (Unix ms)
  bool require_server_verify;   ///< ACDS policy: server must verify client identity
  bool require_client_verify;   ///< ACDS policy: client must verify server identity
} acds_session_lookup_result_t;

/**
 * @brief Look up session by string
 *
 * Sends SESSION_LOOKUP packet and receives SESSION_INFO response.
 *
 * @param client Connected ACDS client
 * @param session_string Session string to look up
 * @param result Lookup result (output)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acds_session_lookup(acds_client_t *client, const char *session_string,
                                      acds_session_lookup_result_t *result);

/**
 * @brief Session join parameters
 */
typedef struct {
  const char *session_string;  ///< Session to join
  uint8_t identity_pubkey[32]; ///< Participant's Ed25519 public key
  uint8_t identity_seckey[64]; ///< Ed25519 secret key (for signing)
  bool has_password;           ///< Password provided
  char password[128];          ///< Password (if has_password)
} acds_session_join_params_t;

/**
 * @brief Session join result
 *
 * Server connection information is ONLY included after successful authentication.
 * This prevents IP address leakage to unauthenticated clients.
 */
typedef struct {
  bool success;               ///< Join succeeded
  uint8_t participant_id[16]; ///< Participant UUID (if success)
  uint8_t session_id[16];     ///< Session UUID (if success)
  uint8_t error_code;         ///< Error code (if !success)
  char error_message[129];    ///< Error message (if !success, null-terminated)
  uint8_t session_type;       ///< acds_session_type_t: 0=DIRECT_TCP, 1=WEBRTC (if success)
  char server_address[65];    ///< Server IP/hostname (if success, null-terminated)
  uint16_t server_port;       ///< Server port (if success)
} acds_session_join_result_t;

/**
 * @brief Join an existing session
 *
 * Sends SESSION_JOIN packet and receives SESSION_JOINED response.
 *
 * @param client Connected ACDS client
 * @param params Join parameters
 * @param result Join result (output)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acds_session_join(acds_client_t *client, const acds_session_join_params_t *params,
                                    acds_session_join_result_t *result);

// ============================================================================
// Cryptographic Signature Helpers
// ============================================================================

/**
 * @brief Sign a SESSION_CREATE message
 *
 * Computes Ed25519 signature over: type || timestamp || capabilities || max_participants
 *
 * @param identity_seckey Ed25519 secret key (64 bytes)
 * @param timestamp Unix milliseconds timestamp
 * @param capabilities Capabilities bitfield
 * @param max_participants Maximum participants (1-8)
 * @param signature_out Output buffer for signature (64 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acds_sign_session_create(const uint8_t identity_seckey[64], uint64_t timestamp, uint8_t capabilities,
                                           uint8_t max_participants, uint8_t signature_out[64]);

/**
 * @brief Verify SESSION_CREATE signature
 *
 * Verifies Ed25519 signature matches the message fields.
 *
 * @param identity_pubkey Ed25519 public key (32 bytes)
 * @param timestamp Unix milliseconds timestamp
 * @param capabilities Capabilities bitfield
 * @param max_participants Maximum participants
 * @param signature Signature to verify (64 bytes)
 * @return ASCIICHAT_OK if valid, ERROR_CRYPTO_VERIFY_FAILED if invalid
 */
asciichat_error_t acds_verify_session_create(const uint8_t identity_pubkey[32], uint64_t timestamp,
                                             uint8_t capabilities, uint8_t max_participants,
                                             const uint8_t signature[64]);

/**
 * @brief Sign a SESSION_JOIN message
 *
 * Computes Ed25519 signature over: type || timestamp || session_string
 *
 * @param identity_seckey Ed25519 secret key (64 bytes)
 * @param timestamp Unix milliseconds timestamp
 * @param session_string Session string (null-terminated)
 * @param signature_out Output buffer for signature (64 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acds_sign_session_join(const uint8_t identity_seckey[64], uint64_t timestamp,
                                         const char *session_string, uint8_t signature_out[64]);

/**
 * @brief Verify SESSION_JOIN signature
 *
 * Verifies Ed25519 signature matches the message fields.
 *
 * @param identity_pubkey Ed25519 public key (32 bytes)
 * @param timestamp Unix milliseconds timestamp
 * @param session_string Session string (null-terminated)
 * @param signature Signature to verify (64 bytes)
 * @return ASCIICHAT_OK if valid, ERROR_CRYPTO_VERIFY_FAILED if invalid
 */
asciichat_error_t acds_verify_session_join(const uint8_t identity_pubkey[32], uint64_t timestamp,
                                           const char *session_string, const uint8_t signature[64]);

/**
 * @brief Check if timestamp is within acceptable window
 *
 * Validates timestamp is recent (within window_seconds of current time) and not in the future.
 *
 * @param timestamp_ms Unix milliseconds timestamp
 * @param window_seconds Acceptable age window (e.g., 300 for 5 minutes)
 * @return true if timestamp is valid, false if too old or in the future
 */
bool acds_validate_timestamp(uint64_t timestamp_ms, uint32_t window_seconds);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Initialize ACDS client configuration with defaults
 *
 * Sets default values:
 * - server_address: "127.0.0.1"
 * - server_port: 27225
 * - timeout_ms: 5000
 *
 * @param config Configuration to initialize
 */
void acds_client_config_init_defaults(acds_client_config_t *config);
