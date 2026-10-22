#include <criterion/criterion.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/crc32.h>
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
  wire_t *wire = transport->user_data;
  if (wire->failure)
    return wire->failure;
  cr_assert_leq(length, sizeof(wire->wire));
  memcpy(wire->wire, data, length);
  wire->length = length;
  wire->sends++;
  return ASCIICHAT_OK;
}

static asciichat_error_t recv_wire(acip_transport_t *transport, void **data, size_t *length, void **allocated) {
  wire_t *wire = transport->user_data;
  if (wire->failure)
    return wire->failure;
  *allocated = buffer_alloc(NULL, wire->length ? wire->length : 1);
  cr_assert_not_null(*allocated);
  memcpy(*allocated, wire->wire, wire->length);
  *data = *allocated;
  *length = wire->length;
  return ASCIICHAT_OK;
}

static bool connected(acip_transport_t *transport) {
  return ((wire_t *)transport->user_data)->connected;
}
static socket_t no_socket(acip_transport_t *transport) {
  (void)transport;
  return INVALID_SOCKET_VALUE;
}
static const acip_transport_methods_t methods = {
    .send = send_wire, .recv = recv_wire, .is_connected = connected, .get_socket = no_socket};

static void client_ping(void *context) {
  ((wire_t *)context)->callbacks++;
}
static void stream_start(uint32_t flags, void *client, void *context) {
  wire_t *wire = context;
  wire->callbacks++;
  wire->streams = flags;
  wire->client = client;
}
static void server_ping(void *client, void *context) {
  stream_start(0, client, context);
}

Test(acip_roundtrip, authenticated_envelopes_validate_inner_boundaries_before_dispatch) {
  crypto_context_t sender, receiver;
  cr_assert_eq(crypto_init(&sender), CRYPTO_OK);
  cr_assert_eq(crypto_init(&receiver), CRYPTO_OK);
  uint8_t sender_key[32], receiver_key[32];
  cr_assert_eq(crypto_get_public_key(&sender, sender_key), CRYPTO_OK);
  cr_assert_eq(crypto_get_public_key(&receiver, receiver_key), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&sender, receiver_key), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&receiver, sender_key), CRYPTO_OK);
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire, .crypto_ctx = &receiver};
  acip_client_callbacks_t client = {.on_ping = client_ping, .app_ctx = &wire};
  acip_server_callbacks_t server = {.on_ping = server_ping, .app_ctx = &wire};
  for (unsigned peer = 0; peer < 2; ++peer) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
      uint8_t plaintext[sizeof(packet_header_t) + 4] = {0};
      packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                                .type = HOST_TO_NET_U16(PACKET_TYPE_PING),
                                .length = HOST_TO_NET_U32(scenario == 2 ? UINT32_MAX : 4)};
      memcpy(plaintext, &header, sizeof(header));
      uint8_t ciphertext[2048];
      size_t ciphertext_length;
      size_t length = scenario == 1 ? sizeof(header) - 1 : sizeof(plaintext);
      cr_assert_eq(crypto_encrypt(&sender, plaintext, length, ciphertext, sizeof(ciphertext), &ciphertext_length),
                   CRYPTO_OK);
      if (scenario == 3)
        ciphertext[ciphertext_length - 1] ^= 1;
      cr_assert_eq(packet_send_via_transport(&transport, PACKET_TYPE_ENCRYPTED, ciphertext, ciphertext_length, 0),
                   ASCIICHAT_OK);
      wire.callbacks = 0;
      asciichat_error_t result = peer ? acip_server_receive_and_dispatch(&transport, &wire, &server)
                                      : acip_client_receive_and_dispatch(&transport, &client);
      cr_assert_eq(result, scenario ? ERROR_CRYPTO : ASCIICHAT_OK, "peer %u scenario %u", peer, scenario);
      cr_assert_eq(wire.callbacks, scenario ? 0 : 1);
    }
  }
  crypto_destroy(&sender);
  crypto_destroy(&receiver);
}

static void check_packet(wire_t *wire, packet_type_t type, const void *payload, size_t length) {
  packet_header_t header;
  cr_assert_eq(wire->length, sizeof(header) + length);
  memcpy(&header, wire->wire, sizeof(header));
  cr_assert_eq(NET_TO_HOST_U64(header.magic), PACKET_MAGIC);
  cr_assert_eq(NET_TO_HOST_U16(header.type), type);
  cr_assert_eq(NET_TO_HOST_U32(header.length), length);
  cr_assert_eq(NET_TO_HOST_U32(header.crc32), asciichat_crc32(payload, length));
  if (length)
    cr_assert_arr_eq(wire->wire + sizeof(header), payload, length);
}

