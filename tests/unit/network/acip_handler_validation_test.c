#include <criterion/criterion.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

typedef struct {
  unsigned calls;
  uint32_t width, height, flags;
  size_t length;
  packet_type_t type;
  const void *payload;
  void *client;
} observation_t;

static void ascii_frame(const ascii_frame_packet_t *header, const void *data, size_t length, void *context) {
  observation_t *seen = context;
  seen->calls++;
  seen->width = header->width;
  seen->height = header->height;
  seen->flags = header->flags;
  seen->payload = data;
  seen->length = length;
}

static void image_frame(const image_frame_packet_t *header, const void *data, size_t length, void *client,
                        void *context) {
  observation_t *seen = context;
  seen->calls++;
  seen->width = header->width;
  seen->height = header->height;
  seen->payload = data;
  seen->length = length;
  seen->client = client;
}

static void encoded_frame(uint32_t width, uint32_t height, uint8_t flags, const void *data, size_t length, void *client,
                          void *context) {
  observation_t *seen = context;
  seen->calls++;
  seen->width = width;
  seen->height = height;
  seen->flags = flags;
  seen->payload = data;
  seen->length = length;
  seen->client = client;
}

static void client_crypto(packet_type_t type, const void *data, size_t length, void *context) {
  observation_t *seen = context;
  seen->calls++;
  seen->type = type;
  seen->payload = data;
  seen->length = length;
}

static void server_crypto(packet_type_t type, const void *data, size_t length, void *client, void *context) {
  client_crypto(type, data, length, context);
  ((observation_t *)context)->client = client;
}

Test(acip_handlers, ascii_header_is_converted_and_invalid_dimensions_never_reach_callback) {
  acip_transport_t transport = {0};
  observation_t seen = {0};
  acip_client_callbacks_t callbacks = {.on_ascii_frame = ascii_frame, .app_ctx = &seen};
  struct {
    ascii_frame_packet_t header;
    char body[7];
  } packet = {.header = {.width = HOST_TO_NET_U32(3), .height = HOST_TO_NET_U32(2), .flags = HOST_TO_NET_U32(5)},
              .body = {'o', 'n', 'e', '\n', 't', 'w', 'o'}};
  const size_t length = sizeof(packet.header) + sizeof(packet.body);
  cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_ASCII_FRAME, &packet, length, &callbacks),
               ASCIICHAT_OK);
  cr_assert_eq(seen.calls, 1);
  cr_assert_eq(seen.width, 3);
  cr_assert_eq(seen.height, 2);
  cr_assert_eq(seen.flags, 5);
  cr_assert_eq(seen.length, 7);
  cr_assert_eq(memcmp(seen.payload, "one\ntwo", 7), 0);
  const uint32_t invalid[][2] = {{0, 2}, {3, 0}, {16385, 2}, {3, 16385}};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    packet.header.width = HOST_TO_NET_U32(invalid[i][0]);
    packet.header.height = HOST_TO_NET_U32(invalid[i][1]);
    cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_ASCII_FRAME, &packet, length, &callbacks),
                 ERROR_INVALID_PARAM);
    cr_assert_eq(seen.calls, 1);
  }
  cr_assert_eq(
      acip_handle_client_packet(&transport, PACKET_TYPE_ASCII_FRAME, &packet, sizeof(packet.header) - 1, &callbacks),
      ERROR_INVALID_PARAM);
  callbacks.on_ascii_frame = NULL;
  cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_ASCII_FRAME, NULL, 0, &callbacks), ASCIICHAT_OK);
}

Test(acip_handlers, image_headers_preserve_client_context_and_reject_bad_formats) {
  acip_transport_t transport = {0};
  observation_t seen = {0};
  int client = 17;
  acip_server_callbacks_t callbacks = {.on_image_frame = image_frame, .app_ctx = &seen};
  struct {
    image_frame_packet_t header;
    uint8_t pixels[6];
  } packet = {.header = {.width = HOST_TO_NET_U32(2), .height = HOST_TO_NET_U32(1), .pixel_format = HOST_TO_NET_U32(3)},
              .pixels = {255, 0, 0, 0, 255, 0}};
  const size_t length = sizeof(packet.header) + sizeof(packet.pixels);
  cr_assert_eq(acip_handle_server_packet(&transport, PACKET_TYPE_IMAGE_FRAME, &packet, length, &client, &callbacks),
               ASCIICHAT_OK);
  cr_assert_eq(seen.calls, 1);
  cr_assert_eq(seen.width, 2);
  cr_assert_eq(seen.height, 1);
  cr_assert_eq(seen.client, &client);
  cr_assert_eq(seen.length, 6);
  cr_assert_eq(memcmp(seen.payload, packet.pixels, 6), 0);
  const uint32_t invalid[][3] = {{0, 1, 3}, {2, 0, 3}, {8193, 1, 3}, {2, 8193, 3}, {2, 1, 0}, {2, 1, 5}};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    packet.header.width = HOST_TO_NET_U32(invalid[i][0]);
    packet.header.height = HOST_TO_NET_U32(invalid[i][1]);
    packet.header.pixel_format = HOST_TO_NET_U32(invalid[i][2]);
    cr_assert_eq(acip_handle_server_packet(&transport, PACKET_TYPE_IMAGE_FRAME, &packet, length, &client, &callbacks),
                 ERROR_INVALID_PARAM);
    cr_assert_eq(seen.calls, 1);
  }
  cr_assert_eq(acip_handle_server_packet(&transport, PACKET_TYPE_IMAGE_FRAME, &packet, sizeof(packet.header) - 1,
                                         &client, &callbacks),
               ERROR_INVALID_PARAM);
}

