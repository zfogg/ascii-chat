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
 * ACIP (ascii-chat IP Protocol) is the wire protocol for session
 * discovery and WebRTC signaling. ACDS is the reference server implementation.
 */

#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/parallel_connect.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>

#include <ascii-chat/network/errors.h>
#include <string.h>
#include <time.h>
#include <errno.h>

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

  // Convert timestamp to network byte order and pack into message
  uint64_t timestamp_net = HOST_TO_NET_U64(timestamp);
  memcpy(&message[1], &timestamp_net, sizeof(timestamp_net));

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

  // Convert timestamp to network byte order and pack into message
  uint64_t timestamp_net = HOST_TO_NET_U64(timestamp);
  memcpy(&message[1], &timestamp_net, sizeof(timestamp_net));

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

  // Convert timestamp to network byte order and pack into message
  uint64_t timestamp_net = HOST_TO_NET_U64(timestamp);
  memcpy(&message[1], &timestamp_net, sizeof(timestamp_net));

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

  // Convert timestamp to network byte order and pack into message
  uint64_t timestamp_net = HOST_TO_NET_U64(timestamp);
  memcpy(&message[1], &timestamp_net, sizeof(timestamp_net));

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
  uint64_t now_ms = time_ns_to_ms(time_get_realtime_ns());
  uint64_t window_ms = (uint64_t)window_seconds * 1000;

  if (timestamp_ms > now_ms + 60000) {
    int64_t skew = (int64_t)timestamp_ms - (int64_t)now_ms;
    log_warn("Timestamp is in the future: %llu > %llu (skew: %lld ms)", (unsigned long long)timestamp_ms,
             (unsigned long long)now_ms, (long long)skew);
    return false;
  }

  uint64_t min_valid_timestamp = (now_ms >= window_ms) ? (now_ms - window_ms) : 0;
  if (timestamp_ms < min_valid_timestamp) {
    int64_t age = (int64_t)now_ms - (int64_t)timestamp_ms;
    log_warn("Timestamp is too old: %llu < %llu (age: %lld ms, max: %u seconds)", (unsigned long long)timestamp_ms,
             (unsigned long long)min_valid_timestamp, (long long)age, window_seconds);
    return false;
  }

  int64_t age = (int64_t)now_ms - (int64_t)timestamp_ms;
  log_debug("Timestamp validation passed (age: %lld ms, window: %u seconds)", (long long)age, window_seconds);
  return true;
}
#include <ascii-chat/network/acip/acds_protocol.h>

static asciichat_error_t protocol_fail(acds_protocol_t *p, asciichat_error_t error, const char *message,
                                       bool terminal) {
  p->state = ACDS_FAILED;
  p->terminal = terminal;
  SAFE_STRNCPY(p->error, message, sizeof(p->error));
  sodium_memzero(p->join.identity_seckey, sizeof(p->join.identity_seckey));
  sodium_memzero(p->join.password, sizeof(p->join.password));
  return SET_ERRNO(error, "%s", message);
}

void acds_protocol_init(acds_protocol_t *p, acds_protocol_send_fn send, void *user_data) {
  memset(p, 0, sizeof(*p));
  p->send = send;
  p->user_data = user_data;
}
void acds_protocol_destroy(acds_protocol_t *p) {
  if (p)
    sodium_memzero(p, sizeof(*p));
}
static asciichat_error_t protocol_send(acds_protocol_t *p, packet_type_t type, const void *data, size_t length) {
  if (!p || !p->send)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Discovery transport missing");
  asciichat_error_t result = p->send(p->user_data, type, data, length);
  if (result != ASCIICHAT_OK)
    return protocol_fail(p, result, "Discovery send failed", false);
  return ASCIICHAT_OK;
}
static bool valid_name(const char *name) {
  if (!name || !name[0] || strlen(name) > 47)
    return false;
  for (const char *c = name; *c; c++)
    if (!(*c >= 'a' && *c <= 'z') && !(*c >= '0' && *c <= '9') && *c != '-')
      return false;
  return true;
}

