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
#include <errno.h>

typedef struct {
  atomic_t bytes, sent, closed;
  asciichat_error_t failure;
  bool omit_context;
  bool wrap_context;
} backend_t;
static size_t queued(acip_transport_t *transport) {
  return (size_t)atomic_load_u64(&((backend_t *)transport->impl_data)->bytes);
}
static asciichat_error_t backend_send(acip_transport_t *transport, const void *data, size_t size) {
  cr_assert_worker(data && size >= sizeof(packet_header_t));
  backend_t *backend = transport->impl_data;
  atomic_fetch_add_u64(&backend->sent, 1);
  if (backend->failure != ASCIICHAT_OK) {
    if (backend->omit_context) {
      SET_ERRNO(ERROR_INVALID_STATE, "Unrelated backend diagnostic");
      return backend->failure;
    }
    errno_set_with_system_error_and_message(backend->failure, __FILE__, __LINE__, __func__, EPIPE,
                                            "Backend rejected outgoing packet");
    return backend->wrap_context ? SET_ERRNO(backend->failure, "Backend failure wrapper") : backend->failure;
  }
  return ASCIICHAT_OK;
}
static asciichat_error_t close_backend(acip_transport_t *transport) {
  atomic_store_bool(&((backend_t *)transport->impl_data)->closed, true);
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t methods = {.send = backend_send, .close = close_backend, .queued_bytes = queued};

typedef struct {
  atomic_t entered, interrupted, closing, overlaps, closes;
} shutdown_backend_t;

static void interrupt_shutdown_backend(acip_transport_t *transport) {
  atomic_store_bool(&((shutdown_backend_t *)transport->impl_data)->interrupted, true);
}

static asciichat_error_t blocked_shutdown_send(acip_transport_t *transport, const void *data, size_t size) {
  (void)data;
  (void)size;
  shutdown_backend_t *backend = transport->impl_data;
  atomic_store_bool(&backend->entered, true);
  while (!atomic_load_bool(&backend->interrupted))
    platform_sleep_ms(1);
  return ERROR_NETWORK;
}

static asciichat_error_t close_shutdown_backend(acip_transport_t *transport) {
  shutdown_backend_t *backend = transport->impl_data;
  if (atomic_fetch_add_u64(&backend->closing, 1))
    atomic_store_bool(&backend->overlaps, true);
  interrupt_shutdown_backend(transport);
  // Give the interrupted send time to fail and attempt its own backend close.
  platform_sleep_ms(30);
  atomic_fetch_add_u64(&backend->closes, 1);
  atomic_fetch_sub_u64(&backend->closing, 1);
  return ASCIICHAT_OK;
}
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

Test(transport_backpressure, destruction_serializes_backend_close_after_a_blocked_send_fails) {
  static const acip_transport_methods_t shutdown_methods = {
      .send = blocked_shutdown_send, .close = close_shutdown_backend, .interrupt = interrupt_shutdown_backend};
  shutdown_backend_t backend = {0};
  acip_transport_t transport = {.methods = &shutdown_methods, .impl_data = &backend};
  cr_assert_eq(acip_outbound_create(&transport), ASCIICHAT_OK);
  cr_assert_eq(submit(&transport, PACKET_TYPE_IMAGE_FRAME, 0, 100, false), ACIP_ADMISSION_ACCEPTED);
  uint64_t started = time_get_ns();
  while (!atomic_load_bool(&backend.entered)) {
    cr_assert(time_get_ns() - started < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  acip_outbound_destroy(&transport);
  cr_assert_null(transport.outbound);
  cr_assert_eq(atomic_load_u64(&backend.closes), 2);
  cr_assert_not(atomic_load_bool(&backend.overlaps), "Destructor and worker must never close a backend concurrently");
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
  errno_suppress(true);
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
  errno_suppress(true);
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
  errno_suppress(true);
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

typedef struct {
  acip_transport_t *transport;
  asciichat_error_t result;
  asciichat_error_context_t cause;
  asciichat_error_context_t detail;
  bool has_cause;
} failing_producer_t;

static void *submit_control(void *arg) {
  failing_producer_t *producer = arg;
  packet_header_t header = {.type = HOST_TO_NET_U16(PACKET_TYPE_STREAM_START)};
  socket_buffer_t buffer = {&header, sizeof(header)};
  acip_admission_t admission;
  // A caller's older failure must not replace the worker's failure snapshot.
  SET_ERRNO(ERROR_INVALID_PARAM, "Old producer diagnostic");
  asciichat_errno_scope_t scope = errno_checkpoint();
  producer->result = acip_outbound_submitv(producer->transport, &buffer, 1, &admission);
  producer->has_cause = errno_peek_since(scope, &producer->cause);
  asciichat_error_context_t frames[3];
  size_t count = errno_snapshot(scope.thread_id, frames, 3).copied;
  for (size_t i = 0; i < count; ++i)
    if (frames[i].error_id > scope.after_id && frames[i].has_system_error)
      producer->detail = frames[i];
  CLEAR_ERRNO_ALL();
  return NULL;
}

static void check_backend_failure(asciichat_error_t code, bool omit_context, bool wrap_context) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  acip_transport_t *transport = create();
  backend_t *backend = transport->impl_data;
  backend->failure = code;
  backend->omit_context = omit_context;
  backend->wrap_context = wrap_context;
  atomic_store_u64(&backend->bytes, 1); // Hold both controls until they are queued.
  failing_producer_t producers[2] = {{.transport = transport}, {.transport = transport}};
  asciichat_thread_t threads[2];
  for (size_t i = 0; i < 2; ++i)
    cr_assert_eq(acthread_create(&threads[i], "failing_control", submit_control, &producers[i]), ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  for (;;) {
    acip_outbound_stats_t stats;
    cr_assert_eq(acip_outbound_snapshot(transport, &stats), ASCIICHAT_OK);
    if (stats.accepted == 2)
      break;
    cr_assert(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  atomic_store_u64(&backend->bytes, 0);
  for (size_t i = 0; i < 2; ++i) {
    cr_assert_eq(thread_join(&threads[i], NULL), ASCIICHAT_OK);
    cr_assert_eq(producers[i].result, code);
    cr_assert(producers[i].has_cause);
    cr_assert_eq(producers[i].cause.code, code);
    cr_assert_str_eq(producers[i].cause.context_message, omit_context   ? "Outbound backend submission failed"
                                                         : wrap_context ? "Backend failure wrapper"
                                                                        : "Backend rejected outgoing packet");
    if (!omit_context) {
      cr_assert(producers[i].detail.has_system_error);
      cr_assert_eq(producers[i].detail.system_errno, EPIPE);
      cr_assert_str_eq(producers[i].detail.function, "backend_send");
      cr_assert_str_eq(producers[i].detail.context_message, "Backend rejected outgoing packet");
      if (wrap_context)
        cr_assert_eq(producers[i].cause.cause_id, producers[i].detail.error_id);
    }
  }
  cr_assert_eq(atomic_load_u64(&backend->sent), 1); // No retry of a possibly partial packet.
  failing_producer_t later = {.transport = transport};
  submit_control(&later);
  cr_assert_eq(later.result, code);
  cr_assert(later.has_cause);
  cr_assert_str_eq(later.cause.context_message, producers[0].cause.context_message);
  if (wrap_context) {
    cr_assert_eq(later.cause.cause_id, later.detail.error_id);
    cr_assert_eq(later.detail.cause_id, producers[0].detail.cause_id);
  } else {
    cr_assert_eq(later.cause.cause_id, producers[0].cause.cause_id);
  }
  if (!omit_context)
    cr_assert_eq(later.detail.system_errno, EPIPE);
  acip_transport_destroy(transport);
  buffer_system_cleanup();
  log_destroy();
}

Test(transport_backpressure, unsupported_backend_preserves_failure_for_all_producers) {
  check_backend_failure(ERROR_NOT_SUPPORTED, false, false);
}

Test(transport_backpressure, fatal_backend_preserves_failure_for_all_producers) {
  check_backend_failure(ERROR_CRYPTO, false, false);
}

Test(transport_backpressure, backend_status_cannot_inherit_unrelated_context) {
  check_backend_failure(ERROR_NETWORK_TIMEOUT, true, false);
}

Test(transport_backpressure, wrapped_backend_preserves_nested_system_error) {
  check_backend_failure(ERROR_NETWORK, false, true);
}

Test(transport_backpressure, asynchronous_media_failure_reaches_next_producer) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  acip_transport_t *transport = create();
  backend_t *backend = transport->impl_data;
  backend->failure = ERROR_NOT_SUPPORTED;
  cr_assert_eq(submit(transport, PACKET_TYPE_IMAGE_FRAME, 0, 1000, false), ACIP_ADMISSION_ACCEPTED);
  uint64_t start = time_get_ns();
  while (!atomic_load_bool(&backend->closed)) {
    cr_assert(time_get_ns() - start < 2 * NS_PER_SEC_INT);
    platform_sleep_ms(1);
  }
  failing_producer_t later = {.transport = transport};
  submit_control(&later);
  cr_assert_eq(later.result, ERROR_NOT_SUPPORTED);
  cr_assert(later.has_cause);
  cr_assert_str_eq(later.cause.context_message, "Backend rejected outgoing packet");
  cr_assert_eq(atomic_load_u64(&backend->sent), 1);
  acip_transport_destroy(transport);
  buffer_system_cleanup();
  log_destroy();
}

Test(transport_backpressure, expired_control_reports_timeout_without_backend_send, .timeout = 15) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  acip_transport_t *transport = create();
  backend_t *backend = transport->impl_data;
  atomic_store_u64(&backend->bytes, 1);
  failing_producer_t producer = {.transport = transport};
  submit_control(&producer);
  cr_assert_eq(producer.result, ERROR_NETWORK_TIMEOUT);
  cr_assert(producer.has_cause);
  cr_assert_eq(producer.cause.code, ERROR_NETWORK_TIMEOUT);
  cr_assert_str_eq(producer.cause.context_message, "Outbound reliable packet expired before submission");
  cr_assert_eq(atomic_load_u64(&backend->sent), 0);
  acip_transport_destroy(transport);
  buffer_system_cleanup();
  log_destroy();
}

Test(transport_backpressure, congestion_recovers_without_terminal_error) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  acip_transport_t *transport = create();
  backend_t *backend = transport->impl_data;
  atomic_store_u64(&backend->bytes, ACIP_OUTBOUND_BUDGET);
  CLEAR_ERRNO_ALL();
  asciichat_errno_scope_t scope = errno_checkpoint();
  cr_assert_eq(submit(transport, PACKET_TYPE_IMAGE_FRAME, 0, 1000, false), ACIP_ADMISSION_CONGESTED);
  cr_assert_not(errno_peek_since(scope, NULL));
  cr_assert_not(atomic_load_bool(&backend->closed));
  atomic_store_u64(&backend->bytes, 0);
  cr_assert_eq(submit(transport, PACKET_TYPE_IMAGE_FRAME, 0, 1000, false), ACIP_ADMISSION_ACCEPTED);
  await_submitted(transport, 1);
  cr_assert_not(errno_peek_since(scope, NULL));
  cr_assert_not(atomic_load_bool(&backend->closed));
  acip_transport_destroy(transport);
  buffer_system_cleanup();
  log_destroy();
}
