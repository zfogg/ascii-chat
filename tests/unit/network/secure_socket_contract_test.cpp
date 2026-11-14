#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/crypto/session.h>
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
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("no_compress", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&sender), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&receiver), CRYPTO_OK);
  uint8_t first[32], second[32];
  ASCIICHAT_REQUIRE_EQ(crypto_get_public_key(&sender, first), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_get_public_key(&receiver, second), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&sender, second), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&receiver, first), CRYPTO_OK);
  sender.handshake_complete = receiver.handshake_complete = true;
  socket_t listener = socket_create("secure-contract-listen", AF_INET, SOCK_STREAM, 0);
  ASCIICHAT_REQUIRE_NE(listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  ASCIICHAT_REQUIRE_EQ(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)), 0);
  ASCIICHAT_REQUIRE_EQ(socket_listen(listener, 1), 0);
  socklen_t size = sizeof(address);
  ASCIICHAT_REQUIRE_EQ(socket_getsockname(listener, (struct sockaddr *)&address, &size), 0);
  sender_fd = socket_create("secure-contract-sender", AF_INET, SOCK_STREAM, 0);
  ASCIICHAT_REQUIRE_EQ(socket_connect(sender_fd, (struct sockaddr *)&address, size), 0);
  receiver_fd = socket_accept(listener, NULL, NULL, "secure-contract-receiver");
  socket_close(listener);
  ASCIICHAT_REQUIRE_NE(receiver_fd, INVALID_SOCKET_VALUE);
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
static size_t read_secure(packet_type_t expected, bool encrypted, void *output, size_t capacity) {
  packet_envelope_t envelope;
  ASCIICHAT_REQUIRE_EQ(receive_packet_secure_with_timeout(receiver_fd, &receiver, encrypted, &envelope, 1000000000ULL),
               PACKET_RECV_SUCCESS);
  ASCIICHAT_REQUIRE_EQ(envelope.type, expected);
  const uint8_t *payload = static_cast<const uint8_t *>(envelope.data);
  size_t length = envelope.len;
  if (envelope.data == envelope.allocated_buffer) {
    ASCIICHAT_REQUIRE_GE(length, sizeof(packet_header_t));
    payload += sizeof(packet_header_t);
    length -= sizeof(packet_header_t);
  }
  ASCIICHAT_REQUIRE_LE(length, capacity);
  if (length)
    memcpy(output, payload, length);
  buffer_release(envelope.allocated_buffer);
  return length;
}

TEST_CASE("secure_socket_contract/encrypted_sender_protects_an_entire_network_order_header_and_payload", "[secure_socket_contract]") {
  uint8_t payload[2048];
  for (size_t i = 0; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(i * 17);
  const size_t lengths[] = {0, 7, sizeof(payload)};
  for (size_t i = 0; i < 3; ++i) {
    ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_PING, lengths[i] ? payload : NULL, lengths[i], &sender),
                 ASCIICHAT_OK);
    packet_type_t type;
    void *ciphertext = NULL;
    size_t ciphertext_len = 0, plaintext_len = 0;
    ASCIICHAT_REQUIRE_EQ(packet_receive(receiver_fd, &type, &ciphertext, &ciphertext_len), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(type, PACKET_TYPE_ENCRYPTED);
    ASCIICHAT_REQUIRE_GT(ciphertext_len, sizeof(packet_header_t) + lengths[i]);
    uint8_t *plaintext = NULL;
    uint64_t sequence;
    bool consumed;
    ASCIICHAT_REQUIRE_EQ(acip_security_decrypt(&receiver, CRYPTO_RECORD_ANY_CHANNEL, ACIP_DELIVERY_RELIABLE, ciphertext,
                                       ciphertext_len, &plaintext, &plaintext_len, &sequence, &consumed),
                 ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(!consumed);
    buffer_release(ciphertext);
    ASCIICHAT_REQUIRE_EQ(plaintext_len, sizeof(packet_header_t) + lengths[i]);
    packet_header_t header;
    memcpy(&header, plaintext, sizeof(header));
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U64(header.magic), PACKET_MAGIC);
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.type), PACKET_TYPE_PING);
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.length), lengths[i]);
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.client_id), 0);
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(header.crc32), lengths[i] ? asciichat_crc32(payload, lengths[i]) : 0);
    ASCIICHAT_REQUIRE_ARRAY_EQ(plaintext + sizeof(header), payload, lengths[i]);
    buffer_release(plaintext);
  }
}

