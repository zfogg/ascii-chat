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
  audio_batch_packet_t audio;
  float samples[4];
} observation_t;

static void client_audio(const audio_batch_packet_t *header, const float *samples, size_t count, void *context) {
  observation_t *seen = context;
  seen->calls++;
  seen->audio = *header;
  seen->length = count;
  cr_assert_leq(count, 4);
  memcpy(seen->samples, samples, count * sizeof(float));
}

static void server_audio(const audio_batch_packet_t *header, const float *samples, size_t count, void *client,
                         void *context) {
  client_audio(header, samples, count, context);
  ((observation_t *)context)->client = client;
}

static void client_signal(void *context) {
  ((observation_t *)context)->calls++;
}

static void server_signal(void *client, void *context) {
  client_signal(context);
  ((observation_t *)context)->client = client;
}

static void stream_signal(uint32_t types, void *client, void *context) {
  server_signal(client, context);
  ((observation_t *)context)->flags = types;
}

Test(acip_handlers, audio_batches_decode_wire_samples_and_metadata_for_both_peers) {
  acip_transport_t transport = {0};
  observation_t seen = {0};
  int client;
  acip_client_callbacks_t c = {.on_audio_batch = client_audio, .app_ctx = &seen};
  acip_server_callbacks_t s = {.on_audio_batch = server_audio, .app_ctx = &seen};
  struct {
    audio_batch_packet_t header;
    uint32_t samples[4];
  } packet = {.header = {.batch_count = HOST_TO_NET_U32(2),
                         .total_samples = HOST_TO_NET_U32(4),
                         .sample_rate = HOST_TO_NET_U32(48000),
                         .channels = HOST_TO_NET_U32(2)},
              .samples = {HOST_TO_NET_U32(0), HOST_TO_NET_U32(0x7fffffff), HOST_TO_NET_U32(0x80000001),
                          HOST_TO_NET_U32(0x40000000)}};
  for (unsigned peer = 0; peer < 2; ++peer) {
    seen.calls = 0;
    asciichat_error_t result =
        peer ? acip_handle_server_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet), &client, &s)
             : acip_handle_client_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet), &c);
    cr_assert_eq(result, ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 1);
    cr_assert_eq(seen.audio.batch_count, 2);
    cr_assert_eq(seen.audio.total_samples, 4);
    cr_assert_eq(seen.audio.sample_rate, 48000);
    cr_assert_eq(seen.audio.channels, 2);
    cr_assert_eq(seen.length, 4);
    cr_assert_float_eq(seen.samples[0], 0, 1e-7);
    cr_assert_float_eq(seen.samples[1], 1, 1e-7);
    cr_assert_float_eq(seen.samples[2], -1, 1e-7);
    cr_assert_float_eq(seen.samples[3], .5f, 1e-7);
    if (peer)
      cr_assert_eq(seen.client, &client);
  }
  const uint32_t rates[] = {8000, 16000, 24000, 32000, 44100, 48000, 96000, 192000};
  for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
    packet.header.sample_rate = HOST_TO_NET_U32(rates[i]);
    cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet), &c),
                 ASCIICHAT_OK);
    cr_assert_eq(seen.audio.sample_rate, rates[i]);
  }
  const uint32_t bad_rates[] = {0, 7999, 12345, 192001, UINT32_MAX};
  for (size_t i = 0; i < sizeof(bad_rates) / sizeof(bad_rates[0]); ++i) {
    packet.header.sample_rate = HOST_TO_NET_U32(bad_rates[i]);
    seen.calls = 0;
    cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet), &c),
                 ERROR_INVALID_PARAM);
    cr_assert_eq(seen.calls, 0);
  }
  packet.header.sample_rate = HOST_TO_NET_U32(48000);
  const uint32_t bad_channels[] = {0, 9, UINT32_MAX};
  for (size_t i = 0; i < sizeof(bad_channels) / sizeof(bad_channels[0]); ++i) {
    packet.header.channels = HOST_TO_NET_U32(bad_channels[i]);
    cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet), &c),
                 ERROR_INVALID_PARAM);
  }
  packet.header.channels = HOST_TO_NET_U32(2);
  seen.calls = 0;
  for (size_t length = 0; length < sizeof(packet); ++length) {
    cr_assert_eq(acip_handle_client_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, length, &c),
                 ERROR_INVALID_PARAM);
    cr_assert_eq(acip_handle_server_packet(&transport, PACKET_TYPE_AUDIO_BATCH, &packet, length, &client, &s),
                 ERROR_INVALID_PARAM);
  }
  cr_assert_eq(seen.calls, 0);
}

Test(acip_handlers, ping_pong_console_and_stream_controls_preserve_context_and_wire_flags) {
  acip_transport_t transport = {0};
  observation_t seen = {0};
  int client;
  acip_client_callbacks_t c = {
      .on_ping = client_signal, .on_pong = client_signal, .on_clear_console = client_signal, .app_ctx = &seen};
  acip_server_callbacks_t s = {.on_ping = server_signal,
                               .on_pong = server_signal,
                               .on_client_leave = server_signal,
                               .on_stream_start = stream_signal,
                               .on_stream_stop = stream_signal,
                               .app_ctx = &seen};
  const packet_type_t client_types[] = {PACKET_TYPE_PING, PACKET_TYPE_PONG, PACKET_TYPE_CLEAR_CONSOLE};
  const packet_type_t server_types[] = {PACKET_TYPE_PING, PACKET_TYPE_PONG, PACKET_TYPE_CLIENT_LEAVE};
  for (size_t i = 0; i < 3; ++i) {
    seen.calls = 0;
    cr_assert_eq(acip_handle_client_packet(&transport, client_types[i], NULL, 0, &c), ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 1);
    cr_assert_eq(acip_handle_server_packet(&transport, server_types[i], NULL, 0, &client, &s), ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 2);
    cr_assert_eq(seen.client, &client);
  }
  const packet_type_t stream_types[] = {PACKET_TYPE_STREAM_START, PACKET_TYPE_STREAM_STOP};
  uint8_t unaligned[] = {0xff, 0x12, 0x34, 0xab, 0xcd};
  for (size_t i = 0; i < 2; ++i) {
    seen.calls = 0;
    for (size_t length = 0; length < 4; ++length)
      cr_assert_eq(acip_handle_server_packet(&transport, stream_types[i], unaligned + 1, length, &client, &s),
                   ERROR_INVALID_PARAM);
    cr_assert_eq(seen.calls, 0);
    cr_assert_eq(acip_handle_server_packet(&transport, stream_types[i], unaligned + 1, 4, &client, &s), ASCIICHAT_OK);
    cr_assert_eq(seen.calls, 1);
    cr_assert_eq(seen.flags, 0x1234abcd);
    cr_assert_eq(seen.client, &client);
  }
}

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
