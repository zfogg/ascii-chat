#include <criterion/criterion.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer.h>
#include <string.h>

static socket_t sender_fd = INVALID_SOCKET_VALUE, receiver_fd = INVALID_SOCKET_VALUE;
static crypto_context_t sender, receiver;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("no_compress", true), ASCIICHAT_OK);
  cr_assert_eq(crypto_init(&sender), CRYPTO_OK);
  cr_assert_eq(crypto_init(&receiver), CRYPTO_OK);
  uint8_t first[32], second[32];
  cr_assert_eq(crypto_get_public_key(&sender, first), CRYPTO_OK);
  cr_assert_eq(crypto_get_public_key(&receiver, second), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&sender, second), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&receiver, first), CRYPTO_OK);
  socket_t listener = socket_create("secure-contract-listen", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)), 0);
  cr_assert_eq(socket_listen(listener, 1), 0);
  socklen_t size = sizeof(address);
  cr_assert_eq(socket_getsockname(listener, (struct sockaddr *)&address, &size), 0);
  sender_fd = socket_create("secure-contract-sender", AF_INET, SOCK_STREAM, 0);
  cr_assert_eq(socket_connect(sender_fd, (struct sockaddr *)&address, size), 0);
  receiver_fd = socket_accept(listener, NULL, NULL, "secure-contract-receiver");
  socket_close(listener);
  cr_assert_neq(receiver_fd, INVALID_SOCKET_VALUE);
}
static void teardown(void) {
  if (sender_fd != INVALID_SOCKET_VALUE)
    socket_close(sender_fd);
  if (receiver_fd != INVALID_SOCKET_VALUE)
    socket_close(receiver_fd);
  sender_fd = receiver_fd = INVALID_SOCKET_VALUE;
  crypto_destroy(&sender);
  crypto_destroy(&receiver);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(secure_socket_contract, .init = setup, .fini = teardown, .timeout = 15);

static size_t read_secure(packet_type_t expected, bool encrypted, void *output, size_t capacity) {
  packet_envelope_t envelope;
  cr_assert_eq(receive_packet_secure_with_timeout(receiver_fd, &receiver, encrypted, &envelope, 1000000000ULL),
               PACKET_RECV_SUCCESS);
  cr_assert_eq(envelope.type, expected);
  const uint8_t *payload = envelope.data;
  size_t length = envelope.len;
  if (envelope.data == envelope.allocated_buffer) {
    cr_assert_geq(length, sizeof(packet_header_t));
    payload += sizeof(packet_header_t);
    length -= sizeof(packet_header_t);
  }
  cr_assert_leq(length, capacity);
  if (length)
    memcpy(output, payload, length);
  buffer_release(envelope.allocated_buffer);
  return length;
}

Test(secure_socket_contract, encrypted_sender_protects_an_entire_network_order_header_and_payload) {
  uint8_t payload[2048], plaintext[4096];
  for (size_t i = 0; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(i * 17);
  const size_t lengths[] = {0, 7, sizeof(payload)};
  for (size_t i = 0; i < 3; ++i) {
    cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_PING, lengths[i] ? payload : NULL, lengths[i], &sender), ASCIICHAT_OK);
    packet_type_t type;
    void *ciphertext = NULL;
    size_t ciphertext_len = 0, plaintext_len = 0;
    cr_assert_eq(packet_receive(receiver_fd, &type, &ciphertext, &ciphertext_len), ASCIICHAT_OK);
    cr_assert_eq(type, PACKET_TYPE_ENCRYPTED);
    cr_assert_gt(ciphertext_len, sizeof(packet_header_t) + lengths[i]);
    cr_assert_eq(crypto_decrypt(&receiver, ciphertext, ciphertext_len, plaintext, sizeof(plaintext), &plaintext_len),
                 CRYPTO_OK);
    buffer_release(ciphertext);
    cr_assert_eq(plaintext_len, sizeof(packet_header_t) + lengths[i]);
    packet_header_t header;
    memcpy(&header, plaintext, sizeof(header));
    cr_assert_eq(NET_TO_HOST_U64(header.magic), PACKET_MAGIC);
    cr_assert_eq(NET_TO_HOST_U16(header.type), PACKET_TYPE_PING);
    cr_assert_eq(NET_TO_HOST_U32(header.length), lengths[i]);
    cr_assert_eq(NET_TO_HOST_U32(header.client_id), 0);
    cr_assert_eq(NET_TO_HOST_U32(header.crc32), lengths[i] ? asciichat_crc32(payload, lengths[i]) : 0);
    cr_assert_arr_eq(plaintext + sizeof(header), payload, lengths[i]);
  }
}

