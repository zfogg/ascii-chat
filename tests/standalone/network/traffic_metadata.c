#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
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
} capture_t;

static asciichat_error_t capture_send(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count,
                                      const acip_send_options_t *options) {
  capture_t *capture = transport->impl_data;
  if (count != 1 || !buffers[0].data || buffers[0].len < sizeof(packet_header_t) || !options ||
      options->delivery != ACIP_DELIVERY_RELIABLE)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid captured packet");
  atomic_store_u64(&capture->traffic_class, options->traffic_class);
  atomic_fetch_add_u64(&capture->count, 1);
  return ASCIICHAT_OK;
}

static asciichat_error_t capture_close(acip_transport_t *transport) {
  (void)transport;
  return ASCIICHAT_OK;
}

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  static const acip_transport_methods_t methods = {.sendv_ex = capture_send, .close = capture_close};
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
  acip_outbound_destroy(&transport);
  buffer_system_cleanup();
  log_destroy();
  return 0;
}
