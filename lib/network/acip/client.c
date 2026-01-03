/**
 * @file network/acip/client.c
 * @brief ACIP client-side protocol implementation
 * @ingroup acip
 *
 * Provides client-side ACIP protocol implementation for:
 * - Session discovery and management
 * - WebRTC signaling relay
 * - String reservation (future)
 *
 * ACIP (ASCII-Chat IP Protocol) is the wire protocol for session
 * discovery and WebRTC signaling. ACDS is the reference server implementation.
 */

#include "network/acip/client.h"
#include "common.h"
#include "crypto/crypto.h"
#include "log/logging.h"
#include "network/packet.h"
#include "platform/socket.h"
#include "util/endian.h"

#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#endif

// ============================================================================
// Helper Functions
// ============================================================================

void acds_client_config_init_defaults(acds_client_config_t *config) {
  if (!config) {
    return;
  }

  memset(config, 0, sizeof(*config));
  SAFE_STRNCPY(config->server_address, "127.0.0.1", sizeof(config->server_address));
  config->server_port = ACIP_DISCOVERY_DEFAULT_PORT;
  config->timeout_ms = 5000;
}

// ============================================================================
// Connection Management
// ============================================================================

asciichat_error_t acds_client_connect(acds_client_t *client, const acds_client_config_t *config) {
  if (!client || !config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "client or config is NULL");
  }

  memset(client, 0, sizeof(*client));
  memcpy(&client->config, config, sizeof(acds_client_config_t));
  client->socket = INVALID_SOCKET_VALUE;
  client->connected = false;

  // Create TCP socket
  client->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client->socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to create socket: %s", socket_get_error_string());
  }

  // Set socket timeouts (SO_RCVTIMEO/SO_SNDTIMEO)
  // Convert timeout_ms to platform-specific format
#ifdef _WIN32
  // Windows uses DWORD (milliseconds)
  DWORD timeout_val = config->timeout_ms;
  if (setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_val, sizeof(timeout_val)) < 0) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket receive timeout");
  }
  if (setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_val, sizeof(timeout_val)) < 0) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket send timeout");
  }
#else
  // POSIX uses struct timeval (seconds and microseconds)
  struct timeval tv;
  tv.tv_sec = config->timeout_ms / 1000;
  tv.tv_usec = (config->timeout_ms % 1000) * 1000;
  if (setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket receive timeout");
  }
  if (setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket send timeout");
  }
#endif

  // Connect to server
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(config->server_port);

  if (inet_pton(AF_INET, config->server_address, &server_addr.sin_addr) <= 0) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
    return SET_ERRNO(ERROR_NETWORK, "Invalid server address: %s", config->server_address);
  }

  if (connect(client->socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
    return SET_ERRNO(ERROR_NETWORK, "Failed to connect to %s:%d: %s", config->server_address, config->server_port,
                     socket_get_error_string());
  }

  client->connected = true;
  log_info("Connected to ACDS server at %s:%d", config->server_address, config->server_port);

  return ASCIICHAT_OK;
}

void acds_client_disconnect(acds_client_t *client) {
  if (!client) {
    return;
  }

  if (client->socket != INVALID_SOCKET_VALUE) {
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
  }

  client->connected = false;
  log_debug("Disconnected from ACDS server");
}

// ============================================================================
// Session Management
// ============================================================================