Test(secure_socket_contract, handshake_and_explicit_plaintext_bypass_encryption_but_ready_data_roundtrips) {
  const uint8_t payload[] = {0, 0xff, 'x'};
  uint8_t output[8];
  cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_CRYPTO_CLIENT_HELLO, payload, sizeof(payload), &sender),
               ASCIICHAT_OK);
  cr_assert_eq(read_secure(PACKET_TYPE_CRYPTO_CLIENT_HELLO, true, output, sizeof(output)), sizeof(payload));
  cr_assert_arr_eq(output, payload, sizeof(payload));
  sender.encrypt_data = false;
  cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), &sender), ASCIICHAT_OK);
  cr_assert_eq(read_secure(PACKET_TYPE_PING, false, output, sizeof(output)), sizeof(payload));
  sender.encrypt_data = true;
  cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), &sender), ASCIICHAT_OK);
  cr_assert_eq(read_secure(PACKET_TYPE_PING, true, output, sizeof(output)), sizeof(payload));
  cr_assert_arr_eq(output, payload, sizeof(payload));
  cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), NULL), ASCIICHAT_OK);
  cr_assert_eq(read_secure(PACKET_TYPE_PING, false, output, sizeof(output)), sizeof(payload));
  cr_assert_eq(send_packet_secure(INVALID_SOCKET_VALUE, PACKET_TYPE_PING, payload, sizeof(payload), &sender),
               ERROR_INVALID_PARAM);
  cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_PING, NULL, 1, &sender), ERROR_INVALID_PARAM);
  cr_assert_eq(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, MAX_PACKET_SIZE + 1ULL, &sender),
               ERROR_NETWORK_SIZE);
}

Test(secure_socket_contract, authenticated_malformed_inner_packets_are_rejected_and_the_next_valid_packet_survives) {
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    uint8_t plaintext[sizeof(packet_header_t) + 4], ciphertext[256];
    const uint8_t payload[] = {'d', 'a', 't', 'a'};
    packet_header_t header = {.magic = HOST_TO_NET_U64(scenario == 0 ? PACKET_MAGIC ^ 1 : PACKET_MAGIC),
                              .type = HOST_TO_NET_U16(PACKET_TYPE_PING),
                              .length = HOST_TO_NET_U32(scenario == 1 ? 5 : 4),
                              .crc32 = HOST_TO_NET_U32(asciichat_crc32(payload, sizeof(payload)) ^ (scenario == 2)),
                              .client_id = 0};
    memcpy(plaintext, &header, sizeof(header));
    memcpy(plaintext + sizeof(header), payload, sizeof(payload));
    size_t length = 0;
    cr_assert_eq(crypto_encrypt(&sender, plaintext, scenario == 3 ? sizeof(header) - 1 : sizeof(plaintext),
                                ciphertext, sizeof(ciphertext), &length), CRYPTO_OK);
    if (scenario == 4)
      ciphertext[length - 1] ^= 1;
    cr_assert_eq(packet_send(sender_fd, PACKET_TYPE_ENCRYPTED, ciphertext, length), ASCIICHAT_OK);
    packet_envelope_t envelope;
    packet_recv_result_t result = receive_packet_secure_with_timeout(receiver_fd, &receiver, true, &envelope,
                                                                    1000000000ULL);
    if (scenario < 5) {
      cr_assert_eq(result, PACKET_RECV_ERROR, "Malformed scenario %u was accepted", scenario);
      cr_assert_null(envelope.allocated_buffer);
      CLEAR_ERRNO_ALL();
    } else {
      cr_assert_eq(result, PACKET_RECV_SUCCESS);
      cr_assert_eq(envelope.len, sizeof(payload));
      cr_assert_arr_eq(envelope.data, payload, sizeof(payload));
      buffer_release(envelope.allocated_buffer);
    }
  }
}

