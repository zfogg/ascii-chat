#include <ascii-chat/crypto/policy.h>
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

#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>

#include <ascii-chat/network/errors.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static asciichat_error_t acds_client_handshake(acds_client_t *client) {
  client->handshake_ctx = network_connection_handshake(client->connection);
  asciichat_error_t result = crypto_handshake_init("acds_client", client->handshake_ctx, false);
  if (result != ASCIICHAT_OK)
    return result;
  crypto_policy_t policy = client->config.security_policy;
  policy.transport_secure = network_connection_secure(client->connection);
  if (client->config.identity_key && client->config.identity_key[0]) {
    result = parse_private_key(client->config.identity_key, &client->handshake_ctx->client_private_key);
    if (result != ASCIICHAT_OK)
      return result;
    client->handshake_ctx->client_public_key.type = KEY_TYPE_ED25519;
    memcpy(client->handshake_ctx->client_public_key.key, client->handshake_ctx->client_private_key.public_key,
           sizeof(client->handshake_ctx->client_private_key.public_key));
    policy.has_identity = true;
  }
  uint8_t mode;
  result = crypto_policy_resolve(&policy, &mode);
  if (result != ASCIICHAT_OK)
    return result;
  return network_client_negotiate(client->transport, client->handshake_ctx, mode, NETWORK_PROTOCOL_DISCOVERY);
}

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
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
  config->timeout_ms = 5 * MS_PER_SEC_INT;
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
  client->connected = false;

  asciichat_error_t pconn_result =
      network_connection_open("acds", config->server_address, config->server_port, &client->connection, NULL);
  if (pconn_result == ASCIICHAT_OK) {
    client->connected = true;
    client->transport = network_connection_transport(client->connection);

    asciichat_error_t handshake_result = acds_client_handshake(client);
    if (handshake_result != ASCIICHAT_OK) {
      network_connection_destroy(&client->connection);
      client->transport = NULL;
      client->connected = false;
      client->handshake_ctx = NULL;
      return handshake_result;
    }

    network_connection_ready(client->connection);

    /* Register ACDS client with named registry */
    char acds_name[64];
    snprintf(acds_name, sizeof(acds_name), "acds_client:%s:%d", config->server_address, config->server_port);
    NAMED_REGISTER(client, acds_name, "acds_client_t", "0x%tx", NULL);

    log_info("Connected to ACDS server at %s:%d", config->server_address, config->server_port);
    return ASCIICHAT_OK;
  }

  return pconn_result;
}

void acds_client_disconnect(acds_client_t *client) {
  if (!client) {
    return;
  }

  network_connection_destroy(&client->connection);
  client->transport = NULL;
  client->handshake_ctx = NULL;

  client->connected = false;
  log_debug("Disconnected from ACDS server");
}

// ============================================================================
// Session Management
// ============================================================================

static asciichat_error_t client_protocol_send(void *data, packet_type_t type, const void *payload, size_t length) {
  acds_client_t *client = data;
  return packet_send_via_transport(client->transport, type, payload, length, 0);
}
static asciichat_error_t client_protocol_receive(acds_client_t *client, acds_protocol_t *protocol) {
  packet_type_t type;
  void *payload = NULL, *allocation = NULL;
  size_t length = 0;
  asciichat_error_t result = packet_receive_via_transport(client->transport, &type, &payload, &length, &allocation);
  if (result == ASCIICHAT_OK)
    result = acds_protocol_receive(protocol, type, payload, length);
  buffer_release(allocation);
  return result;
}
asciichat_error_t acds_session_create(acds_client_t *client, const acds_session_create_params_t *params,
                                      acds_session_create_result_t *result) {
  if (!client || !client->connected || !params || !result)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery create parameters");
  acds_protocol_t protocol;
  acds_protocol_init(&protocol, client_protocol_send, client);
  asciichat_error_t error = acds_protocol_create(&protocol, params, 0, 0);
  if (error == ASCIICHAT_OK)
    error = client_protocol_receive(client, &protocol);
  if (error == ASCIICHAT_OK) {
    memset(result, 0, sizeof(*result));
    SAFE_STRNCPY(result->session_string, protocol.name, sizeof(result->session_string));
    memcpy(result->session_id, protocol.created.session_id, sizeof(result->session_id));
    result->expires_at = protocol.created.expires_at;
  }
  acds_protocol_destroy(&protocol);
  return error;
}
asciichat_error_t acds_session_lookup(acds_client_t *client, const char *name, acds_session_lookup_result_t *result) {
  if (!client || !client->connected || !name || !result)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery lookup parameters");
  acds_protocol_t protocol;
  acds_protocol_init(&protocol, client_protocol_send, client);
  asciichat_error_t error = acds_protocol_lookup(&protocol, name);
  if (error == ASCIICHAT_OK)
    error = client_protocol_receive(client, &protocol);
  if (error == ASCIICHAT_OK) {
    memset(result, 0, sizeof(*result));
    result->found = protocol.info.found;
    if (result->found) {
      memcpy(result->session_id, protocol.info.session_id, sizeof(result->session_id));
      memcpy(result->host_pubkey, protocol.info.host_pubkey, sizeof(result->host_pubkey));
      result->capabilities = protocol.info.capabilities;
      result->max_participants = protocol.info.max_participants;
      result->current_participants = protocol.info.current_participants;
      result->has_password = protocol.info.has_password;
      result->created_at = protocol.info.created_at;
      result->expires_at = protocol.info.expires_at;
      result->require_server_verify = protocol.info.require_server_verify;
      result->require_client_verify = protocol.info.require_client_verify;
    }
  }
  acds_protocol_destroy(&protocol);
  return error;
}
asciichat_error_t acds_session_join(acds_client_t *client, const acds_session_join_params_t *params,
                                    acds_session_join_result_t *result) {
  if (!client || !client->connected || !params || !result)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid discovery join parameters");
  acds_protocol_t protocol;
  acds_protocol_init(&protocol, client_protocol_send, client);
  memset(result, 0, sizeof(*result));
  asciichat_error_t error = acds_protocol_join(&protocol, params);
  if (error == ASCIICHAT_OK)
    error = client_protocol_receive(client, &protocol);
  result->success = protocol.joined.success;
  result->error_code = protocol.joined.error_code;
  SAFE_STRNCPY(result->error_message, protocol.joined.error_message, sizeof(result->error_message));
  if (error == ASCIICHAT_OK) {
    memcpy(result->participant_id, protocol.joined.participant_id, sizeof(result->participant_id));
    memcpy(result->session_id, protocol.joined.session_id, sizeof(result->session_id));
    result->session_type = protocol.joined.session_type;
    SAFE_STRNCPY(result->server_address, protocol.joined.server_address, sizeof(result->server_address));
    result->server_port = protocol.joined.server_port;
  }
  acds_protocol_destroy(&protocol);
  return error;
}