asciichat_error_t acds_protocol_create(acds_protocol_t *p, const acds_session_create_params_t *params,
                                       uint8_t total_keys, uint8_t key_index) {
  if (!p || !params)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery create parameters");
  if (params->reserved_string && params->reserved_string[0])
    return protocol_fail(p, ERROR_NOT_SUPPORTED, "Reserved session names are not supported", true);
  acip_session_create_t req = {0};
  memcpy(req.identity_pubkey, params->identity_pubkey, sizeof(req.identity_pubkey));
  req.timestamp = time_ns_to_ms(time_get_realtime_ns());
  req.capabilities = params->capabilities;
  req.max_participants = params->max_participants;
  req.session_type = params->session_type;
  req.total_keys = total_keys;
  req.key_index = key_index;
  req.has_password = params->has_password;
  req.expose_ip_publicly = params->acds_expose_ip;
  SAFE_STRNCPY(req.server_address, params->server_address, sizeof(req.server_address));
  req.server_port = params->server_port;
  asciichat_error_t result = acds_sign_session_create(params->identity_seckey, req.timestamp, req.capabilities,
                                                      req.max_participants, req.signature);
  if (result != ASCIICHAT_OK)
    return protocol_fail(p, result, "Could not sign session creation", true);
  if (params->has_password &&
      crypto_pwhash_str((char *)req.password_hash, params->password,
                        strnlen(params->password, sizeof(params->password)), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    return protocol_fail(p, ERROR_CRYPTO, "Could not hash session password", true);
  p->state = ACDS_CREATING;
  result = protocol_send(p, PACKET_TYPE_ACIP_SESSION_CREATE, &req, sizeof(req));
  sodium_memzero(&req, sizeof(req));
  return result;
}
asciichat_error_t acds_protocol_lookup(acds_protocol_t *p, const char *name) {
  if (!p)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing discovery protocol");
  if (!valid_name(name))
    return protocol_fail(p, ERROR_INVALID_PARAM, "Invalid session name", true);
  acip_session_lookup_t req = {0};
  req.session_string_len = (uint8_t)strlen(name);
  memcpy(req.session_string, name, req.session_string_len);
  SAFE_STRNCPY(p->name, name, sizeof(p->name));
  p->state = ACDS_LOOKING_UP;
  return protocol_send(p, PACKET_TYPE_ACIP_SESSION_LOOKUP, &req, sizeof(req));
}
asciichat_error_t acds_protocol_join(acds_protocol_t *p, const acds_session_join_params_t *params) {
  if (!p || !params)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery join parameters");
  if (!valid_name(params->session_string))
    return protocol_fail(p, ERROR_INVALID_PARAM, "Invalid session name", true);
  acip_session_join_t req = {0};
  req.session_string_len = (uint8_t)strlen(params->session_string);
  memcpy(req.session_string, params->session_string, req.session_string_len);
  memcpy(req.identity_pubkey, params->identity_pubkey, sizeof(req.identity_pubkey));
  req.timestamp = time_ns_to_ms(time_get_realtime_ns());
  req.has_password = params->has_password;
  if (params->has_password)
    SAFE_STRNCPY(req.password, params->password, sizeof(req.password));
  asciichat_error_t result =
      acds_sign_session_join(params->identity_seckey, req.timestamp, params->session_string, req.signature);
  if (result != ASCIICHAT_OK)
    return protocol_fail(p, result, "Could not sign session join", true);
  p->state = ACDS_JOINING;
  result = protocol_send(p, PACKET_TYPE_ACIP_SESSION_JOIN, &req, sizeof(req));
  sodium_memzero(&req, sizeof(req));
  return result;
}
asciichat_error_t acds_protocol_enter(acds_protocol_t *p, bool create, const char *name, const char *password) {
  if (!p || !password)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery parameters");
  if (strlen(password) > 127)
    return protocol_fail(p, ERROR_INVALID_PARAM, "Password is too long", true);
  if (crypto_sign_keypair(p->join.identity_pubkey, p->join.identity_seckey) != 0)
    return protocol_fail(p, ERROR_CRYPTO, "Could not generate discovery identity", true);
  p->auto_join = true;
  p->require_webrtc = true;
  p->join.has_password = password[0] != 0;
  SAFE_STRNCPY(p->join.password, password, sizeof(p->join.password));
  p->join.session_string = p->name;
  if (!create)
    return acds_protocol_lookup(p, name);
  acds_session_create_params_t params = {.capabilities = 3,
                                         .max_participants = 8,
                                         .session_type = SESSION_TYPE_WEBRTC,
                                         .has_password = p->join.has_password};
  memcpy(params.identity_pubkey, p->join.identity_pubkey, sizeof(params.identity_pubkey));
  memcpy(params.identity_seckey, p->join.identity_seckey, sizeof(params.identity_seckey));
  memcpy(params.password, p->join.password, sizeof(params.password));
  asciichat_error_t result = acds_protocol_create(p, &params, 0, 0);
  sodium_memzero(&params, sizeof(params));
  return result;
}
asciichat_error_t acds_protocol_receive(acds_protocol_t *p, packet_type_t type, const void *payload, size_t length) {
  if (!p || (!payload && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery packet");
  if (p->state == ACDS_FAILED || p->state == ACDS_ENDED)
    return SET_ERRNO(ERROR_INVALID_STATE, "Discovery session has ended");
  if (type == PACKET_TYPE_ACIP_ERROR || type == PACKET_TYPE_ERROR_MESSAGE) {
    asciichat_error_t result = network_error_decode(type, payload, length);
    return protocol_fail(p, result, asciichat_error_string(result), true);
  }
  if (type == PACKET_TYPE_ACIP_SESSION_END) {
    p->state = ACDS_ENDED;
    p->terminal = true;
    SAFE_STRNCPY(p->error, "Session ended", sizeof(p->error));
    return SET_ERRNO(ERROR_NETWORK, "Session ended");
  }
  if (type == PACKET_TYPE_ACIP_SESSION_CREATED && p->state == ACDS_CREATING) {
    if (length < sizeof(p->created))
      return protocol_fail(p, ERROR_NETWORK_PROTOCOL, "Truncated created session", true);
    memcpy(&p->created, payload, sizeof(p->created));
    if (!p->created.session_string_len || p->created.session_string_len > 47)
      return protocol_fail(p, ERROR_NETWORK_PROTOCOL, "Invalid created session name", true);
    memcpy(p->name, p->created.session_string, p->created.session_string_len);
    p->name[p->created.session_string_len] = 0;
    p->state = ACDS_CREATED;
    if (p->auto_join)
      return acds_protocol_join(p, &p->join);
    return ASCIICHAT_OK;
  }
  if (type == PACKET_TYPE_ACIP_SESSION_INFO && p->state == ACDS_LOOKING_UP) {
    if (length < sizeof(p->info))
      return protocol_fail(p, ERROR_NETWORK_PROTOCOL, "Truncated session information", true);
    memcpy(&p->info, payload, sizeof(p->info));
    p->state = ACDS_FOUND;
    if (!p->auto_join)
      return ASCIICHAT_OK;
    if (!p->info.found)
      return protocol_fail(p, ERROR_NETWORK, "Session not found", true);
    if (p->require_webrtc && p->info.session_type != SESSION_TYPE_WEBRTC)
      return protocol_fail(p, ERROR_NOT_SUPPORTED, "This session does not offer WebRTC", true);
    if (p->info.require_server_verify || p->info.require_client_verify)
      return protocol_fail(p, ERROR_NOT_SUPPORTED, "This session requires an authorized identity", true);
    if (p->info.has_password && !p->join.has_password)
      return protocol_fail(p, ERROR_CRYPTO_AUTH, "This session requires a password", true);
    return acds_protocol_join(p, &p->join);
  }
  if (type == PACKET_TYPE_ACIP_SESSION_JOINED && p->state == ACDS_JOINING) {
    if (length < sizeof(p->joined))
      return protocol_fail(p, ERROR_NETWORK_PROTOCOL, "Truncated discovery join response", true);
    memcpy(&p->joined, payload, sizeof(p->joined));
    p->joined.error_message[sizeof(p->joined.error_message) - 1] = 0;
    p->joined.server_address[sizeof(p->joined.server_address) - 1] = 0;
    p->joined.turn_username[sizeof(p->joined.turn_username) - 1] = 0;
    p->joined.turn_password[sizeof(p->joined.turn_password) - 1] = 0;
    if (!p->joined.success)
      return protocol_fail(p, network_error_from_acip(p->joined.error_code),
                           p->joined.error_message[0] ? p->joined.error_message : "Session join rejected", true);
    if (p->require_webrtc && p->joined.session_type != SESSION_TYPE_WEBRTC)
      return protocol_fail(p, ERROR_NOT_SUPPORTED, "This session does not offer WebRTC", true);
    p->state = ACDS_JOINED;
    sodium_memzero(p->join.identity_seckey, sizeof(p->join.identity_seckey));
    sodium_memzero(p->join.password, sizeof(p->join.password));
    return ASCIICHAT_OK;
  }
  // Signaling and participant notifications are delivered by the caller's transport adapter.
  if (p->state == ACDS_JOINED && type != PACKET_TYPE_ACIP_SESSION_CREATED && type != PACKET_TYPE_ACIP_SESSION_INFO &&
      type != PACKET_TYPE_ACIP_SESSION_JOINED)
    return ASCIICHAT_OK;
  return protocol_fail(p, ERROR_NETWORK_PROTOCOL, "Unexpected discovery response", true);
}
asciichat_error_t acds_protocol_leave(acds_protocol_t *p) {
  if (!p)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing discovery protocol");
  if (p->state != ACDS_JOINED)
    return ASCIICHAT_OK;
  acip_session_leave_t req = {0};
  memcpy(req.session_id, p->joined.session_id, sizeof(req.session_id));
  memcpy(req.participant_id, p->joined.participant_id, sizeof(req.participant_id));
  p->state = ACDS_ENDED;
  return protocol_send(p, PACKET_TYPE_ACIP_SESSION_LEAVE, &req, sizeof(req));
}
asciichat_error_t acds_handshake_begin(acip_transport_t *transport, bool encrypted) {
  protocol_version_packet_t version = {0};
  version.protocol_version = HOST_TO_NET_U16(1);
  version.supports_encryption = encrypted;
  return packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
}
asciichat_error_t acds_handshake_receive(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                         packet_type_t type, const void *payload, size_t length) {
  if (!ctx || !transport || (!payload && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid handshake packet");
  switch (type) {
  case PACKET_TYPE_CRYPTO_PARAMETERS:
    if (ctx->state != CRYPTO_HANDSHAKE_INIT || length != sizeof(crypto_parameters_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid discovery crypto parameters");
    return crypto_handshake_set_parameters(ctx, payload);
  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT:
    return crypto_handshake_client_key_exchange(ctx, transport, type, payload, length);
  case PACKET_TYPE_CRYPTO_AUTH_CHALLENGE:
    return crypto_handshake_client_auth_response(ctx, transport, type, payload, length);
  case PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE:
  case PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP:
  case PACKET_TYPE_CRYPTO_AUTH_FAILED:
    return crypto_handshake_client_complete(ctx, transport, type, payload, length);
  default:
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected discovery handshake packet: %u", type);
  }
}