TEST_CASE("secure_socket_contract/handshake_and_explicit_plaintext_bypass_encryption_but_ready_data_roundtrips", "[secure_socket_contract]") {
  const uint8_t payload[] = {0, 0xff, 'x'};
  uint8_t output[8];
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_CRYPTO_CLIENT_HELLO, payload, sizeof(payload), &sender),
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(read_secure(PACKET_TYPE_CRYPTO_CLIENT_HELLO, true, output, sizeof(output)), sizeof(payload));
  ASCIICHAT_REQUIRE_ARRAY_EQ(output, payload, sizeof(payload));
  sender.encrypt_data = false;
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), &sender), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(read_secure(PACKET_TYPE_PING, false, output, sizeof(output)), sizeof(payload));
  sender.encrypt_data = true;
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), &sender), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(read_secure(PACKET_TYPE_PING, true, output, sizeof(output)), sizeof(payload));
  ASCIICHAT_REQUIRE_ARRAY_EQ(output, payload, sizeof(payload));
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(read_secure(PACKET_TYPE_PING, false, output, sizeof(output)), sizeof(payload));
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(INVALID_SOCKET_VALUE, PACKET_TYPE_PING, payload, sizeof(payload), &sender),
               ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_PING, NULL, 1, &sender), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, MAX_PACKET_SIZE + 1ULL, &sender),
               ERROR_NETWORK_SIZE);
}

TEST_CASE("secure_socket_contract/authenticated_malformed_inner_packets_are_rejected_and_the_next_valid_packet_survives", "[secure_socket_contract]") {
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
    crypto_session_t *session;
    ASCIICHAT_REQUIRE_EQ(crypto_session_lock(&sender, &session), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(crypto_record_seal_into(&session->records, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, plaintext,
                                         scenario == 3 ? sizeof(header) - 1 : sizeof(plaintext), ciphertext,
                                         sizeof(ciphertext), &length),
                 ASCIICHAT_OK);
    crypto_session_unlock(session);
    if (scenario == 4)
      ciphertext[length - 1] ^= 1;
    ASCIICHAT_REQUIRE_EQ(packet_send(sender_fd, PACKET_TYPE_ENCRYPTED, ciphertext, length), ASCIICHAT_OK);
    packet_envelope_t envelope;
    packet_recv_result_t result =
        receive_packet_secure_with_timeout(receiver_fd, &receiver, true, &envelope, 1000000000ULL);
    if (scenario < 5) {
      ASCIICHAT_REQUIRE_EQ(result, PACKET_RECV_ERROR, "Malformed scenario %u was accepted", scenario);
      ASCIICHAT_REQUIRE_NULL(envelope.allocated_buffer);
      CLEAR_ERRNO_ALL();
    } else {
      ASCIICHAT_REQUIRE_EQ(result, PACKET_RECV_SUCCESS);
      ASCIICHAT_REQUIRE_EQ(envelope.len, sizeof(payload));
      ASCIICHAT_REQUIRE_ARRAY_EQ(envelope.data, payload, sizeof(payload));
      buffer_release(envelope.allocated_buffer);
    }
  }
}

