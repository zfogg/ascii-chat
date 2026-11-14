#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

typedef struct {
  uint8_t wire[4096];
  size_t length;
  unsigned sends, callbacks;
  uint32_t streams;
  bool connected;
  asciichat_error_t failure;
  void *client;
} wire_t;

static asciichat_error_t send_wire(acip_transport_t *transport, const void *data, size_t length) {
  wire_t *wire = (wire_t *)transport->user_data;
  if (wire->failure)
    return wire->failure;
  ASCIICHAT_REQUIRE_LE(length, sizeof(wire->wire));
  memcpy(wire->wire, data, length);
  wire->length = length;
  wire->sends++;
  return ASCIICHAT_OK;
}

static asciichat_error_t recv_wire(acip_transport_t *transport, void **data, size_t *length, void **allocated) {
  wire_t *wire = (wire_t *)transport->user_data;
  if (wire->failure)
    return wire->failure;
  if (!wire->connected)
    return ERROR_NETWORK;
  *allocated = buffer_alloc(NULL, wire->length ? wire->length : 1);
  ASCIICHAT_REQUIRE_NOT_NULL(*allocated);
  memcpy(*allocated, wire->wire, wire->length);
  *data = *allocated;
  *length = wire->length;
  return ASCIICHAT_OK;
}

static bool connected(acip_transport_t *transport) {
  return ((wire_t *)transport->user_data)->connected;
}
static bool pending(acip_transport_t *transport) {
  return ((wire_t *)transport->user_data)->length != 0;
}
static socket_t no_socket(acip_transport_t *transport) {
  (void)transport;
  return INVALID_SOCKET_VALUE;
}
static const acip_transport_methods_t methods = {.send = send_wire,
                                                 .recv = recv_wire,
                                                 .is_connected = connected,
                                                 .has_pending_data = pending,
                                                 .get_socket = no_socket};

static void client_ping(void *context) {
  ((wire_t *)context)->callbacks++;
}
static void stream_start(uint32_t flags, void *client, void *context) {
  wire_t *wire = (wire_t *)context;
  wire->callbacks++;
  wire->streams = flags;
  wire->client = client;
}
static void server_ping(void *client, void *context) {
  stream_start(0, client, context);
}

TEST_CASE("acip_roundtrip/authenticated_envelopes_validate_inner_boundaries_before_dispatch", "[acip_roundtrip]") {
  crypto_context_t sender, receiver;
  ASCIICHAT_REQUIRE_EQ(crypto_init(&sender), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&receiver), CRYPTO_OK);
  uint8_t sender_key[32], receiver_key[32];
  ASCIICHAT_REQUIRE_EQ(crypto_get_public_key(&sender, sender_key), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_get_public_key(&receiver, receiver_key), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&sender, receiver_key), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&receiver, sender_key), CRYPTO_OK);
  sender.handshake_complete = receiver.handshake_complete = true;
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire, .crypto_ctx = &receiver};
  acip_client_callbacks_t client = {.on_ping = client_ping, .app_ctx = &wire};
  acip_server_callbacks_t server = {.on_ping = server_ping, .app_ctx = &wire};
  for (unsigned peer = 0; peer < 2; ++peer) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
      uint8_t plaintext[sizeof(packet_header_t) + 4] = {0};
      packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                                .type = HOST_TO_NET_U16(PACKET_TYPE_PING),
                                .length = HOST_TO_NET_U32(scenario == 2 ? UINT32_MAX : 4),
                                .crc32 = HOST_TO_NET_U32(asciichat_crc32(plaintext + sizeof(header), 4))};
      memcpy(plaintext, &header, sizeof(header));
      uint8_t ciphertext[2048];
      size_t ciphertext_length;
      size_t length = scenario == 1 ? sizeof(header) - 1 : sizeof(plaintext);
      crypto_session_t *session = NULL;
      ASCIICHAT_REQUIRE_EQ(crypto_session_lock(&sender, &session), ASCIICHAT_OK);
      ASCIICHAT_REQUIRE_EQ(crypto_record_seal_into(&session->records, ACIP_TRAFFIC_CONTROL, CRYPTO_RECORD_RELIABLE, plaintext,
                                           length, ciphertext, sizeof(ciphertext), &ciphertext_length),
                   ASCIICHAT_OK);
      crypto_session_unlock(session);
      if (scenario == 3)
        ciphertext[ciphertext_length - 1] ^= 1;
      ASCIICHAT_REQUIRE_EQ(packet_send_via_transport(&transport, PACKET_TYPE_ENCRYPTED, ciphertext, ciphertext_length, 0),
                   ASCIICHAT_OK);
      wire.callbacks = 0;
      asciichat_error_t result = peer ? acip_server_receive_and_dispatch(&transport, &wire, &server)
                                      : acip_client_receive_and_dispatch(&transport, &client);
      asciichat_error_t expected = !scenario ? ASCIICHAT_OK : scenario == 3 ? ERROR_CRYPTO : ERROR_NETWORK_PROTOCOL;
      ASCIICHAT_REQUIRE_EQ(result, expected, "peer %u scenario %u result %d", peer, scenario, result);
      ASCIICHAT_REQUIRE_EQ(wire.callbacks, scenario ? 0 : 1);
    }
  }
  crypto_destroy(&sender);
  crypto_destroy(&receiver);
}

