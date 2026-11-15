#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/network/crc32.h>
#include <libplatform/socket.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer.h>
#include <string.h>

static socket_t sender_fd = INVALID_SOCKET_VALUE, receiver_fd = INVALID_SOCKET_VALUE;
static crypto_context_t sender, receiver;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  REQUIRE((options_set_bool("no_compress", true)) == (ASCIICHAT_OK));
  REQUIRE((crypto_init(&sender)) == (CRYPTO_OK));
  REQUIRE((crypto_init(&receiver)) == (CRYPTO_OK));
  uint8_t first[32], second[32];
  REQUIRE((crypto_get_public_key(&sender, first)) == (CRYPTO_OK));
  REQUIRE((crypto_get_public_key(&receiver, second)) == (CRYPTO_OK));
  REQUIRE((crypto_set_peer_public_key(&sender, second)) == (CRYPTO_OK));
  REQUIRE((crypto_set_peer_public_key(&receiver, first)) == (CRYPTO_OK));
  sender.handshake_complete = receiver.handshake_complete = true;
  socket_t listener = socket_create("secure-contract-listen", AF_INET, SOCK_STREAM, 0);
  REQUIRE((listener) != (INVALID_SOCKET_VALUE));
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  REQUIRE((socket_bind(listener, (struct sockaddr *)&address, sizeof(address))) == (0));
  REQUIRE((socket_listen(listener, 1)) == (0));
  socklen_t size = sizeof(address);
  REQUIRE((socket_getsockname(listener, (struct sockaddr *)&address, &size)) == (0));
  sender_fd = socket_create("secure-contract-sender", AF_INET, SOCK_STREAM, 0);
  REQUIRE((socket_connect(sender_fd, (struct sockaddr *)&address, size)) == (0));
  receiver_fd = socket_accept(listener, NULL, NULL, "secure-contract-receiver");
  socket_close(listener);
  REQUIRE((receiver_fd) != (INVALID_SOCKET_VALUE));
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
  REQUIRE((receive_packet_secure_with_timeout(receiver_fd, &receiver, encrypted, &envelope, 1000000000ULL)) == (PACKET_RECV_SUCCESS));
  REQUIRE((envelope.type) == (expected));
  const uint8_t *payload = static_cast<const uint8_t *>(envelope.data);
  size_t length = envelope.len;
  if (envelope.data == envelope.allocated_buffer) {
    REQUIRE((length) >= (sizeof(packet_header_t)));
    payload += sizeof(packet_header_t);
    length -= sizeof(packet_header_t);
  }
  REQUIRE((length) <= (capacity));
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
    REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_PING, lengths[i] ? payload : NULL, lengths[i], &sender)) == (ASCIICHAT_OK));
    packet_type_t type;
    void *ciphertext = NULL;
    size_t ciphertext_len = 0, plaintext_len = 0;
    REQUIRE((packet_receive(receiver_fd, &type, &ciphertext, &ciphertext_len)) == (ASCIICHAT_OK));
    REQUIRE((type) == (PACKET_TYPE_ENCRYPTED));
    REQUIRE((ciphertext_len) > (sizeof(packet_header_t) + lengths[i]));
    uint8_t *plaintext = NULL;
    uint64_t sequence;
    bool consumed;
    REQUIRE((acip_security_decrypt(&receiver, CRYPTO_RECORD_ANY_CHANNEL, ACIP_DELIVERY_RELIABLE, ciphertext, ciphertext_len, &plaintext, &plaintext_len, &sequence, &consumed)) == (ASCIICHAT_OK));
    REQUIRE(!consumed);
    buffer_release(ciphertext);
    REQUIRE((plaintext_len) == (sizeof(packet_header_t) + lengths[i]));
    packet_header_t header;
    memcpy(&header, plaintext, sizeof(header));
    REQUIRE((NET_TO_HOST_U64(header.magic)) == (PACKET_MAGIC));
    REQUIRE((NET_TO_HOST_U16(header.type)) == (PACKET_TYPE_PING));
    REQUIRE((NET_TO_HOST_U32(header.length)) == (lengths[i]));
    REQUIRE((NET_TO_HOST_U32(header.client_id)) == (0));
    REQUIRE((NET_TO_HOST_U32(header.crc32)) == (lengths[i] ? asciichat_crc32(payload, lengths[i]) : 0));
    REQUIRE(std::memcmp((plaintext + sizeof(header)), (payload), (lengths[i])) == 0);
    buffer_release(plaintext);
  }
}

