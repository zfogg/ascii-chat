#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>

#define CHECK(expression)                                                                                              \
  do {                                                                                                                 \
    if (!(expression))                                                                                                 \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expression);                                             \
  } while (0)

typedef struct {
  atomic_t count;
  atomic_t traffic_class;
  atomic_t delivery, deadline, datagrams, fail_datagram, blocked, request_keyframe;
} capture_t;

static asciichat_error_t capture_send(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count,
                                      const acip_send_options_t *options) {
  capture_t *capture = transport->impl_data;
  if (count != 1 || !buffers[0].data || buffers[0].len < sizeof(packet_header_t) || !options)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid captured packet");
  if (options->delivery == ACIP_DELIVERY_DATAGRAM && atomic_exchange_bool(&capture->fail_datagram, false))
    return SET_ERRNO(ERROR_BUFFER_FULL, "Simulated datagram congestion");
  atomic_store_u64(&capture->traffic_class, options->traffic_class);
  atomic_store_u64(&capture->delivery, options->delivery);
  atomic_store_u64(&capture->deadline, options->deadline_ns);
  atomic_fetch_add_u64(&capture->count, 1);
  return ASCIICHAT_OK;
}

static asciichat_error_t capture_capabilities(acip_transport_t *transport, acip_transport_capabilities_t *out) {
  capture_t *capture = transport->impl_data;
  bool enabled = atomic_load_bool(&capture->datagrams);
  *out = (acip_transport_capabilities_t){.independent_streams = !atomic_load_bool(&capture->blocked),
                                         .datagrams = enabled,
                                         .max_datagram_payload = enabled ? 1200 : 0};
  return ASCIICHAT_OK;
}

static asciichat_error_t capture_validate(acip_transport_t *transport, const acip_send_options_t *options) {
  capture_t *capture = transport->impl_data;
  if (options->delivery != ACIP_DELIVERY_DATAGRAM || !atomic_load_bool(&capture->datagrams))
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "Datagrams unavailable");
  return ASCIICHAT_OK;
}

static bool capture_keyframe_request(acip_transport_t *transport, uint16_t type, uint32_t id) {
  capture_t *capture = transport->impl_data;
  return type == PACKET_TYPE_IMAGE_FRAME_H264 && id == 42 && atomic_exchange_bool(&capture->request_keyframe, false);
}

static asciichat_error_t capture_close(acip_transport_t *transport) {
  (void)transport;
  return ASCIICHAT_OK;
}