static void check_packet(wire_t *wire, packet_type_t type, const void *payload, size_t length) {
  packet_header_t header;
  ASCIICHAT_REQUIRE_EQ(wire->length, sizeof(header) + length);
  memcpy(&header, wire->wire, sizeof(header));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U64(header.magic), PACKET_MAGIC);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.type), type);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.length), length);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.crc32), asciichat_crc32(payload, length));
  if (length)
    ASCIICHAT_REQUIRE_ARRAY_EQ(wire->wire + sizeof(header), payload, length);
}

TEST_CASE("acip_roundtrip/control_senders_produce_network_order_packets_and_receiver_dispatches", "[acip_roundtrip]") {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  int client;
  acip_server_callbacks_t callbacks = {
      .on_stream_start = stream_start, .on_stream_stop = stream_start, .app_ctx = &wire};
  const uint8_t values[] = {0, 1, 3, 0xff};
  for (size_t i = 0; i < sizeof(values); ++i) {
    uint32_t payload = HOST_TO_NET_U32(values[i]);
    ASCIICHAT_REQUIRE_EQ(acip_send_stream_start(&transport, values[i]), ASCIICHAT_OK);
    check_packet(&wire, PACKET_TYPE_STREAM_START, &payload, sizeof(payload));
    wire.callbacks = 0;
    ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, &client, &callbacks), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(wire.callbacks, 1);
    ASCIICHAT_REQUIRE_EQ(wire.streams, values[i]);
    ASCIICHAT_REQUIRE_EQ(wire.client, &client);
    ASCIICHAT_REQUIRE_EQ(acip_send_stream_stop(&transport, values[i]), ASCIICHAT_OK);
    check_packet(&wire, PACKET_TYPE_STREAM_STOP, &payload, sizeof(payload));
    ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, &client, &callbacks), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(wire.callbacks, 2);
  }
  ASCIICHAT_REQUIRE_EQ(acip_send_clear_console(&transport), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0);
  const uint8_t capabilities[] = {0, 1, 0xfe, 0xff};
  ASCIICHAT_REQUIRE_EQ(acip_send_capabilities(&transport, capabilities, sizeof(capabilities)), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_CLIENT_CAPABILITIES, capabilities, sizeof(capabilities));
  protocol_version_packet_t version = {0};
  ASCIICHAT_REQUIRE_EQ(acip_send_protocol_version(&transport, &version), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version));
  server_state_packet_t state = {0};
  ASCIICHAT_REQUIRE_EQ(acip_send_server_state(&transport, &state), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_SERVER_STATE, &state, sizeof(state));
}

TEST_CASE("acip_roundtrip/client_receive_returns_owned_payload_and_dispatches_after_release", "[acip_roundtrip]") {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  uint8_t payload[] = {0, 1, 0xff};
  ASCIICHAT_REQUIRE_EQ(packet_send_via_transport(&transport, PACKET_TYPE_PING, payload, sizeof(payload), 0), ASCIICHAT_OK);
  packet_envelope_t envelope;
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(&transport, &envelope), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(envelope.type, PACKET_TYPE_PING);
  ASCIICHAT_REQUIRE_EQ(envelope.len, sizeof(payload));
  ASCIICHAT_REQUIRE_NOT_NULL(envelope.allocated_buffer);
  ASCIICHAT_REQUIRE_ARRAY_EQ(envelope.data, payload, sizeof(payload));
  buffer_release(envelope.allocated_buffer);
  acip_client_callbacks_t callbacks = {.on_ping = client_ping, .app_ctx = &wire};
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_and_dispatch(&transport, &callbacks), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(wire.callbacks, 1);
}