TEST_CASE("secure_socket_contract/handshake_and_explicit_plaintext_bypass_encryption_but_ready_data_roundtrips", "[secure_socket_contract]") {
  const uint8_t payload[] = {0, 0xff, 'x'};
  uint8_t output[8];
  REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_CRYPTO_CLIENT_HELLO, payload, sizeof(payload), &sender)) == (ASCIICHAT_OK));
  REQUIRE((read_secure(PACKET_TYPE_CRYPTO_CLIENT_HELLO, true, output, sizeof(output))) == (sizeof(payload)));
  REQUIRE(std::memcmp((output), (payload), (sizeof(payload))) == 0);
  sender.encrypt_data = false;
  REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), &sender)) == (ASCIICHAT_OK));
  REQUIRE((read_secure(PACKET_TYPE_PING, false, output, sizeof(output))) == (sizeof(payload)));
  sender.encrypt_data = true;
  REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), &sender)) == (ASCIICHAT_OK));
  REQUIRE((read_secure(PACKET_TYPE_PING, true, output, sizeof(output))) == (sizeof(payload)));
  REQUIRE(std::memcmp((output), (payload), (sizeof(payload))) == 0);
  REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, sizeof(payload), NULL)) == (ASCIICHAT_OK));
  REQUIRE((read_secure(PACKET_TYPE_PING, false, output, sizeof(output))) == (sizeof(payload)));
  REQUIRE((send_packet_secure(INVALID_SOCKET_VALUE, PACKET_TYPE_PING, payload, sizeof(payload), &sender)) == (ERROR_INVALID_PARAM));
  REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_PING, NULL, 1, &sender)) == (ERROR_INVALID_PARAM));
  REQUIRE((send_packet_secure(sender_fd, PACKET_TYPE_PING, payload, MAX_PACKET_SIZE + 1ULL, &sender)) == (ERROR_NETWORK_SIZE));
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
    REQUIRE((crypto_session_lock(&sender, &session)) == (ASCIICHAT_OK));
    REQUIRE((crypto_record_seal_into(&session->records, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, plaintext, scenario == 3 ? sizeof(header) - 1 : sizeof(plaintext), ciphertext,
                                                 sizeof(ciphertext), &length)) == (ASCIICHAT_OK));
    crypto_session_unlock(session);
    if (scenario == 4)
      ciphertext[length - 1] ^= 1;
    REQUIRE((packet_send(sender_fd, PACKET_TYPE_ENCRYPTED, ciphertext, length)) == (ASCIICHAT_OK));
    packet_envelope_t envelope;
    packet_recv_result_t result = receive_packet_secure_with_timeout(receiver_fd, &receiver, true, &envelope, 1000000000ULL);
    if (scenario < 5) {
      REQUIRE((result) == (PACKET_RECV_ERROR));
      REQUIRE((envelope.allocated_buffer) == nullptr);
      CLEAR_ERRNO_ALL();
    } else {
      REQUIRE((result) == (PACKET_RECV_SUCCESS));
      REQUIRE((envelope.len) == (sizeof(payload)));
      REQUIRE(std::memcmp((envelope.data), (payload), (sizeof(payload))) == 0);
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
      REQUIRE((packet_send_remote_log(sender_fd, encrypted ? &sender : NULL, LOG_WARN, REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, 0x80, input)) == (ASCIICHAT_OK));
      size_t length = read_secure(PACKET_TYPE_REMOTE_LOG, encrypted != 0, payload, sizeof(payload));
      log_level_t level;
      remote_log_direction_t direction;
      uint16_t flags;
      size_t message_len;
      REQUIRE((packet_parse_remote_log(payload, length, &level, &direction, &flags, decoded, sizeof(decoded), &message_len)) == (ASCIICHAT_OK));
      REQUIRE((level) == (LOG_WARN));
      REQUIRE((direction) == (REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT));
      REQUIRE((flags) == (0x80 | (variant == 3 ? REMOTE_LOG_FLAG_TRUNCATED : 0)));
      REQUIRE((message_len) == (variant >= 2 ? MAX_REMOTE_LOG_MESSAGE_LENGTH : input ? strlen(input) : 0));
      REQUIRE((strlen(decoded)) == (message_len));
      if (message_len)
        REQUIRE(std::memcmp((decoded), (input), (message_len)) == 0);
    }
  }
  REQUIRE((packet_send_remote_log(sender_fd, NULL, (log_level_t)99, REMOTE_LOG_DIRECTION_UNKNOWN, 0, "bad")) == (ERROR_INVALID_PARAM));
  REQUIRE((packet_send_remote_log(INVALID_SOCKET_VALUE, NULL, LOG_INFO, REMOTE_LOG_DIRECTION_UNKNOWN, 0, "bad")) == (ERROR_INVALID_PARAM));
}

