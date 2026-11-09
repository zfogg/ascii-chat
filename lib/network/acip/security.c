#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/tooling/defer/defer.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/common/protocol_constants.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

void acip_security_cleanup(acip_security_t *security) {
  if (!security)
    return;
  SAFE_FREE(security->handshake.client_whitelist);
  crypto_handshake_destroy(&security->handshake);
}

asciichat_error_t acip_security_start(acip_security_t *security, acip_transport_t *transport) {
  if (!security || !transport || security->mode > ACIP_CRYPTO_FULL ||
      security->handshake.state != CRYPTO_HANDSHAKE_INIT)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid peer security initialization");
  security->handshake.addressless_transport = true;
  transport->crypto_ctx = &security->handshake.crypto_ctx;
  security->handshake.crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(security->mode);
  if (security->handshake.is_server)
    return ASCIICHAT_OK;
  protocol_version_packet_t version;
  protocol_version_packet_init(&version, security->mode);
  return packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
}

asciichat_error_t acip_security_receive(acip_security_t *security, acip_transport_t *transport, packet_type_t type,
                                        const void *payload, size_t length) {
  if (!security || !transport || (!payload && length) || security->ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "Unexpected peer security packet");
  crypto_handshake_context_t *ctx = &security->handshake;
  asciichat_error_t result = ASCIICHAT_OK;
  if (!security->version_received) {
    if (type != PACKET_TYPE_PROTOCOL_VERSION || length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected peer security negotiation before media");
    protocol_version_packet_t version;
    memcpy(&version, payload, sizeof(version));
    uint8_t mode = version.supports_encryption;
    if (NET_TO_HOST_U16(version.protocol_version) != PROTOCOL_VERSION_MAJOR ||
        NET_TO_HOST_U16(version.protocol_revision) != PROTOCOL_VERSION_MINOR ||
        NET_TO_HOST_U16(version.protocol_patch) != PROTOCOL_VERSION_PATCH || mode > ACIP_CRYPTO_FULL)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported peer security protocol");
    if (ctx->is_server) {
      // Match secure WebSocket policy: defaults do not override a client's explicit no-encrypt.
      if ((ctx->has_password || ctx->require_client_auth) && !ACIP_CRYPTO_HAS_AUTH(mode))
        return SET_ERRNO(ERROR_CRYPTO_AUTH, "Peer omitted required authentication");
      if (security->require_encryption && !ACIP_CRYPTO_HAS_ENCRYPT(mode))
        return SET_ERRNO(ERROR_CRYPTO, "Peer omitted required encryption");
      if (security->forbid_encryption && ACIP_CRYPTO_HAS_ENCRYPT(mode))
        return SET_ERRNO(ERROR_CRYPTO, "Peer requires encryption but local policy disables it");
      version.supports_encryption = mode;
      result = packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
      if (result != ASCIICHAT_OK)
        return result;
    } else if ((mode & security->mode) != security->mode) {
      return SET_ERRNO(ERROR_CRYPTO, "Peer attempted to downgrade requested security");
    }
    if (security->forbid_encryption && ACIP_CRYPTO_HAS_ENCRYPT(mode))
      return SET_ERRNO(ERROR_CRYPTO, "Peer requires encryption but local policy disables it");
    security->mode = mode;
    security->version_received = true;
    ctx->crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
    if (mode == ACIP_CRYPTO_NONE) {
      ctx->state = CRYPTO_HANDSHAKE_DISABLED;
      security->ready = true;
      return ASCIICHAT_OK;
    }
    if (!ctx->is_server)
      return ASCIICHAT_OK;
    result = crypto_handshake_server_send_parameters(ctx, transport);
    if (result == ASCIICHAT_OK)
      result = crypto_handshake_server_start(ctx, transport);
    return result;
  }
  if (ctx->is_server) {
    if (ctx->state == CRYPTO_HANDSHAKE_KEY_EXCHANGE)
      result = crypto_handshake_server_auth_challenge(ctx, transport, type, payload, length);
    else if (ctx->state == CRYPTO_HANDSHAKE_AUTHENTICATING)
      result = crypto_handshake_server_complete(ctx, transport, type, payload, length);
    else
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected peer authentication state");
  } else {
    switch (type) {
    case PACKET_TYPE_CRYPTO_PARAMETERS:
      if (security->parameters_received || ctx->state != CRYPTO_HANDSHAKE_INIT ||
          length != sizeof(crypto_parameters_packet_t))
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid peer crypto parameters");
      result = crypto_handshake_set_parameters(ctx, payload);
      if (result == ASCIICHAT_OK && ctx->crypto_ctx.encrypt_data != !!ACIP_CRYPTO_HAS_ENCRYPT(security->mode))
        return SET_ERRNO(ERROR_CRYPTO, "Peer cipher does not match negotiated policy");
      security->parameters_received = result == ASCIICHAT_OK;
      break;
    case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT:
      if (!security->parameters_received)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Missing peer crypto parameters");
      if (ctx->verify_server_key && length == ctx->crypto_ctx.public_key_size)
        return SET_ERRNO(ERROR_CRYPTO_AUTH, "Peer omitted the required identity key");
      result = crypto_handshake_client_key_exchange(ctx, transport, type, payload, length);
      break;
    case PACKET_TYPE_CRYPTO_AUTH_CHALLENGE:
      result = crypto_handshake_client_auth_response(ctx, transport, type, payload, length);
      break;
    case PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE:
      if (ctx->has_password || ctx->client_private_key.type == KEY_TYPE_ED25519)
        return SET_ERRNO(ERROR_CRYPTO_AUTH, "Peer skipped required mutual authentication");
      result = crypto_handshake_client_complete(ctx, transport, type, payload, length);
      break;
    case PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP:
    case PACKET_TYPE_CRYPTO_AUTH_FAILED:
      result = crypto_handshake_client_complete(ctx, transport, type, payload, length);
      break;
    default:
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Application packet before peer authentication");
    }
  }
  if (result == ASCIICHAT_OK)
    security->ready = ctx->state == CRYPTO_HANDSHAKE_READY;
  else
    ctx->state = CRYPTO_HANDSHAKE_FAILED;
  if (result == ASCIICHAT_OK && security->ready)
    result = acip_security_activate(transport);
  return result;
}

static const packet_type_t rekey_types[] = {0,
                                            PACKET_TYPE_CRYPTO_REKEY_REQUEST,
                                            PACKET_TYPE_CRYPTO_REKEY_RESPONSE,
                                            PACKET_TYPE_CRYPTO_REKEY_COMPLETE,
                                            PACKET_TYPE_CRYPTO_EPOCH_MARKER,
                                            PACKET_TYPE_CRYPTO_EPOCH_ACK,
                                            PACKET_TYPE_CRYPTO_REKEY_NEEDED};

static crypto_rekey_message_t rekey_message(uint16_t type) {
  for (unsigned i = 1; i < sizeof(rekey_types) / sizeof(rekey_types[0]); ++i)
    if (rekey_types[i] == type)
      return (crypto_rekey_message_t)i;
  return CRYPTO_REKEY_NONE;
}

bool acip_security_rekey_packet(uint16_t type) {
  return rekey_message(type) != CRYPTO_REKEY_NONE;
}

asciichat_error_t acip_security_rekey_encode(unsigned flow, crypto_rekey_message_t type, const void *payload,
                                             size_t length, void *packet, size_t capacity, size_t *written) {
  if (type <= CRYPTO_REKEY_NONE || type > CRYPTO_REKEY_NEEDED || flow >= CRYPTO_RECORD_CHANNELS)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rekey message");
  return packet_encode(rekey_types[type], payload, length, flow, packet, capacity, written);
}

asciichat_error_t acip_security_rekey_receive(crypto_rekey_session_t *session, acip_traffic_class_t flow,
                                              acip_delivery_t delivery, const void *packet, size_t length, uint64_t now,
                                              bool *consumed) {
  packet_header_t header;
  asciichat_error_t result = packet_validate(packet, length, 0, &header);
  if (result != ASCIICHAT_OK)
    return result;
  return crypto_rekey_session_receive(session, flow, delivery, rekey_message(NET_TO_HOST_U16(header.type)),
                                      (const uint8_t *)packet + sizeof(header), length - sizeof(header), now, consumed);
}

asciichat_error_t acip_security_rekey_seal(crypto_record_t *records, unsigned flow, crypto_rekey_message_t message,
                                           const void *payload, size_t length, uint8_t **wire, size_t *written) {
  uint8_t packet[sizeof(packet_header_t) + CRYPTO_PUBLIC_KEY_SIZE];
  size_t packet_length = 0;
  asciichat_error_t result =
      acip_security_rekey_encode(flow, message, payload, length, packet, sizeof(packet), &packet_length);
  return result == ASCIICHAT_OK ? acip_packet_seal(records, (acip_traffic_class_t)flow, ACIP_DELIVERY_RELIABLE, packet,
                                                   packet_length, wire, written)
                                : result;
}

asciichat_error_t acip_security_activate(acip_transport_t *transport) {
  if (!transport)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing secure transport");
  crypto_context_t *crypto = transport->crypto_ctx;
  if (!crypto || !crypto->encrypt_data)
    return ASCIICHAT_OK;
  crypto_session_t *session = NULL;
  asciichat_error_t result = crypto_session_lock(crypto, &session);
  if (result == ASCIICHAT_OK) {
    crypto_session_unlock(session);
    atomic_store_bool(&transport->packet_security_active, true);
  }
  return result;
}

asciichat_error_t acip_security_decrypt(crypto_context_t *crypto, unsigned flow, unsigned delivery, const void *payload,
                                        size_t length, uint8_t **output, size_t *written, uint64_t *sequence,
                                        bool *consumed) {
  if (!output || !written || !consumed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing security receive output");
  *output = NULL;
  *written = 0;
  *consumed = false;
  crypto_session_t *session = NULL;
  asciichat_error_t result = crypto_session_lock(crypto, &session);
  if (result != ASCIICHAT_OK)
    return result;
  result = acip_packet_decrypt_payload(&session->records, flow, delivery, payload, length, output, written, sequence);
  if (result == ASCIICHAT_OK) {
    packet_header_t header;
    memcpy(&header, *output, sizeof(header));
    result = crypto_session_receive(session, session->records.last_receive_channel, delivery,
                                    rekey_message(NET_TO_HOST_U16(header.type)), *output + sizeof(header),
                                    *written - sizeof(header), time_get_ns(), consumed);
  }
  crypto_session_unlock(session);
  if (result != ASCIICHAT_OK) {
    buffer_release(*output);
    *output = NULL;
    *written = 0;
  }
  return result;
}

asciichat_error_t acip_security_next_control(crypto_context_t *crypto, acip_traffic_class_t *flow, uint8_t **wire,
                                             size_t *length) {
  if (!flow || !wire || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing security output");
  *wire = NULL;
  *length = 0;
  crypto_session_t *session = NULL;
  asciichat_error_t result = crypto_session_lock(crypto, &session);
  if (result != ASCIICHAT_OK)
    return result;
  defer(crypto_session_unlock(session));
  result = crypto_session_tick(session, time_get_ns());
  if (result == ASCIICHAT_OK && session->count) {
    crypto_session_message_t *message = &session->messages[session->head];
    *flow = (acip_traffic_class_t)message->channel;
    result = acip_security_rekey_seal(&session->records, message->channel, message->type, message->payload,
                                      message->length, wire, length);
    if (result == ASCIICHAT_OK) {
      memset(message, 0, sizeof(*message));
      session->head = (session->head + 1) % CRYPTO_SESSION_MESSAGES;
      --session->count;
    }
  }
  return result;
}

asciichat_error_t acip_security_flush(acip_transport_t *transport) {
  if (!transport || !atomic_load_bool(&transport->packet_security_active))
    return ASCIICHAT_OK;
  for (unsigned i = 0; i < CRYPTO_SESSION_MESSAGES; ++i) {
    uint8_t *wire = NULL;
    size_t length = 0;
    acip_traffic_class_t channel = ACIP_TRAFFIC_CONTROL;
    asciichat_error_t result = acip_security_next_control(transport->crypto_ctx, &channel, &wire, &length);
    if (result != ASCIICHAT_OK || !wire)
      return result;
    defer(buffer_release(wire));
    socket_buffer_t slice = {wire, length};
    acip_send_options_t options = {.traffic_class = channel, .delivery = ACIP_DELIVERY_RELIABLE};
    if (transport->methods->sendv_ex)
      result = transport->methods->sendv_ex(transport, &slice, 1, &options);
    else if (transport->methods->sendv)
      result = transport->methods->sendv(transport, &slice, 1);
    else
      result = transport->methods->send(transport, wire, length);
    if (result != ASCIICHAT_OK)
      return result;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t acip_security_request_rekey(acip_transport_t *transport) {
  if (!transport || !transport->crypto_ctx || !transport->crypto_ctx->encrypt_data)
    return SET_ERRNO(ERROR_INVALID_STATE, "An encrypted authenticated session is required for rotation");
  crypto_session_t *session = NULL;
  asciichat_error_t result = crypto_session_lock(transport->crypto_ctx, &session);
  if (result == ASCIICHAT_OK) {
    result = crypto_rekey_session_request(&session->rotation, time_get_ns());
    crypto_session_unlock(session);
  }
  return result;
}

asciichat_error_t acip_security_seal(crypto_context_t *crypto, acip_traffic_class_t flow, acip_delivery_t delivery,
                                     const void *packet, size_t length, uint8_t **output, size_t *written) {
  crypto_session_t *session = NULL;
  asciichat_error_t result = crypto_session_lock(crypto, &session);
  if (result == ASCIICHAT_OK) {
    result = acip_packet_seal(&session->records, flow, delivery, packet, length, output, written);
    crypto_session_unlock(session);
  }
  return result;
}

asciichat_error_t acip_security_open(crypto_context_t *crypto, acip_traffic_class_t flow, acip_delivery_t delivery,
                                     const void *packet, size_t length, uint8_t **output, size_t *written,
                                     uint64_t *sequence, bool *consumed) {
  packet_header_t header;
  asciichat_error_t result = packet_validate(packet, length, 0, &header);
  if (result != ASCIICHAT_OK)
    return result;
  if (NET_TO_HOST_U16(header.type) != PACKET_TYPE_ENCRYPTED)
    return SET_ERRNO(ERROR_CRYPTO, "Expected authenticated session record");
  return acip_security_decrypt(crypto, flow, delivery, (const uint8_t *)packet + sizeof(header),
                               length - sizeof(header), output, written, sequence, consumed);
}