TEST_CASE("acip_roundtrip/client_drains_queued_packet_after_disconnect", "[acip_roundtrip]") {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  ASCIICHAT_REQUIRE_EQ(packet_send_via_transport(&transport, PACKET_TYPE_PING, NULL, 0, 0), ASCIICHAT_OK);
  wire.connected = false;
  packet_envelope_t envelope;
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(&transport, &envelope), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(envelope.type, PACKET_TYPE_PING);
  buffer_release(envelope.allocated_buffer);
}

TEST_CASE("acip_roundtrip/incomplete_headers_and_inconsistent_lengths_never_dispatch", "[acip_roundtrip]") {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  acip_server_callbacks_t server = {.on_stream_start = stream_start, .app_ctx = &wire};
  acip_client_callbacks_t client = {.on_ping = client_ping, .app_ctx = &wire};
  packet_envelope_t envelope;
  for (size_t length = 0; length < sizeof(packet_header_t); ++length) {
    wire.length = length;
    ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(&transport, &envelope), ERROR_NETWORK_PROTOCOL);
    ASCIICHAT_REQUIRE_NULL(envelope.allocated_buffer);
    ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK_PROTOCOL);
  }
  uint32_t payload = HOST_TO_NET_U32(3);
  ASCIICHAT_REQUIRE_EQ(acip_send_stream_start(&transport, 3), ASCIICHAT_OK);
  packet_header_t header;
  memcpy(&header, wire.wire, sizeof(header));
  const uint32_t wrong_lengths[] = {0, 3, 5, UINT32_MAX};
  for (size_t i = 0; i < sizeof(wrong_lengths) / sizeof(wrong_lengths[0]); ++i) {
    header.length = HOST_TO_NET_U32(wrong_lengths[i]);
    memcpy(wire.wire, &header, sizeof(header));
    ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(&transport, &envelope), ERROR_NETWORK_PROTOCOL);
    ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK_PROTOCOL);
    ASCIICHAT_REQUIRE_EQ(wire.callbacks, 0);
  }
  ASCIICHAT_REQUIRE_EQ(packet_send_via_transport(&transport, (packet_type_t)0xffff, &payload, sizeof(payload), 0),
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_and_dispatch(&transport, &client), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(wire.callbacks, 0);
}

TEST_CASE("acip_roundtrip/missing_context_disconnection_and_backend_errors", "[acip_roundtrip]") {
  wire_t wire = {0};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  packet_envelope_t envelope;
  acip_client_callbacks_t client = {0};
  acip_server_callbacks_t server = {0};
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(NULL, &envelope), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(&transport, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_and_dispatch(&transport, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(NULL, NULL, &server), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, NULL, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_client_receive_packet(&transport, &envelope), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK);
  wire.connected = true;
  const asciichat_error_t failures[] = {ERROR_NETWORK_TIMEOUT, ERROR_NETWORK, ERROR_CRYPTO};
  for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
    wire.failure = failures[i];
    ASCIICHAT_REQUIRE_EQ(acip_client_receive_and_dispatch(&transport, &client), failures[i]);
    ASCIICHAT_REQUIRE_EQ(acip_server_receive_and_dispatch(&transport, NULL, &server), failures[i]);
    ASCIICHAT_REQUIRE_EQ(acip_send_stream_start(&transport, 3), failures[i]);
    ASCIICHAT_REQUIRE_EQ(wire.sends, 0);
    ASCIICHAT_REQUIRE_EQ(wire.callbacks, 0);
  }
  ASCIICHAT_REQUIRE_EQ(acip_send_stream_start(NULL, 3), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_send_stream_stop(NULL, 3), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_send_clear_console(NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_send_capabilities(&transport, NULL, 0), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_send_protocol_version(&transport, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(acip_send_server_state(&transport, NULL), ERROR_INVALID_PARAM);
}