TEST_CASE("secure_socket_contract/remote_log_sender_preserves_fields_and_marks_only_truncated_messages", "[secure_socket_contract]") {
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
      ASCIICHAT_REQUIRE_EQ(packet_send_remote_log(sender_fd, encrypted ? &sender : NULL, LOG_WARN,
                                                  REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, 0x80, input),
                           ASCIICHAT_OK);
      size_t length = read_secure(PACKET_TYPE_REMOTE_LOG, encrypted != 0, payload, sizeof(payload));
      log_level_t level;
      remote_log_direction_t direction;
      uint16_t flags;
      size_t message_len;
      ASCIICHAT_REQUIRE_EQ(
          packet_parse_remote_log(payload, length, &level, &direction, &flags, decoded, sizeof(decoded), &message_len),
          ASCIICHAT_OK);
      ASCIICHAT_REQUIRE_EQ(level, LOG_WARN);
      ASCIICHAT_REQUIRE_EQ(direction, REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT);
      ASCIICHAT_REQUIRE_EQ(flags, 0x80 | (variant == 3 ? REMOTE_LOG_FLAG_TRUNCATED : 0));
      ASCIICHAT_REQUIRE_EQ(message_len, variant >= 2 ? MAX_REMOTE_LOG_MESSAGE_LENGTH : input ? strlen(input) : 0);
      ASCIICHAT_REQUIRE_EQ(strlen(decoded), message_len);
      if (message_len)
        ASCIICHAT_REQUIRE_ARRAY_EQ(decoded, input, message_len);
    }
  }
  ASCIICHAT_REQUIRE_EQ(packet_send_remote_log(sender_fd, NULL, (log_level_t)99, REMOTE_LOG_DIRECTION_UNKNOWN, 0, "bad"),
               ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(packet_send_remote_log(INVALID_SOCKET_VALUE, NULL, LOG_INFO, REMOTE_LOG_DIRECTION_UNKNOWN, 0, "bad"),
               ERROR_INVALID_PARAM);
}

TEST_CASE("secure_socket_contract/opus_batch_and_rgb_image_senders_use_network_order_with_exact_payloads", "[secure_socket_contract]") {
  const uint8_t opus[] = {1, 2, 3, 4, 5, 6};
  const uint16_t sizes[] = {1, 5};
  uint8_t output[128];
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, sizeof(opus), sizes, 48000, 20, 2, &sender), ASCIICHAT_OK);
  size_t length = read_secure(PACKET_TYPE_AUDIO_OPUS_BATCH, true, output, sizeof(output));
  ASCIICHAT_REQUIRE_EQ(length, 16 + sizeof(sizes) + sizeof(opus));
  uint32_t value;
  memcpy(&value, output, 4);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(value), 48000);
  memcpy(&value, output + 4, 4);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(value), 20);
  memcpy(&value, output + 8, 4);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(value), 2);
  for (size_t i = 0; i < 2; ++i) {
    uint16_t frame_size;
    memcpy(&frame_size, output + 16 + i * 2, 2);
    ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(frame_size), sizes[i]);
  }
  ASCIICHAT_REQUIRE_ARRAY_EQ(output + 20, opus, sizeof(opus));
  uint8_t rgb[18];
  for (unsigned i = 0; i < sizeof(rgb); ++i)
    rgb[i] = (uint8_t)(i + 40);
  ASCIICHAT_REQUIRE_EQ(send_image_frame_packet(sender_fd, rgb, 2, 3, 1), ASCIICHAT_OK);
  length = read_secure(PACKET_TYPE_IMAGE_FRAME, false, output, sizeof(output));
  ASCIICHAT_REQUIRE_EQ(length, sizeof(image_frame_packet_t) + sizeof(rgb));
  image_frame_packet_t image;
  memcpy(&image, output, sizeof(image));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(image.width), 2);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(image.height), 3);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(image.pixel_format), 1);
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(image.compressed_size), 0);
  ASCIICHAT_REQUIRE_ARRAY_EQ(output + sizeof(image), rgb, sizeof(rgb));
  ASCIICHAT_REQUIRE_EQ(send_image_frame_packet(sender_fd, NULL, 2, 3, 1), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(send_image_frame_packet(sender_fd, rgb, 0, 3, 1), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(send_image_frame_packet(sender_fd, rgb, 2, 0, 1), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(send_image_frame_packet(sender_fd, rgb, 4097, 1, 1), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, NULL, 6, sizes, 48000, 20, 2, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, 0, sizes, 48000, 20, 2, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, 6, NULL, 48000, 20, 2, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 0, 20, 2, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 0, 2, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 20, 0, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 20, 1001, NULL), ERROR_INVALID_PARAM);
}