Test(secure_socket_contract, remote_log_sender_preserves_fields_and_marks_only_truncated_messages) {
  char message[MAX_REMOTE_LOG_MESSAGE_LENGTH + 8], exact[MAX_REMOTE_LOG_MESSAGE_LENGTH + 1];
  char decoded[MAX_REMOTE_LOG_MESSAGE_LENGTH + 1];
  uint8_t payload[MAX_REMOTE_LOG_MESSAGE_LENGTH + sizeof(remote_log_packet_t)];
  memset(message, 'L', sizeof(message) - 1);
  message[sizeof(message) - 1] = 0;
  memset(exact, 'E', sizeof(exact) - 1);
  exact[sizeof(exact) - 1] = 0;
  for (unsigned encrypted = 0; encrypted < 2; ++encrypted) {
    for (unsigned variant = 0; variant < 4; ++variant) {
      const char *input = variant == 0 ? NULL : variant == 1 ? "short message" : variant == 2 ? exact : message;
      cr_assert_eq(packet_send_remote_log(sender_fd, encrypted ? &sender : NULL, LOG_WARN,
                                          REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, 0x80, input), ASCIICHAT_OK);
      size_t length = read_secure(PACKET_TYPE_REMOTE_LOG, encrypted != 0, payload, sizeof(payload));
      log_level_t level;
      remote_log_direction_t direction;
      uint16_t flags;
      size_t message_len;
      cr_assert_eq(packet_parse_remote_log(payload, length, &level, &direction, &flags, decoded, sizeof(decoded),
                                            &message_len), ASCIICHAT_OK);
      cr_assert_eq(level, LOG_WARN);
      cr_assert_eq(direction, REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT);
      cr_assert_eq(flags, 0x80 | (variant == 3 ? REMOTE_LOG_FLAG_TRUNCATED : 0));
      cr_assert_eq(message_len, variant >= 2 ? MAX_REMOTE_LOG_MESSAGE_LENGTH : input ? strlen(input) : 0);
      cr_assert_eq(strlen(decoded), message_len);
      if (message_len)
        cr_assert_arr_eq(decoded, input, message_len);
    }
  }
  cr_assert_eq(packet_send_remote_log(sender_fd, NULL, (log_level_t)99, REMOTE_LOG_DIRECTION_UNKNOWN, 0, "bad"),
               ERROR_INVALID_PARAM);
  cr_assert_eq(packet_send_remote_log(INVALID_SOCKET_VALUE, NULL, LOG_INFO, REMOTE_LOG_DIRECTION_UNKNOWN, 0, "bad"),
               ERROR_INVALID_PARAM);
}

Test(secure_socket_contract, opus_batch_and_rgb_image_senders_use_network_order_with_exact_payloads) {
  const uint8_t opus[] = {1, 2, 3, 4, 5, 6};
  const uint16_t sizes[] = {1, 5};
  uint8_t output[128];
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, sizeof(opus), sizes, 48000, 20, 2, &sender), ASCIICHAT_OK);
  size_t length = read_secure(PACKET_TYPE_AUDIO_OPUS_BATCH, true, output, sizeof(output));
  cr_assert_eq(length, 16 + sizeof(sizes) + sizeof(opus));
  uint32_t value;
  memcpy(&value, output, 4);
  cr_assert_eq(NET_TO_HOST_U32(value), 48000);
  memcpy(&value, output + 4, 4);
  cr_assert_eq(NET_TO_HOST_U32(value), 20);
  memcpy(&value, output + 8, 4);
  cr_assert_eq(NET_TO_HOST_U32(value), 2);
  for (size_t i = 0; i < 2; ++i) {
    uint16_t frame_size;
    memcpy(&frame_size, output + 16 + i * 2, 2);
    cr_assert_eq(NET_TO_HOST_U16(frame_size), sizes[i]);
  }
  cr_assert_arr_eq(output + 20, opus, sizeof(opus));
  uint8_t rgb[18];
  for (unsigned i = 0; i < sizeof(rgb); ++i)
    rgb[i] = (uint8_t)(i + 40);
  cr_assert_eq(send_image_frame_packet(sender_fd, rgb, 2, 3, 1), ASCIICHAT_OK);
  length = read_secure(PACKET_TYPE_IMAGE_FRAME, false, output, sizeof(output));
  cr_assert_eq(length, sizeof(image_frame_packet_t) + sizeof(rgb));
  image_frame_packet_t image;
  memcpy(&image, output, sizeof(image));
  cr_assert_eq(NET_TO_HOST_U32(image.width), 2);
  cr_assert_eq(NET_TO_HOST_U32(image.height), 3);
  cr_assert_eq(NET_TO_HOST_U32(image.pixel_format), 1);
  cr_assert_eq(NET_TO_HOST_U32(image.compressed_size), 0);
  cr_assert_arr_eq(output + sizeof(image), rgb, sizeof(rgb));
  cr_assert_eq(send_image_frame_packet(sender_fd, NULL, 2, 3, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(send_image_frame_packet(sender_fd, rgb, 0, 3, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(send_image_frame_packet(sender_fd, rgb, 2, 0, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(send_image_frame_packet(sender_fd, rgb, 4097, 1, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, NULL, 6, sizes, 48000, 20, 2, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, 0, sizes, 48000, 20, 2, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, 6, NULL, 48000, 20, 2, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 0, 20, 2, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 0, 2, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 20, 0, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 20, 1001, NULL), ERROR_INVALID_PARAM);
}