static size_t capture_queued(acip_transport_t *transport) {
  capture_t *capture = transport->impl_data;
  return atomic_load_bool(&capture->blocked) ? 1 : 0;
}

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  static const acip_transport_methods_t methods = {.sendv_ex = capture_send,
                                                   .close = capture_close,
                                                   .get_capabilities = capture_capabilities,
                                                   .validate_send_options = capture_validate,
                                                   .queued_bytes = capture_queued,
                                                   .consume_keyframe_request = capture_keyframe_request};
  capture_t capture = {0};
  acip_transport_t transport = {.methods = &methods, .impl_data = &capture};
  CHECK(acip_outbound_create(&transport) == ASCIICHAT_OK);
  const struct {
    packet_type_t type;
    acip_traffic_class_t expected;
  } cases[] = {
      {PACKET_TYPE_PROTOCOL_VERSION, ACIP_TRAFFIC_CONTROL}, {PACKET_TYPE_AUDIO_MESSAGE, ACIP_TRAFFIC_AUDIO},
      {PACKET_TYPE_AUDIO_BATCH, ACIP_TRAFFIC_AUDIO},        {PACKET_TYPE_AUDIO_OPUS_BATCH, ACIP_TRAFFIC_AUDIO},
      {PACKET_TYPE_ASCII_FRAME, ACIP_TRAFFIC_VISUAL},       {PACKET_TYPE_IMAGE_FRAME, ACIP_TRAFFIC_VISUAL},
      {PACKET_TYPE_IMAGE_FRAME_H264, ACIP_TRAFFIC_VISUAL},  {PACKET_TYPE_IMAGE_FRAME_H265, ACIP_TRAFFIC_VISUAL}};
  const uint8_t keyframe = 1;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    CHECK(packet_send_via_transport(&transport, cases[i].type, &keyframe, 1, (uint32_t)i) == ASCIICHAT_OK);
    uint64_t start = time_get_ns();
    while (atomic_load_u64(&capture.count) <= i) {
      CHECK(time_get_ns() - start < 2 * NS_PER_SEC_INT);
      platform_sleep_ms(1);
    }
    CHECK(atomic_load_u64(&capture.traffic_class) == (uint64_t)cases[i].expected);
  }

  packet_header_t header = {0};
  socket_buffer_t buffer = {&header, sizeof(header)};
  acip_send_options_t options = {.traffic_class = ACIP_TRAFFIC_AUDIO, .delivery = ACIP_DELIVERY_DATAGRAM};
  CHECK(acip_transport_sendv_ex(&transport, &buffer, 1, &options) == ERROR_NOT_SUPPORTED);
  options.delivery = ACIP_DELIVERY_RELIABLE;
  options.deadline_ns = 1;
  CHECK(acip_transport_sendv_ex(&transport, &buffer, 1, &options) == ERROR_NOT_SUPPORTED);
  CHECK(atomic_load_u64(&capture.count) == sizeof(cases) / sizeof(cases[0]));
  acip_transport_capabilities_t caps;
  CHECK(acip_transport_get_capabilities(&transport, &caps) == ASCIICHAT_OK && caps.independent_streams &&
        !caps.datagrams && !caps.max_datagram_payload);
  atomic_store_bool(&capture.datagrams, true);
  CHECK(acip_transport_get_capabilities(&transport, &caps) == ASCIICHAT_OK && caps.datagrams &&
        caps.max_datagram_payload == 1200);
  header.type = HOST_TO_NET_U16(PACKET_TYPE_AUDIO_BATCH);
  options.delivery = ACIP_DELIVERY_DATAGRAM;
  options.deadline_ns = time_get_ns() + 2 * NS_PER_SEC_INT;
  CHECK(acip_transport_sendv_ex(&transport, &buffer, 1, &options) == ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  while (atomic_load_u64(&capture.count) == sizeof(cases) / sizeof(cases[0])) {
    CHECK(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  CHECK(atomic_load_u64(&capture.delivery) == ACIP_DELIVERY_DATAGRAM &&
        atomic_load_u64(&capture.deadline) == options.deadline_ns);
  options.deadline_ns = 1;
  CHECK(acip_transport_sendv_ex(&transport, &buffer, 1, &options) == ERROR_NETWORK_TIMEOUT);
  options.deadline_ns = 0;
  atomic_store_bool(&capture.fail_datagram, true);
  CHECK(acip_transport_sendv_ex(&transport, &buffer, 1, &options) == ASCIICHAT_OK);
  acip_outbound_stats_t stats;
  start = time_get_ns();
  do {
    CHECK(acip_outbound_snapshot(&transport, &stats) == ASCIICHAT_OK);
    CHECK(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    if (!stats.dropped)
      platform_sleep_ms(1);
  } while (!stats.dropped);
  options.delivery = ACIP_DELIVERY_RELIABLE;
  CHECK(acip_transport_sendv_ex(&transport, &buffer, 1, &options) == ASCIICHAT_OK);
  start = time_get_ns();
  while (atomic_load_u64(&capture.count) < sizeof(cases) / sizeof(cases[0]) + 2) {
    CHECK(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  atomic_store_bool(&capture.blocked, true);
  header.type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME_H264);
  header.client_id = HOST_TO_NET_U32(42);
  const uint8_t encoded_keyframe = 1;
  socket_buffer_t encoded[] = {{&header, sizeof(header)}, {&encoded_keyframe, 1}};
  options.traffic_class = ACIP_TRAFFIC_VISUAL;
  options.delivery = ACIP_DELIVERY_DATAGRAM;
  options.deadline_ns = time_get_ns() + 50 * NS_PER_MS_INT;
  CHECK(acip_transport_sendv_ex(&transport, encoded, 2, &options) == ASCIICHAT_OK);
  CHECK(!acip_outbound_needs_keyframe(&transport, PACKET_TYPE_IMAGE_FRAME_H264, 42));
  start = time_get_ns();
  while (!acip_outbound_needs_keyframe(&transport, PACKET_TYPE_IMAGE_FRAME_H264, 42)) {
    CHECK(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  options.deadline_ns = time_get_ns() + 2 * NS_PER_SEC_INT;
  CHECK(acip_transport_sendv_ex(&transport, encoded, 2, &options) == ASCIICHAT_OK);
  const uint8_t encoded_delta = 0;
  encoded[1].data = &encoded_delta;
  CHECK(acip_transport_sendv_ex(&transport, encoded, 2, &options) == ASCIICHAT_OK);
  CHECK(acip_outbound_snapshot(&transport, &stats) == ASCIICHAT_OK);
  uint64_t dropped_before_request = stats.dropped;
  atomic_store_bool(&capture.request_keyframe, true);
  CHECK(acip_outbound_needs_keyframe(&transport, PACKET_TYPE_IMAGE_FRAME_H264, 42));
  CHECK(acip_outbound_snapshot(&transport, &stats) == ASCIICHAT_OK);
  CHECK(stats.dropped == dropped_before_request + 2);
  atomic_store_bool(&capture.blocked, false);
  acip_outbound_destroy(&transport);

  /* Media datagrams require negotiated support; other traffic stays reliable. */
  capture_t preferred = {0};
  acip_transport_t preferred_transport = {
      .methods = &methods, .impl_data = &preferred, .prefer_audio_datagrams = true, .prefer_visual_datagrams = true};
  CHECK(acip_outbound_create(&preferred_transport) == ASCIICHAT_OK);
  for (unsigned i = 0; i < 6; ++i) {
    atomic_store_bool(&preferred.datagrams, i != 0 && i != 3);
    packet_type_t type = i == 2 ? PACKET_TYPE_PING : i >= 3 ? PACKET_TYPE_IMAGE_FRAME_H264 : PACKET_TYPE_AUDIO_BATCH;
    uint32_t client_id = i == 5 ? 42 : 0;
    CHECK(packet_send_via_transport(&preferred_transport, type, &keyframe, 1, client_id) == ASCIICHAT_OK);
    uint64_t before = time_get_ns();
    while (atomic_load_u64(&preferred.count) <= i) {
      CHECK(time_get_ns() - before < 2 * NS_PER_SEC_INT);
      platform_sleep_ms(1);
    }
    bool datagram = i == 1 || i == 4;
    CHECK(atomic_load_u64(&preferred.delivery) == (datagram ? ACIP_DELIVERY_DATAGRAM : ACIP_DELIVERY_RELIABLE));
    CHECK((atomic_load_u64(&preferred.deadline) != 0) == datagram);
  }
  acip_outbound_destroy(&preferred_transport);
  buffer_system_cleanup();
  log_destroy();
  return 0;
}
