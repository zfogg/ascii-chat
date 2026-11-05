#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <network/quic/stream.h>
#include <network/quic/datagram.h>
#include <string.h>

#define CHECK(expression)                                                                                              \
  do {                                                                                                                 \
    if (!(expression))                                                                                                 \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expression);                                             \
  } while (0)

typedef struct {
  acip_traffic_class_t expected;
  const uint8_t *packet;
  size_t size;
  unsigned count;
  bool reject;
} capture_t;

static asciichat_error_t capture(acip_traffic_class_t traffic_class, const void *packet, size_t size, void *arg) {
  capture_t *state = arg;
  if (traffic_class != state->expected || size != state->size || memcmp(packet, state->packet, size) != 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Incorrect stream packet");
  ++state->count;
  return state->reject ? SET_ERRNO(ERROR_BUFFER_FULL, "Receiver rejected packet") : ASCIICHAT_OK;
}

static void test_datagrams(void) {
  uint8_t packet[301];
  for (size_t i = 0; i < sizeof(packet); ++i)
    packet[i] = (uint8_t)i;
  uint8_t fragments[4][128];
  size_t sizes[4];
  for (uint16_t i = 0; i < 4; ++i)
    CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_VISUAL, 7, packet, sizeof(packet), 100, i, fragments[i],
                                      sizeof(fragments[i]), &sizes[i]) == ASCIICHAT_OK);
  acip_quic_datagram_reader_t reader = {0};
  acip_traffic_class_t flow = ACIP_TRAFFIC_CONTROL;
  uint8_t *output = NULL;
  size_t output_size = 0;
  const unsigned order[] = {3, 1, 1, 0, 2};
  for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
    unsigned n = order[i];
    CHECK(acip_quic_datagram_read(&reader, fragments[n], sizes[n], 10 + i, &flow, &output, &output_size) ==
          ASCIICHAT_OK);
    if (i + 1 < sizeof(order) / sizeof(order[0]))
      CHECK(output == NULL);
  }
  CHECK(output && output_size == sizeof(packet) && flow == ACIP_TRAFFIC_VISUAL);
  CHECK(memcmp(output, packet, sizeof(packet)) == 0 && reader.bytes == 0);
  buffer_release(output);
  CHECK(acip_quic_datagram_read(&reader, fragments[0], sizes[0], 20, &flow, &output, &output_size) == ASCIICHAT_OK);
  fragments[0][ACIP_QUIC_DATAGRAM_HEADER_SIZE] ^= 1;
  CHECK(acip_quic_datagram_read(&reader, fragments[0], sizes[0], 21, &flow, &output, &output_size) ==
        ERROR_NETWORK_PROTOCOL);
  fragments[0][ACIP_QUIC_DATAGRAM_HEADER_SIZE] ^= 1;
  CHECK(acip_quic_datagram_read(&reader, fragments[1], sizes[1] - 1, 22, &flow, &output, &output_size) ==
        ERROR_NETWORK_PROTOCOL);
  fragments[1][26] = 1;
  CHECK(acip_quic_datagram_read(&reader, fragments[1], sizes[1], 22, &flow, &output, &output_size) ==
        ERROR_NETWORK_PROTOCOL);
  fragments[1][26] = 0;
  CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_CONTROL, 1, packet, sizeof(packet), 100, 0, fragments[1],
                                    sizeof(fragments[1]), &sizes[1]) == ERROR_INVALID_PARAM);
  CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_AUDIO, 1, packet, sizeof(packet), 100, 4, fragments[1],
                                    sizeof(fragments[1]), &sizes[1]) == ERROR_NETWORK_SIZE);
  CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_AUDIO, 1, packet, sizeof(packet), 100, 0, fragments[1], 40,
                                    &sizes[1]) == ERROR_BUFFER_FULL);
  CHECK(acip_quic_datagram_read(&reader, fragments[0], sizes[0], 19, &flow, &output, &output_size) ==
        ERROR_INVALID_PARAM);
  CHECK(reader.bytes > 0);
  acip_quic_datagram_expire(&reader, 20 + ACIP_QUIC_DATAGRAM_EXPIRY_NS);
  CHECK(reader.bytes == 0);
  for (uint64_t id = 1; id <= 4; ++id) {
    CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_VISUAL, id, packet, sizeof(packet), 100, 0, fragments[0],
                                      sizeof(fragments[0]), &sizes[0]) == ASCIICHAT_OK);
    CHECK(acip_quic_datagram_read(&reader, fragments[0], sizes[0], 20 + ACIP_QUIC_DATAGRAM_EXPIRY_NS, &flow, &output,
                                  &output_size) == ASCIICHAT_OK);
  }
  CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_VISUAL, 5, packet, sizeof(packet), 100, 0, fragments[0],
                                    sizeof(fragments[0]), &sizes[0]) == ASCIICHAT_OK);
  CHECK(acip_quic_datagram_read(&reader, fragments[0], sizes[0], 20 + ACIP_QUIC_DATAGRAM_EXPIRY_NS, &flow, &output,
                                &output_size) == ERROR_BUFFER_FULL);
  CHECK(acip_quic_datagram_fragment(ACIP_TRAFFIC_AUDIO, 5, packet, sizeof(packet), 100, 0, fragments[0],
                                    sizeof(fragments[0]), &sizes[0]) == ASCIICHAT_OK);
  CHECK(acip_quic_datagram_read(&reader, fragments[0], sizes[0], 20 + ACIP_QUIC_DATAGRAM_EXPIRY_NS, &flow, &output,
                                &output_size) == ASCIICHAT_OK);
  acip_quic_datagram_reset(&reader);
  CHECK(reader.bytes == 0);
}

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  struct sockaddr_storage address;
  socklen_t address_length = 0;
  CHECK(parse_numeric_socket_address("127.0.0.1", 23456, &address, &address_length) == ASCIICHAT_OK);
  CHECK(address.ss_family == AF_INET && address_length == sizeof(struct sockaddr_in) &&
        NET_TO_HOST_U16(((struct sockaddr_in *)&address)->sin_port) == 23456);
  CHECK(parse_numeric_socket_address("::1", 23456, &address, &address_length) == ASCIICHAT_OK);
  CHECK(address.ss_family == AF_INET6 && address_length == sizeof(struct sockaddr_in6) &&
        NET_TO_HOST_U16(((struct sockaddr_in6 *)&address)->sin6_port) == 23456);
  CHECK(parse_numeric_socket_address("localhost", 23456, &address, &address_length) == ERROR_INVALID_PARAM &&
        address_length == 0);
  test_datagrams();
  const char payload[] = "video";
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME),
                            .length = HOST_TO_NET_U32(sizeof(payload)),
                            .crc32 = HOST_TO_NET_U32(asciichat_crc32(payload, sizeof(payload)))};
  uint8_t packet[sizeof(header) + sizeof(payload)];
  memcpy(packet, &header, sizeof(header));
  memcpy(packet + sizeof(header), payload, sizeof(payload));

  for (int traffic = ACIP_TRAFFIC_CONTROL; traffic <= ACIP_TRAFFIC_VISUAL; ++traffic) {
    uint8_t wire[ACIP_QUIC_STREAM_PREAMBLE_SIZE + 2 * sizeof(packet)];
    packet_type_t type = traffic == ACIP_TRAFFIC_CONTROL ? PACKET_TYPE_PING
                         : traffic == ACIP_TRAFFIC_AUDIO ? PACKET_TYPE_AUDIO_BATCH
                                                         : PACKET_TYPE_IMAGE_FRAME;
    header.type = HOST_TO_NET_U16(type);
    memcpy(packet, &header, sizeof(header));
    CHECK(acip_quic_stream_preamble((acip_traffic_class_t)traffic, wire) == ASCIICHAT_OK);
    memcpy(wire + ACIP_QUIC_STREAM_PREAMBLE_SIZE, packet, sizeof(packet));
    memcpy(wire + ACIP_QUIC_STREAM_PREAMBLE_SIZE + sizeof(packet), packet, sizeof(packet));
    for (size_t split = 1; split <= sizeof(wire); ++split) {
      acip_quic_stream_reader_t reader = {0};
      capture_t state = {.expected = (acip_traffic_class_t)traffic, .packet = packet, .size = sizeof(packet)};
      for (size_t offset = 0; offset < sizeof(wire);) {
        size_t count = sizeof(wire) - offset < split ? sizeof(wire) - offset : split;
        CHECK(acip_quic_stream_read(&reader, wire + offset, count, capture, &state) == ASCIICHAT_OK);
        offset += count;
      }
      CHECK(state.count == 2);
      CHECK(acip_quic_stream_finish(&reader) == ASCIICHAT_OK);
      CHECK(acip_quic_stream_read(&reader, NULL, 0, capture, &state) == ERROR_INVALID_PARAM);
      acip_quic_stream_reset(&reader);
    }
  }

  header.type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME);
  memcpy(packet, &header, sizeof(header));
  uint8_t preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE];
  CHECK(acip_quic_stream_preamble(ACIP_TRAFFIC_VISUAL, preamble) == ASCIICHAT_OK);
  CHECK(acip_quic_stream_preamble((acip_traffic_class_t)99, preamble) == ERROR_INVALID_PARAM);
  capture_t state = {.expected = ACIP_TRAFFIC_VISUAL, .packet = packet, .size = sizeof(packet)};
  {
    acip_quic_stream_reader_t reader = {0};
    packet_header_t empty = {.magic = HOST_TO_NET_U64(PACKET_MAGIC), .type = HOST_TO_NET_U16(PACKET_TYPE_PING)};
    uint8_t control_preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE];
    CHECK(acip_quic_stream_preamble(ACIP_TRAFFIC_CONTROL, control_preamble) == ASCIICHAT_OK);
    capture_t empty_state = {
        .expected = ACIP_TRAFFIC_CONTROL, .packet = (const uint8_t *)&empty, .size = sizeof(empty)};
    CHECK(acip_quic_stream_read(&reader, control_preamble, sizeof(control_preamble), capture, &empty_state) ==
          ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, &empty, sizeof(empty), capture, &empty_state) == ASCIICHAT_OK);
    CHECK(empty_state.count == 1 && acip_quic_stream_finish(&reader) == ASCIICHAT_OK);
    acip_quic_stream_reset(&reader);
  }
  for (int traffic = ACIP_TRAFFIC_CONTROL; traffic <= ACIP_TRAFFIC_VISUAL; ++traffic) {
    acip_quic_stream_reader_t reader = {0};
    uint8_t wrong_preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE];
    CHECK(acip_quic_stream_preamble((acip_traffic_class_t)traffic, wrong_preamble) == ASCIICHAT_OK);
    header.type = HOST_TO_NET_U16(traffic == ACIP_TRAFFIC_VISUAL ? PACKET_TYPE_PING : PACKET_TYPE_IMAGE_FRAME);
    memcpy(packet, &header, sizeof(header));
    CHECK(acip_quic_stream_read(&reader, wrong_preamble, sizeof(wrong_preamble), capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, packet, sizeof(packet), capture, &state) == ERROR_NETWORK_PROTOCOL);
    CHECK(!state.count);
    acip_quic_stream_reset(&reader);
  }
  header.type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED);
  memcpy(packet, &header, sizeof(header));
  for (int traffic = ACIP_TRAFFIC_CONTROL; traffic <= ACIP_TRAFFIC_VISUAL; ++traffic) {
    acip_quic_stream_reader_t reader = {0};
    uint8_t encrypted_preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE];
    capture_t encrypted_state = {.expected = (acip_traffic_class_t)traffic, .packet = packet, .size = sizeof(packet)};
    CHECK(acip_quic_stream_preamble((acip_traffic_class_t)traffic, encrypted_preamble) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, encrypted_preamble, sizeof(encrypted_preamble), capture, &encrypted_state) ==
          ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, packet, sizeof(packet), capture, &encrypted_state) == ASCIICHAT_OK);
    CHECK(encrypted_state.count == 1);
    acip_quic_stream_reset(&reader);
  }
  header.type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME);
  memcpy(packet, &header, sizeof(header));
  for (size_t i = 0; i < sizeof(preamble); ++i) {
    acip_quic_stream_reader_t reader = {0};
    uint8_t invalid[sizeof(preamble)];
    memcpy(invalid, preamble, sizeof(invalid));
    invalid[i] = i == 5 ? 99 : (uint8_t)(invalid[i] ^ 0xff);
    CHECK(acip_quic_stream_read(&reader, invalid, sizeof(invalid), capture, &state) == ERROR_NETWORK_PROTOCOL);
    CHECK(acip_quic_stream_finish(&reader) == ERROR_INVALID_PARAM);
    acip_quic_stream_reset(&reader);
  }

  for (size_t length = 0; length < sizeof(preamble); ++length) {
    acip_quic_stream_reader_t reader = {0};
    CHECK(acip_quic_stream_read(&reader, preamble, length, capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_finish(&reader) == ERROR_NETWORK_PROTOCOL);
    acip_quic_stream_reset(&reader);
  }
  for (size_t length = 1; length < sizeof(packet); ++length) {
    acip_quic_stream_reader_t reader = {0};
    CHECK(acip_quic_stream_read(&reader, preamble, sizeof(preamble), capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, packet, length, capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_finish(&reader) == ERROR_NETWORK_PROTOCOL);
    acip_quic_stream_reset(&reader);
  }
  {
    acip_quic_stream_reader_t reader = {0};
    reader.packets.max_payload = sizeof(payload) - 1;
    CHECK(acip_quic_stream_read(&reader, preamble, sizeof(preamble), capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, packet, sizeof(packet), capture, &state) == ERROR_NETWORK_PROTOCOL);
    acip_quic_stream_reset(&reader);
  }
  {
    acip_quic_stream_reader_t reader = {0};
    uint8_t corrupt[sizeof(packet)];
    memcpy(corrupt, packet, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1] ^= 1;
    CHECK(acip_quic_stream_read(&reader, preamble, sizeof(preamble), capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, corrupt, sizeof(corrupt), capture, &state) == ERROR_NETWORK_PROTOCOL);
    acip_quic_stream_reset(&reader);
  }
  {
    acip_quic_stream_reader_t reader = {0};
    state.reject = true;
    CHECK(acip_quic_stream_read(&reader, preamble, sizeof(preamble), capture, &state) == ASCIICHAT_OK);
    CHECK(acip_quic_stream_read(&reader, packet, sizeof(packet), capture, &state) == ERROR_BUFFER_FULL);
    CHECK(acip_quic_stream_finish(&reader) == ERROR_INVALID_PARAM);
    acip_quic_stream_reset(&reader);
  }
  buffer_system_cleanup();
  log_destroy();
  return 0;
}