asciichat_error_t acds_session_create(acds_client_t *client, const acds_session_create_params_t *params,
                                      acds_session_create_result_t *result) {
  if (!client || !params || !result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (!client->connected) {
    return SET_ERRNO(ERROR_NETWORK, "Not connected to ACDS server");
  }

  // Build SESSION_CREATE payload
  acip_session_create_t req;
  memset(&req, 0, sizeof(req));

  memcpy(req.identity_pubkey, params->identity_pubkey, 32);

  // Sign the request: type || timestamp || capabilities
  uint64_t timestamp = (uint64_t)time(NULL) * 1000; // Unix ms
  req.timestamp = timestamp;
  req.capabilities = params->capabilities;
  req.max_participants = params->max_participants;

  // Generate Ed25519 signature for identity verification
  asciichat_error_t sign_result = acds_sign_session_create(params->identity_seckey, timestamp, params->capabilities,
                                                           params->max_participants, req.signature);
  if (sign_result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to sign SESSION_CREATE request");
  }

  req.has_password = params->has_password ? 1 : 0;
  if (params->has_password) {
    // Hash password with Argon2id using libsodium
    // crypto_pwhash_str produces a null-terminated ASCII string
    if (crypto_pwhash_str((char *)req.password_hash, params->password, strlen(params->password),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
      return SET_ERRNO(ERROR_CRYPTO, "Failed to hash password (out of memory)");
    }
  } else {
    memset(req.password_hash, 0, sizeof(req.password_hash));
  }

  req.reserved_string_len = 0; // Auto-generate
  if (params->reserved_string && params->reserved_string[0] != '\0') {
    req.reserved_string_len = strlen(params->reserved_string);
    // Variable part would follow here (not implemented yet)
  }

  // Server connection information
  SAFE_STRNCPY(req.server_address, params->server_address, sizeof(req.server_address));
  req.server_port = params->server_port;

  // IP disclosure policy
  // Auto-detection: If password is set, IP will be revealed after verification
  // If no password, require explicit opt-in via acds_expose_ip
  req.expose_ip_publicly = params->acds_expose_ip ? 1 : 0;

  // Session type (Direct TCP or WebRTC)
  req.session_type = params->session_type;

  // Send SESSION_CREATE packet
  asciichat_error_t send_result = send_packet(client->socket, PACKET_TYPE_ACIP_SESSION_CREATE, &req, sizeof(req));
  if (send_result != ASCIICHAT_OK) {
    return send_result;
  }

  log_debug("Sent SESSION_CREATE request");

  // Receive SESSION_CREATED response
  packet_type_t resp_type;
  void *resp_payload = NULL;
  size_t resp_size = 0;

  int recv_result = receive_packet(client->socket, &resp_type, &resp_payload, &resp_size);
  if (recv_result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive SESSION_CREATED response");
  }

  // Check response type
  if (resp_type != PACKET_TYPE_ACIP_SESSION_CREATED) {
    if (resp_type == PACKET_TYPE_ERROR_MESSAGE || resp_type == PACKET_TYPE_ACIP_ERROR) {
      log_error("Session creation failed: server returned error packet");
      SAFE_FREE(resp_payload);
      return SET_ERRNO(ERROR_NETWORK, "Session creation failed");
    }
    SAFE_FREE(resp_payload);
    return SET_ERRNO(ERROR_NETWORK, "Unexpected response type: 0x%02X", resp_type);
  }

  // Parse SESSION_CREATED response
  if (resp_size < sizeof(acip_session_created_t)) {
    SAFE_FREE(resp_payload);
    return SET_ERRNO(ERROR_NETWORK, "SESSION_CREATED response too small: %zu bytes", resp_size);
  }

  acip_session_created_t *resp = (acip_session_created_t *)resp_payload;

  // Copy session string (null-terminate)
  size_t string_len = resp->session_string_len < sizeof(result->session_string) - 1
                          ? resp->session_string_len
                          : sizeof(result->session_string) - 1;
  memcpy(result->session_string, resp->session_string, string_len);
  result->session_string[string_len] = '\0';

  memcpy(result->session_id, resp->session_id, 16);
  result->expires_at = resp->expires_at;

  log_info("Session created: %s (expires at %llu)", result->session_string, (unsigned long long)result->expires_at);

  SAFE_FREE(resp_payload);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_session_lookup(acds_client_t *client, const char *session_string,
                                      acds_session_lookup_result_t *result) {
  if (!client || !session_string || !result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (!client->connected) {
    return SET_ERRNO(ERROR_NETWORK, "Not connected to ACDS server");
  }

  // Build SESSION_LOOKUP payload
  acip_session_lookup_t req;
  memset(&req, 0, sizeof(req));

  req.session_string_len = strlen(session_string);
  if (req.session_string_len >= sizeof(req.session_string)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session string too long");
  }

  memcpy(req.session_string, session_string, req.session_string_len);

  // Send SESSION_LOOKUP packet
  asciichat_error_t send_result = send_packet(client->socket, PACKET_TYPE_ACIP_SESSION_LOOKUP, &req, sizeof(req));
  if (send_result != ASCIICHAT_OK) {
    return send_result;
  }

  log_debug("Sent SESSION_LOOKUP request for '%s'", session_string);

  // Receive SESSION_INFO response
  packet_type_t resp_type;
  void *resp_payload = NULL;
  size_t resp_size = 0;

  int recv_result = receive_packet(client->socket, &resp_type, &resp_payload, &resp_size);
  if (recv_result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive SESSION_INFO response");
  }

  if (resp_type != PACKET_TYPE_ACIP_SESSION_INFO) {
    SAFE_FREE(resp_payload);
    return SET_ERRNO(ERROR_NETWORK, "Unexpected response type: 0x%02X", resp_type);
  }

  if (resp_size < sizeof(acip_session_info_t)) {
    SAFE_FREE(resp_payload);
    return SET_ERRNO(ERROR_NETWORK, "SESSION_INFO response too small");
  }

  acip_session_info_t *resp = (acip_session_info_t *)resp_payload;

  // Copy result
  result->found = resp->found != 0;
  if (result->found) {
    memcpy(result->session_id, resp->session_id, 16);
    memcpy(result->host_pubkey, resp->host_pubkey, 32);
    result->capabilities = resp->capabilities;
    result->max_participants = resp->max_participants;
    result->current_participants = resp->current_participants;
    result->has_password = resp->has_password != 0;
    result->created_at = resp->created_at;
    result->expires_at = resp->expires_at;
    result->require_server_verify = resp->require_server_verify != 0;
    result->require_client_verify = resp->require_client_verify != 0;

    log_info("Session found: %s (%d/%d participants, password=%s, policies: server_verify=%d client_verify=%d)",
             session_string, result->current_participants, result->max_participants,
             result->has_password ? "required" : "not required", result->require_server_verify,
             result->require_client_verify);
  } else {
    log_info("Session not found: %s", session_string);
  }

  SAFE_FREE(resp_payload);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_session_join(acds_client_t *client, const acds_session_join_params_t *params,
                                    acds_session_join_result_t *result) {
  if (!client || !params || !result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (!client->connected) {
    return SET_ERRNO(ERROR_NETWORK, "Not connected to ACDS server");
  }

  // Build SESSION_JOIN payload
  acip_session_join_t req;
  memset(&req, 0, sizeof(req));

  req.session_string_len = strlen(params->session_string);
  if (req.session_string_len >= sizeof(req.session_string)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session string too long");
  }

  memcpy(req.session_string, params->session_string, req.session_string_len);
  memcpy(req.identity_pubkey, params->identity_pubkey, 32);

  // Sign the request: type || timestamp || session_string
  uint64_t timestamp = (uint64_t)time(NULL) * 1000;
  req.timestamp = timestamp;

  // Generate Ed25519 signature for identity verification
  asciichat_error_t sign_result =
      acds_sign_session_join(params->identity_seckey, timestamp, params->session_string, req.signature);
  if (sign_result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to sign SESSION_JOIN request");
  }

  req.has_password = params->has_password ? 1 : 0;
  if (params->has_password) {
    SAFE_STRNCPY(req.password, params->password, sizeof(req.password));
  }

  // Send SESSION_JOIN packet
  asciichat_error_t send_result = send_packet(client->socket, PACKET_TYPE_ACIP_SESSION_JOIN, &req, sizeof(req));
  if (send_result != ASCIICHAT_OK) {
    return send_result;
  }

  log_debug("Sent SESSION_JOIN request for '%s'", params->session_string);

  // Receive SESSION_JOINED response
  packet_type_t resp_type;
  void *resp_payload = NULL;
  size_t resp_size = 0;

  int recv_result = receive_packet(client->socket, &resp_type, &resp_payload, &resp_size);
  if (recv_result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive SESSION_JOINED response");
  }

  if (resp_type != PACKET_TYPE_ACIP_SESSION_JOINED) {
    SAFE_FREE(resp_payload);
    return SET_ERRNO(ERROR_NETWORK, "Unexpected response type: 0x%02X", resp_type);
  }

  if (resp_size < sizeof(acip_session_joined_t)) {
    SAFE_FREE(resp_payload);
    return SET_ERRNO(ERROR_NETWORK, "SESSION_JOINED response too small");
  }

  acip_session_joined_t *resp = (acip_session_joined_t *)resp_payload;

  // Copy result
  result->success = resp->success != 0;
  if (result->success) {
    memcpy(result->participant_id, resp->participant_id, 16);
    memcpy(result->session_id, resp->session_id, 16);
    // Server connection information (ONLY revealed after successful authentication)
    result->session_type = resp->session_type;
    SAFE_STRNCPY(result->server_address, resp->server_address, sizeof(result->server_address));
    result->server_port = resp->server_port;
    log_info("Joined session successfully (participant ID: %02x%02x..., server=%s:%d, type=%s)",
             result->participant_id[0], result->participant_id[1], result->server_address, result->server_port,
             result->session_type == SESSION_TYPE_WEBRTC ? "WebRTC" : "DirectTCP");
  } else {
    result->error_code = resp->error_code;
    size_t msg_len = strnlen(resp->error_message, sizeof(resp->error_message));
    if (msg_len >= sizeof(result->error_message)) {
      msg_len = sizeof(result->error_message) - 1;
    }
    memcpy(result->error_message, resp->error_message, msg_len);
    result->error_message[msg_len] = '\0';
    log_warn("Failed to join session: %s (code %d)", result->error_message, result->error_code);
  }

  SAFE_FREE(resp_payload);
  return ASCIICHAT_OK;
}

// ============================================================================
// Cryptographic Signature Helpers
// ============================================================================

asciichat_error_t acds_sign_session_create(const uint8_t identity_seckey[64], uint64_t timestamp, uint8_t capabilities,
                                           uint8_t max_participants, uint8_t signature_out[64]) {
  if (!identity_seckey || !signature_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter to acds_sign_session_create");
  }

  // Build message: type (1 byte) || timestamp (8 bytes) || capabilities (1 byte) || max_participants (1 byte)
  uint8_t message[11];
  message[0] = (uint8_t)PACKET_TYPE_ACIP_SESSION_CREATE;

  // Convert timestamp to big-endian (network byte order)
  message[1] = (uint8_t)(timestamp >> 56);
  message[2] = (uint8_t)(timestamp >> 48);
  message[3] = (uint8_t)(timestamp >> 40);
  message[4] = (uint8_t)(timestamp >> 32);
  message[5] = (uint8_t)(timestamp >> 24);
  message[6] = (uint8_t)(timestamp >> 16);
  message[7] = (uint8_t)(timestamp >> 8);
  message[8] = (uint8_t)(timestamp);

  message[9] = capabilities;
  message[10] = max_participants;

  // Sign using Ed25519
  if (crypto_sign_detached(signature_out, NULL, message, sizeof(message), identity_seckey) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Ed25519 signature generation failed");
  }

  log_debug("Generated SESSION_CREATE signature (timestamp=%llu, caps=%u, max=%u)", (unsigned long long)timestamp,
            capabilities, max_participants);

  return ASCIICHAT_OK;
}

asciichat_error_t acds_verify_session_create(const uint8_t identity_pubkey[32], uint64_t timestamp,
                                             uint8_t capabilities, uint8_t max_participants,
                                             const uint8_t signature[64]) {
  if (!identity_pubkey || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter to acds_verify_session_create");
  }

  // Build message: type (1 byte) || timestamp (8 bytes) || capabilities (1 byte) || max_participants (1 byte)
  uint8_t message[11];
  message[0] = (uint8_t)PACKET_TYPE_ACIP_SESSION_CREATE;

  // Convert timestamp to big-endian (network byte order)
  message[1] = (uint8_t)(timestamp >> 56);
  message[2] = (uint8_t)(timestamp >> 48);
  message[3] = (uint8_t)(timestamp >> 40);
  message[4] = (uint8_t)(timestamp >> 32);
  message[5] = (uint8_t)(timestamp >> 24);
  message[6] = (uint8_t)(timestamp >> 16);
  message[7] = (uint8_t)(timestamp >> 8);
  message[8] = (uint8_t)(timestamp);

  message[9] = capabilities;
  message[10] = max_participants;

  // Verify using Ed25519
  if (crypto_sign_verify_detached(signature, message, sizeof(message), identity_pubkey) != 0) {
    log_warn("SESSION_CREATE signature verification failed");
    return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "Invalid SESSION_CREATE signature");
  }

  log_debug("SESSION_CREATE signature verified successfully");
  return ASCIICHAT_OK;
}

asciichat_error_t acds_sign_session_join(const uint8_t identity_seckey[64], uint64_t timestamp,
                                         const char *session_string, uint8_t signature_out[64]) {
  if (!identity_seckey || !session_string || !signature_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter to acds_sign_session_join");
  }

  size_t session_len = strlen(session_string);
  if (session_len > 48) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session string too long (max 48 chars)");
  }

  // Build message: type (1 byte) || timestamp (8 bytes) || session_string (variable length)
  uint8_t message[1 + 8 + 48];
  message[0] = (uint8_t)PACKET_TYPE_ACIP_SESSION_JOIN;

  // Convert timestamp to big-endian (network byte order)
  message[1] = (uint8_t)(timestamp >> 56);
  message[2] = (uint8_t)(timestamp >> 48);
  message[3] = (uint8_t)(timestamp >> 40);
  message[4] = (uint8_t)(timestamp >> 32);
  message[5] = (uint8_t)(timestamp >> 24);
  message[6] = (uint8_t)(timestamp >> 16);
  message[7] = (uint8_t)(timestamp >> 8);
  message[8] = (uint8_t)(timestamp);

  // Copy session string (copy exactly session_len bytes, no null terminator in signature)
  memcpy(&message[9], session_string, session_len);

  size_t message_len = 9 + session_len;

  // Sign using Ed25519
  if (crypto_sign_detached(signature_out, NULL, message, message_len, identity_seckey) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Ed25519 signature generation failed");
  }

  log_debug("Generated SESSION_JOIN signature (timestamp=%llu, session='%s')", (unsigned long long)timestamp,
            session_string);

  return ASCIICHAT_OK;
}

asciichat_error_t acds_verify_session_join(const uint8_t identity_pubkey[32], uint64_t timestamp,
                                           const char *session_string, const uint8_t signature[64]) {
  if (!identity_pubkey || !session_string || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter to acds_verify_session_join");
  }

  size_t session_len = strlen(session_string);
  if (session_len > 48) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session string too long (max 48 chars)");
  }

  // Build message: type (1 byte) || timestamp (8 bytes) || session_string (variable length)
  uint8_t message[1 + 8 + 48];
  message[0] = (uint8_t)PACKET_TYPE_ACIP_SESSION_JOIN;

  // Convert timestamp to big-endian (network byte order)
  message[1] = (uint8_t)(timestamp >> 56);
  message[2] = (uint8_t)(timestamp >> 48);
  message[3] = (uint8_t)(timestamp >> 40);
  message[4] = (uint8_t)(timestamp >> 32);
  message[5] = (uint8_t)(timestamp >> 24);
  message[6] = (uint8_t)(timestamp >> 16);
  message[7] = (uint8_t)(timestamp >> 8);
  message[8] = (uint8_t)(timestamp);

  // Copy session string (copy exactly session_len bytes, no null terminator in signature)
  memcpy(&message[9], session_string, session_len);

  size_t message_len = 9 + session_len;

  // Verify using Ed25519
  if (crypto_sign_verify_detached(signature, message, message_len, identity_pubkey) != 0) {
    log_warn("SESSION_JOIN signature verification failed");
    return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "Invalid SESSION_JOIN signature");
  }

  log_debug("SESSION_JOIN signature verified successfully");
  return ASCIICHAT_OK;
}

bool acds_validate_timestamp(uint64_t timestamp_ms, uint32_t window_seconds) {
  // Get current time in milliseconds
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    log_error("clock_gettime failed");
    return false;
  }

  uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
  uint64_t window_ms = (uint64_t)window_seconds * 1000;

  // Check if timestamp is too far in the future (allow 60 second clock skew)
  if (timestamp_ms > now_ms + 60000) {
    // Cast to signed before subtraction to avoid unsigned underflow
    int64_t skew = (int64_t)timestamp_ms - (int64_t)now_ms;
    log_warn("Timestamp is in the future: %llu > %llu (skew: %lld ms)", (unsigned long long)timestamp_ms,
             (unsigned long long)now_ms, (long long)skew);
    return false;
  }

  // Check if timestamp is too old
  // To avoid unsigned underflow, check if now_ms is large enough before subtracting
  uint64_t min_valid_timestamp = (now_ms >= window_ms) ? (now_ms - window_ms) : 0;
  if (timestamp_ms < min_valid_timestamp) {
    // Cast to signed before subtraction to avoid unsigned underflow
    int64_t age = (int64_t)now_ms - (int64_t)timestamp_ms;
    log_warn("Timestamp is too old: %llu < %llu (age: %lld ms, max: %u seconds)", (unsigned long long)timestamp_ms,
             (unsigned long long)min_valid_timestamp, (long long)age, window_seconds);
    return false;
  }

  // Cast to signed before subtraction to avoid unsigned underflow
  int64_t age = (int64_t)now_ms - (int64_t)timestamp_ms;
  log_debug("Timestamp validation passed (age: %lld ms, window: %u seconds)", (long long)age, window_seconds);
  return true;
}
