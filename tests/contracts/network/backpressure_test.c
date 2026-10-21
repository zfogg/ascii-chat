#include "../worker_assert.h"
#include <criterion/criterion.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <string.h>

typedef struct {
  atomic_t bytes, sent, closed;
} backend_t;
static size_t queued(acip_transport_t *transport) {
  return (size_t)atomic_load_u64(&((backend_t *)transport->impl_data)->bytes);
}
static asciichat_error_t backend_send(acip_transport_t *transport, const void *data, size_t size) {
  cr_assert_worker(data && size >= sizeof(packet_header_t));
  atomic_fetch_add_u64(&((backend_t *)transport->impl_data)->sent, 1);
  return ASCIICHAT_OK;
}
static asciichat_error_t close_backend(acip_transport_t *transport) {
  atomic_store_bool(&((backend_t *)transport->impl_data)->closed, true);
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t methods = {.send = backend_send, .close = close_backend, .queued_bytes = queued};
static acip_transport_t *create(void) {
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  transport->methods = &methods;
  transport->impl_data = SAFE_CALLOC(1, sizeof(backend_t), backend_t *);
  cr_assert_eq(acip_outbound_create(transport), ASCIICHAT_OK);
  return transport;
}
static acip_admission_t submit(acip_transport_t *transport, uint16_t type, uint32_t stream, size_t size,
                               bool keyframe) {
  uint8_t *payload = buffer_alloc(NULL, size);
  cr_assert_worker(payload);
  memset(payload, 0, size);
  payload[0] = keyframe ? 1 : 0;
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(type),
                            .length = HOST_TO_NET_U32((uint32_t)size),
                            .client_id = HOST_TO_NET_U32(stream)};
  socket_buffer_t buffers[] = {{&header, sizeof(header)}, {payload, size}};
  acip_admission_t admission;
  cr_assert_worker((acip_outbound_submitv(transport, buffers, 2, &admission)) == (ASCIICHAT_OK));
  buffer_release(payload);
  return admission;
}
static void await_submitted(acip_transport_t *transport, uint64_t expected) {
  uint64_t start = time_get_ns();
  for (;;) {
    acip_outbound_stats_t stats;
    cr_assert_eq(acip_outbound_snapshot(transport, &stats), ASCIICHAT_OK);
    if (stats.submitted >= expected)
      return;
    cr_assert(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
}
typedef struct {
  acip_transport_t *transport;
  uint32_t stream;
} producer_t;
static void *produce(void *arg) {
  producer_t *producer = arg;
  for (int i = 0; i < 250; ++i)
    cr_assert_worker((submit(producer->transport, PACKET_TYPE_IMAGE_FRAME, producer->stream, 4096, false)) ==
                     (ACIP_ADMISSION_ACCEPTED));
  return NULL;
}

Test(transport_backpressure, slow_peer_does_not_block_healthy_peer) {
  log_init(NULL, LOG_ERROR, false, false);
  asciichat_errno_suppress(true);
  acip_transport_t *slow = create(), *healthy = create();
  backend_t *backend = slow->impl_data;
  atomic_store_u64(&backend->bytes, 300000);
  for (int i = 0; i < 100; ++i)
    cr_assert_eq(submit(slow, PACKET_TYPE_IMAGE_FRAME, 0, 10000, false), ACIP_ADMISSION_ACCEPTED);
  cr_assert_eq(submit(healthy, PACKET_TYPE_IMAGE_FRAME, 0, 10000, false), ACIP_ADMISSION_ACCEPTED);
  await_submitted(healthy, 1);
  cr_assert_eq(submit(slow, PACKET_TYPE_PING, 0, 1, false), ACIP_ADMISSION_ACCEPTED);
  cr_assert_eq(submit(slow, PACKET_TYPE_AUDIO_BATCH, 0, 100, false), ACIP_ADMISSION_ACCEPTED);
  cr_assert_eq(atomic_load_u64(&backend->sent), 0);
  acip_outbound_stats_t stats;
  cr_assert_eq(acip_outbound_snapshot(slow, &stats), ASCIICHAT_OK);
  cr_assert(stats.dropped == 99 && stats.application_bytes > 0);
  platform_sleep_ms(120);
  cr_assert_eq(submit(slow, PACKET_TYPE_IMAGE_FRAME, 0, 10000, false), ACIP_ADMISSION_ACCEPTED);
  cr_assert(acip_outbound_snapshot(slow, &stats) == ASCIICHAT_OK && stats.dropped == 100);
  for (uint32_t stream = 1; stream < 100; ++stream)
    submit(slow, PACKET_TYPE_IMAGE_FRAME, stream, 100000, false);
  cr_assert_eq(acip_outbound_snapshot(slow, &stats), ASCIICHAT_OK);
  cr_assert(stats.congestion > 0 && stats.application_bytes + stats.backend_bytes <= ACIP_OUTBOUND_BUDGET);
  acip_transport_destroy(slow); // Teardown releases queued owners without a writable callback.
  acip_transport_destroy(healthy);

  buffer_system_cleanup();
  log_destroy();
}

Test(transport_backpressure, codec_recovery_requires_keyframe) {
  log_init(NULL, LOG_ERROR, false, false);
  asciichat_errno_suppress(true);
  backend_t *backend;
  acip_outbound_stats_t stats;
  for (int codec = 0; codec < 2; ++codec) {
    uint16_t type = codec ? PACKET_TYPE_IMAGE_FRAME_H264 : PACKET_TYPE_IMAGE_FRAME_H265;
    acip_transport_t *transport = create();
    backend = transport->impl_data;
    atomic_store_u64(&backend->bytes, 300000);
    cr_assert_eq(submit(transport, type, 0, 1000, false), ACIP_ADMISSION_NEEDS_KEYFRAME);
    cr_assert_eq(submit(transport, type, 0, 1000, true), ACIP_ADMISSION_ACCEPTED);
    cr_assert_eq(submit(transport, type, 0, 1000, false), ACIP_ADMISSION_ACCEPTED);
    platform_sleep_ms(120);
    cr_assert_eq(submit(transport, type, 0, 1000, false), ACIP_ADMISSION_NEEDS_KEYFRAME);
    cr_assert(acip_outbound_needs_keyframe(transport, type, 0));
    cr_assert_eq(submit(transport, type, 0, 1000, true), ACIP_ADMISSION_ACCEPTED);
    atomic_store_u64(&backend->bytes, 0);
    await_submitted(transport, 1);
    cr_assert_eq(acip_outbound_snapshot(transport, &stats), ASCIICHAT_OK);
    cr_assert(stats.recovery_frames == 1 && stats.dropped == 4);
    acip_transport_destroy(transport);
  }
  buffer_system_cleanup();
  log_destroy();
}

Test(transport_backpressure, concurrent_streams_obey_budget_and_keep_control) {
  log_init(NULL, LOG_ERROR, false, false);
  asciichat_errno_suppress(true);
  acip_outbound_stats_t stats;
  acip_transport_t *concurrent = create();
  atomic_store_u64(&((backend_t *)concurrent->impl_data)->bytes, 300000);
  asciichat_thread_t threads[4];
  producer_t producers[4];
  for (uint32_t i = 0; i < 4; ++i) {
    producers[i] = (producer_t){concurrent, i};
    cr_assert_eq(acthread_create(&threads[i], "outbound_producer", produce, &producers[i]), ASCIICHAT_OK);
  }
  for (size_t i = 0; i < 4; ++i)
    cr_assert_eq(thread_join(&threads[i], NULL), ASCIICHAT_OK);
  cr_assert_eq(submit(concurrent, PACKET_TYPE_PING, 0, 1, false), ACIP_ADMISSION_ACCEPTED);
  cr_assert_eq(acip_outbound_snapshot(concurrent, &stats), ASCIICHAT_OK);
  cr_assert(stats.accepted == 1001 && stats.dropped >= 996);
  cr_assert(stats.application_bytes + stats.backend_bytes <= ACIP_OUTBOUND_BUDGET);
  cr_assert_eq(submit(concurrent, PACKET_TYPE_IMAGE_FRAME, 42, 3 * 1024 * 1024, false), ACIP_ADMISSION_CONGESTED);
  acip_transport_destroy(concurrent);

  buffer_stats_t buffers;
  buffer_domain_stats(buffer_default_domain(), &buffers);
  cr_assert_eq(buffers.outstanding, 0);
  buffer_system_cleanup();
  log_destroy();
}