Test(acip_handlers, h264_and_h265_validate_access_unit_headers_before_dispatch) {
  acip_transport_t transport = {0};
  observation_t seen = {0};
  int client = 22;
  acip_server_callbacks_t callbacks = {
      .on_image_frame_h264 = encoded_frame, .on_image_frame_h265 = encoded_frame, .app_ctx = &seen};
  const packet_type_t types[] = {PACKET_TYPE_IMAGE_FRAME_H264, PACKET_TYPE_IMAGE_FRAME_H265};
  for (size_t i = 0; i < 2; ++i) {
    uint8_t packet[] = {3, 0, 2, 0, 1, 0xaa, 0xbb};
    seen.calls = 0;
    cr_assert_eq(acip_handle_server_packet(&transport, types[i], packet, sizeof(packet), &client, &callbacks),
                 ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 1);
    cr_assert_eq(seen.width, 2);
    cr_assert_eq(seen.height, 1);
    cr_assert_eq(seen.flags, 3);
    cr_assert_eq(seen.length, 2);
    cr_assert_eq(seen.payload, packet + 5);
    cr_assert_eq(seen.client, &client);
    for (size_t size = 0; size <= 5; ++size)
      cr_assert_eq(acip_handle_server_packet(&transport, types[i], packet, size, &client, &callbacks),
                   ERROR_INVALID_PARAM);
    const uint8_t invalid[][5] = {
        {4, 0, 2, 0, 1}, {0, 0, 0, 0, 1}, {0, 0, 2, 0, 0}, {0, 0x20, 1, 0, 1}, {0, 0, 1, 0x20, 1}};
    for (size_t j = 0; j < sizeof(invalid) / sizeof(invalid[0]); ++j) {
      memcpy(packet, invalid[j], 5);
      cr_assert_eq(acip_handle_server_packet(&transport, types[i], packet, sizeof(packet), &client, &callbacks),
                   ERROR_INVALID_PARAM);
      cr_assert_eq(seen.calls, 1);
    }
  }
}

Test(acip_handlers, crypto_dispatch_keeps_packet_type_bytes_and_both_contexts) {
  acip_transport_t transport = {0};
  observation_t seen = {0};
  int client = 31;
  const uint8_t bytes[] = {0, 0xff, 0x80, 0x11};
  acip_client_callbacks_t client_callbacks = {.on_crypto_parameters = client_crypto,
                                              .on_crypto_key_exchange_init = client_crypto,
                                              .on_crypto_auth_challenge = client_crypto,
                                              .on_crypto_server_auth_resp = client_crypto,
                                              .on_crypto_auth_failed = client_crypto,
                                              .on_crypto_handshake_complete = client_crypto,
                                              .app_ctx = &seen};
  const packet_type_t client_types[] = {PACKET_TYPE_CRYPTO_PARAMETERS,     PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT,
                                        PACKET_TYPE_CRYPTO_AUTH_CHALLENGE, PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP,
                                        PACKET_TYPE_CRYPTO_AUTH_FAILED,    PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE};
  for (size_t i = 0; i < sizeof(client_types) / sizeof(client_types[0]); ++i) {
    seen.calls = 0;
    cr_assert_eq(acip_handle_client_packet(&transport, client_types[i], bytes, sizeof(bytes), &client_callbacks),
                 ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 1);
    cr_assert_eq(seen.type, client_types[i]);
    cr_assert_eq(seen.payload, bytes);
    cr_assert_eq(seen.length, sizeof(bytes));
  }
  acip_server_callbacks_t server_callbacks = {.on_crypto_key_exchange_resp = server_crypto,
                                              .on_crypto_auth_response = server_crypto,
                                              .on_crypto_no_encryption = server_crypto,
                                              .app_ctx = &seen};
  const packet_type_t server_types[] = {PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, PACKET_TYPE_CRYPTO_AUTH_RESPONSE,
                                        PACKET_TYPE_CRYPTO_NO_ENCRYPTION};
  for (size_t i = 0; i < sizeof(server_types) / sizeof(server_types[0]); ++i) {
    seen.calls = 0;
    cr_assert_eq(
        acip_handle_server_packet(&transport, server_types[i], bytes, sizeof(bytes), &client, &server_callbacks),
        ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 1);
    cr_assert_eq(seen.type, server_types[i]);
    cr_assert_eq(seen.payload, bytes);
    cr_assert_eq(seen.length, sizeof(bytes));
    cr_assert_eq(seen.client, &client);
  }
}

Test(acip_handlers, unknown_packets_and_missing_dispatch_context_are_rejected) {
  acip_transport_t transport = {0};
  acip_client_callbacks_t client = {0};
  acip_server_callbacks_t server = {0};
  cr_assert_eq(acip_handle_client_packet(NULL, PACKET_TYPE_PING, NULL, 0, &client), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_PING, NULL, 0, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_handle_server_packet(NULL, PACKET_TYPE_PING, NULL, 0, NULL, &server), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_handle_server_packet(&transport, PACKET_TYPE_PING, NULL, 0, NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_handle_client_packet(&transport, (packet_type_t)0xffff, NULL, 0, &client), ERROR_INVALID_PARAM);
  cr_assert_eq(acip_handle_server_packet(&transport, (packet_type_t)0xffff, NULL, 0, NULL, &server),
               ERROR_INVALID_PARAM);
  cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_ACIP_PARTICIPANT_JOINED, NULL, 0, &client),
               ASCIICHAT_OK);
  cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_ACIP_PARTICIPANT_LEFT, NULL, 0, &client),
               ASCIICHAT_OK);
}