TEST_CASE("secure_socket_contract/opus_batch_and_rgb_image_senders_use_network_order_with_exact_payloads", "[secure_socket_contract]") {
  const uint8_t opus[] = {1, 2, 3, 4, 5, 6};
  const uint16_t sizes[] = {1, 5};
  uint8_t output[128];
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, sizeof(opus), sizes, 48000, 20, 2, &sender)) == (ASCIICHAT_OK));
  size_t length = read_secure(PACKET_TYPE_AUDIO_OPUS_BATCH, true, output, sizeof(output));
  REQUIRE((length) == (16 + sizeof(sizes) + sizeof(opus)));
  uint32_t value;
  memcpy(&value, output, 4);
  REQUIRE((NET_TO_HOST_U32(value)) == (48000));
  memcpy(&value, output + 4, 4);
  REQUIRE((NET_TO_HOST_U32(value)) == (20));
  memcpy(&value, output + 8, 4);
  REQUIRE((NET_TO_HOST_U32(value)) == (2));
  for (size_t i = 0; i < 2; ++i) {
    uint16_t frame_size;
    memcpy(&frame_size, output + 16 + i * 2, 2);
    REQUIRE((NET_TO_HOST_U16(frame_size)) == (sizes[i]));
  }
  REQUIRE(std::memcmp((output + 20), (opus), (sizeof(opus))) == 0);
  uint8_t rgb[18];
  for (unsigned i = 0; i < sizeof(rgb); ++i)
    rgb[i] = (uint8_t)(i + 40);
  REQUIRE((send_image_frame_packet(sender_fd, rgb, 2, 3, 1)) == (ASCIICHAT_OK));
  length = read_secure(PACKET_TYPE_IMAGE_FRAME, false, output, sizeof(output));
  REQUIRE((length) == (sizeof(image_frame_packet_t) + sizeof(rgb)));
  image_frame_packet_t image;
  memcpy(&image, output, sizeof(image));
  REQUIRE((NET_TO_HOST_U32(image.width)) == (2));
  REQUIRE((NET_TO_HOST_U32(image.height)) == (3));
  REQUIRE((NET_TO_HOST_U32(image.pixel_format)) == (1));
  REQUIRE((NET_TO_HOST_U32(image.compressed_size)) == (0));
  REQUIRE(std::memcmp((output + sizeof(image)), (rgb), (sizeof(rgb))) == 0);
  REQUIRE((send_image_frame_packet(sender_fd, NULL, 2, 3, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((send_image_frame_packet(sender_fd, rgb, 0, 3, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((send_image_frame_packet(sender_fd, rgb, 2, 0, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((send_image_frame_packet(sender_fd, rgb, 4097, 1, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, NULL, 6, sizes, 48000, 20, 2, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, 0, sizes, 48000, 20, 2, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, 6, NULL, 48000, 20, 2, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 0, 20, 2, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 0, 2, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 20, 0, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((av_send_audio_opus_batch(sender_fd, opus, 6, sizes, 48000, 20, 1001, NULL)) == (ERROR_INVALID_PARAM));
}