Test(acip_roundtrip, control_senders_produce_network_order_packets_and_receiver_dispatches) {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  int client;
  acip_server_callbacks_t callbacks = {
      .on_stream_start = stream_start, .on_stream_stop = stream_start, .app_ctx = &wire};
  const uint8_t values[] = {0, 1, 3, 0xff};
  for (size_t i = 0; i < sizeof(values); ++i) {
    uint32_t payload = HOST_TO_NET_U32(values[i]);
    cr_assert_eq(acip_send_stream_start(&transport, values[i]), ASCIICHAT_OK);
    check_packet(&wire, PACKET_TYPE_STREAM_START, &payload, sizeof(payload));
    wire.callbacks = 0;
    cr_assert_eq(acip_server_receive_and_dispatch(&transport, &client, &callbacks), ASCIICHAT_OK);
    cr_assert_eq(wire.callbacks, 1);
    cr_assert_eq(wire.streams, values[i]);
    cr_assert_eq(wire.client, &client);
    cr_assert_eq(acip_send_stream_stop(&transport, values[i]), ASCIICHAT_OK);
    check_packet(&wire, PACKET_TYPE_STREAM_STOP, &payload, sizeof(payload));
    cr_assert_eq(acip_server_receive_and_dispatch(&transport, &client, &callbacks), ASCIICHAT_OK);
    cr_assert_eq(wire.callbacks, 2);
  }
  cr_assert_eq(acip_send_clear_console(&transport), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0);
  const uint8_t capabilities[] = {0, 1, 0xfe, 0xff};
  cr_assert_eq(acip_send_capabilities(&transport, capabilities, sizeof(capabilities)), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_CLIENT_CAPABILITIES, capabilities, sizeof(capabilities));
  protocol_version_packet_t version = {0};
  cr_assert_eq(acip_send_protocol_version(&transport, &version), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version));
  server_state_packet_t state = {0};
  cr_assert_eq(acip_send_server_state(&transport, &state), ASCIICHAT_OK);
  check_packet(&wire, PACKET_TYPE_SERVER_STATE, &state, sizeof(state));
}

Test(acip_roundtrip, client_receive_returns_owned_payload_and_dispatches_after_release) {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  uint8_t payload[] = {0, 1, 0xff};
  cr_assert_eq(packet_send_via_transport(&transport, PACKET_TYPE_PING, payload, sizeof(payload), 0), ASCIICHAT_OK);
  packet_envelope_t envelope;
  cr_assert_eq(acip_client_receive_packet(&transport, &envelope), ASCIICHAT_OK);
  cr_assert_eq(envelope.type, PACKET_TYPE_PING);
  cr_assert_eq(envelope.len, sizeof(payload));
  cr_assert_not_null(envelope.allocated_buffer);
  cr_assert_arr_eq(envelope.data, payload, sizeof(payload));
  buffer_release(envelope.allocated_buffer);
  acip_client_callbacks_t callbacks = {.on_ping = client_ping, .app_ctx = &wire};
  cr_assert_eq(acip_client_receive_and_dispatch(&transport, &callbacks), ASCIICHAT_OK);
  cr_assert_eq(wire.callbacks, 1);
}

Test(acip_roundtrip, incomplete_headers_and_inconsistent_lengths_never_dispatch) {
  wire_t wire = {.connected = true};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  acip_server_callbacks_t server = {.on_stream_start = stream_start, .app_ctx = &wire};
  acip_client_callbacks_t client = {.on_ping = client_ping, .app_ctx = &wire};
  packet_envelope_t envelope;
  for (size_t length = 0; length < sizeof(packet_header_t); ++length) {
    wire.length = length;
    cr_assert_eq(acip_client_receive_packet(&transport, &envelope), ERROR_NETWORK);
    cr_assert_null(envelope.allocated_buffer);
    cr_assert_eq(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK);
  }
  uint32_t payload = HOST_TO_NET_U32(3);
  cr_assert_eq(acip_send_stream_start(&transport, 3), ASCIICHAT_OK);
  packet_header_t header;
  memcpy(&header, wire.wire, sizeof(header));
  const uint32_t wrong_lengths[] = {0, 3, 5, UINT32_MAX};
  for (size_t i = 0; i < sizeof(wrong_lengths) / sizeof(wrong_lengths[0]); ++i) {
    header.length = HOST_TO_NET_U32(wrong_lengths[i]);
    memcpy(wire.wire, &header, sizeof(header));
    cr_assert_eq(acip_client_receive_packet(&transport, &envelope), ERROR_NETWORK);
    cr_assert_eq(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK);
    cr_assert_eq(wire.callbacks, 0);
  }
  cr_assert_eq(packet_send_via_transport(&transport, (packet_type_t)0xffff, &payload, sizeof(payload), 0),
               ASCIICHAT_OK);
  cr_assert_eq(acip_client_receive_and_dispatch(&transport, &client), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_INVALID_PARAM);
  cr_assert_eq(wire.callbacks, 0);
}

Test(acip_roundtrip, missing_context_disconnection_and_backend_errors) {
  wire_t wire = {0};
  acip_transport_t transport = {.methods = &methods, .user_data = &wire};
  packet_envelope_t envelope;
  acip_client_callbacks_t client = {0};
  acip_server_callbacks_t server = {0};
  cr_assert_eq(acip_client_receive_packet(NULL, &envelope), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_client_receive_packet(&transport, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_client_receive_and_dispatch(&transport, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_server_receive_and_dispatch(NULL, NULL, &server), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_server_receive_and_dispatch(&transport, NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_client_receive_packet(&transport, &envelope), ERROR_NETWORK);
  cr_assert_eq(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK);
  wire.connected = true;
  const asciichat_error_t failures[] = {ERROR_NETWORK_TIMEOUT, ERROR_NETWORK, ERROR_CRYPTO};
  for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
    wire.failure = failures[i];
    cr_assert_eq(acip_client_receive_and_dispatch(&transport, &client), failures[i]);
    cr_assert_eq(acip_server_receive_and_dispatch(&transport, NULL, &server), ERROR_NETWORK);
    cr_assert_eq(acip_send_stream_start(&transport, 3), failures[i]);
    cr_assert_eq(wire.sends, 0);
    cr_assert_eq(wire.callbacks, 0);
  }
  cr_assert_eq(acip_send_stream_start(NULL, 3), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_send_stream_stop(NULL, 3), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_send_clear_console(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_send_capabilities(&transport, NULL, 0), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_send_protocol_version(&transport, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_send_server_state(&transport, NULL), ERROR_INVALID_PARAM);
}
