#include "../worker_assert.h"
#include <criterion/criterion.h>
#include <ascii-chat/platform/event.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <string.h>

static size_t packets;
static asciichat_error_t count_packet(const void *data, size_t size, void *arg) {
  (void)arg;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  cr_assert_eq(size, sizeof(header) + NET_TO_HOST_U32(header.length));
  ++packets;
  return ASCIICHAT_OK;
}

static void decoder_tests(void) {
  packet_decoder_t decoder = {0};
  uint8_t wire[sizeof(packet_header_t) + 3];
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(PACKET_TYPE_TEXT_MESSAGE),
                            .length = HOST_TO_NET_U32(3),
                            .crc32 = HOST_TO_NET_U32(asciichat_crc32("abc", 3))};
  memcpy(wire, &header, sizeof(header));
  memcpy(wire + sizeof(header), "abc", 3);
  for (size_t split = 0; split <= sizeof(wire); ++split) {
    cr_assert_eq(packet_decoder_feed(&decoder, wire, split, count_packet, NULL), ASCIICHAT_OK);
    cr_assert_eq(packet_decoder_feed(&decoder, wire + split, sizeof(wire) - split, count_packet, NULL), ASCIICHAT_OK);
    cr_assert(!packet_decoder_pending(&decoder));
  }
  cr_assert_eq(packets, sizeof(wire) + 1);
  buffer_stats_t before, after;
  buffer_domain_stats(buffer_default_domain(), &before);
  for (unsigned i = 0; i < 100; ++i)
    cr_assert_eq(packet_decoder_feed(&decoder, wire, sizeof(wire), count_packet, NULL), ASCIICHAT_OK);
  buffer_domain_stats(buffer_default_domain(), &after);
  cr_assert(before.allocations == after.allocations && before.hits == after.hits);
  uint8_t batch[sizeof(wire) * 2];
  memcpy(batch, wire, sizeof(wire));
  memcpy(batch + sizeof(wire), wire, sizeof(wire));
  cr_assert_eq(packet_decoder_feed(&decoder, batch, sizeof(batch), count_packet, NULL), ASCIICHAT_OK);
  wire[sizeof(wire) - 1] ^= 1;
  cr_assert_neq(packet_decoder_feed(&decoder, wire, sizeof(wire), count_packet, NULL), ASCIICHAT_OK);
  cr_assert_neq(packet_decoder_feed(&decoder, wire, sizeof(wire), count_packet, NULL), ASCIICHAT_OK);
  packet_decoder_reset(&decoder);
  header.length = HOST_TO_NET_U32(MAX_PACKET_SIZE + 1);
  cr_assert_neq(packet_decoder_feed(&decoder, &header, sizeof(header), count_packet, NULL), ASCIICHAT_OK);
  cr_assert_eq(decoder.packet, NULL);
  packet_decoder_reset(&decoder);
  header.length = 0;
  header.crc32 = 0;
  cr_assert_eq(packet_decoder_feed(&decoder, &header, sizeof(header), count_packet, NULL), ASCIICHAT_OK);
  cr_assert(!packet_decoder_pending(&decoder));
  packet_decoder_reset(&decoder);
  CLEAR_ERRNO_ALL();
}

typedef struct {
  platform_event_loop_t *loop;
  platform_event_stream_t *stream;
  const void *data;
  size_t length;
  unsigned accepted, closed, completions, jobs;
  bool expire, cancel, push, zerocopy, acknowledged, drain;
  socket_io_stats_t stats;
  _Atomic(size_t) queued_bytes, peak_queued_bytes;
} fixture_t;

static void queue_observed(void *arg, size_t bytes) {
  fixture_t *fixture = arg;
  fixture->queued_bytes = bytes;
  if (bytes > fixture->peak_queued_bytes)
    fixture->peak_queued_bytes = bytes;
}

static void write_done(void *arg, asciichat_error_t result) {
  fixture_t *fixture = arg;
  if (fixture->expire && fixture->push)
    cr_assert_worker(result == ASCIICHAT_OK || result == ERROR_NETWORK);
  else
    cr_assert_worker((result) == ((fixture->cancel ? ERROR_NETWORK : ASCIICHAT_OK)));
  ++fixture->completions;
  if (fixture->push && fixture->acknowledged && fixture->completions == 3)
    platform_event_stream_close(fixture->stream, ASCIICHAT_OK);
}
static void work(void *arg) {
  fixture_t *fixture = arg;
  socket_buffer_t buffer = {fixture->data, fixture->length};
  cr_assert_worker((platform_event_stream_write(fixture->stream, &buffer, 1, write_done, fixture)) == (ASCIICHAT_OK));
  ++fixture->jobs;
}
static void work_done(platform_event_stream_t *stream, void *arg, bool closing) {
  (void)arg;
  if (!closing)
    cr_assert_worker((platform_event_stream_resume(stream)) == (ASCIICHAT_OK));
}
static void read_data(platform_event_stream_t *stream, const void *data, size_t length, void *arg) {
  fixture_t *fixture = arg;
  if (fixture->push) {
    fixture->acknowledged = true;
    if (fixture->completions == 3)
      platform_event_stream_close(stream, ASCIICHAT_OK);
    return;
  }
  fixture->data = data;
  fixture->length = length;
  cr_assert_worker((platform_event_stream_work(stream, work, work_done, fixture)) == (ASCIICHAT_OK));
}
static void closed(void *arg, asciichat_error_t result) {
  fixture_t *fixture = arg;
  cr_assert_worker((result) == ((fixture->cancel   ? ERROR_NETWORK
                                 : fixture->expire ? ERROR_NETWORK_TIMEOUT
                                                   : ASCIICHAT_OK)));
  ++fixture->closed;
  cr_assert_worker((fixture->queued_bytes) == (0));
  if (fixture->completions)
    cr_assert_worker(fixture->peak_queued_bytes > 0);
  platform_event_loop_stop(fixture->loop);
}
static void accepted(platform_event_stream_t *stream, void *arg) {
  fixture_t *fixture = arg;
  ++fixture->accepted;
  fixture->stream = stream;
  platform_event_stream_observe_queue(stream, queue_observed, fixture);
  if (!fixture->zerocopy)
    platform_event_stream_set_zerocopy(stream, false);
  cr_assert_worker((platform_event_stream_start(stream, read_data, closed, fixture)) == (ASCIICHAT_OK));
  cr_assert_worker((platform_event_stream_deadline(stream, fixture->expire ? 50 : 10000)) == (ASCIICHAT_OK));
  socket_buffer_t oversize = {"x", 17 * 1024 * 1024};
  cr_assert_worker((platform_event_stream_write(stream, &oversize, 1, NULL, NULL)) == (ERROR_NETWORK_SIZE));
  CLEAR_ERRNO_ALL();
  if (fixture->push) {
    size_t length = fixture->expire ? 8 * 1024 * 1024 : 256 * 1024;
    char *bytes = buffer_alloc(NULL, length);
    cr_assert_worker((bytes) != (NULL));
    memset(bytes, 0x5a, length);
    socket_buffer_t buffer = {bytes, length};
    socket_buffer_t prefix = {"prefix", 6}, suffix = {"suffix", 6};
    if (!fixture->expire)
      cr_assert_worker((platform_event_stream_write(stream, &prefix, 1, write_done, fixture)) == (ASCIICHAT_OK));
    cr_assert_worker((platform_event_stream_write(stream, &buffer, 1, write_done, fixture)) == (ASCIICHAT_OK));
    // Submission copies the caller's data before returning.
    memset(bytes, 0xa5, length);
    buffer_release(bytes);
    if (!fixture->expire)
      cr_assert_worker((platform_event_stream_write(stream, &suffix, 1, write_done, fixture)) == (ASCIICHAT_OK));
    if (fixture->drain) {
      platform_event_stream_request_close(stream, ASCIICHAT_OK);
      cr_assert_worker((platform_event_stream_write(stream, &suffix, 1, NULL, NULL)) != (ASCIICHAT_OK));
      CLEAR_ERRNO_ALL();
    }
  } else if (fixture->cancel) {
    char bytes[65536] = {0};
    socket_buffer_t buffer = {bytes, sizeof(bytes)};
    for (unsigned i = 0; i < 256; ++i)
      cr_assert_worker((platform_event_stream_write(stream, &buffer, 1, write_done, fixture)) == (ASCIICHAT_OK));
    cr_assert_worker((platform_event_stream_write(stream, &buffer, 1, write_done, fixture)) == (ERROR_NETWORK_SIZE));
    cr_assert_worker((platform_event_stream_queued_bytes(stream)) == (16 * 1024 * 1024));
    CLEAR_ERRNO_ALL();
    platform_event_stream_close(stream, ERROR_NETWORK);
  }
}
static void *run_loop(void *arg) {
  fixture_t *fixture = arg;
  socket_io_stats_reset();
  cr_assert_worker((platform_event_loop_run(fixture->loop)) == (ASCIICHAT_OK));
  fixture->stats = socket_io_stats_get();
  return NULL;
}

static void event_test(bool expire, bool cancel, bool push, bool zerocopy, bool drain) {
  fixture_t fixture = {.expire = expire, .cancel = cancel, .push = push, .zerocopy = zerocopy, .drain = drain};
  cr_assert_worker((platform_event_loop_create(&fixture.loop)) == (ASCIICHAT_OK));
  socket_t listener = socket_create("event-test-listener", AF_INET, SOCK_STREAM, 0);
  cr_assert_worker((listener) != (INVALID_SOCKET_VALUE));
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_worker((socket_bind(listener, (struct sockaddr *)&address, sizeof(address))) == (0));
  cr_assert_worker((socket_listen(listener, 16)) == (0));
  socklen_t size = sizeof(address);
  cr_assert_worker((socket_getsockname(listener, (struct sockaddr *)&address, &size)) == (0));
  platform_event_listener_t *event_listener;
  cr_assert_worker((platform_event_listen(fixture.loop, &listener, accepted, &fixture, &event_listener)) ==
                   (ASCIICHAT_OK));
  cr_assert_worker((listener) == (INVALID_SOCKET_VALUE));
  asciichat_thread_t thread;
  cr_assert_worker((acthread_create(&thread, "event_test", run_loop, &fixture)) == (ASCIICHAT_OK));
  socket_t client = socket_create("event-test-client", AF_INET, SOCK_STREAM, 0);
  if (push && expire) {
    int receive_buffer = 4096;
    cr_assert_worker((socket_setsockopt(client, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer))) ==
                     (0));
  }
  cr_assert_worker((socket_connect(client, (struct sockaddr *)&address, sizeof(address))) == (0));
  if (push && !expire) {
    char received[256 * 1024 + 12];
    cr_assert_worker((recv_with_timeout(client, received, sizeof(received), 10 * NS_PER_SEC_INT)) ==
                     ((ssize_t)sizeof(received)));
    cr_assert_worker((memcmp(received, "prefix", 6)) == (0));
    for (size_t i = 6; i < sizeof(received) - 6; ++i)
      cr_assert_worker((received[i]) == (0x5a));
    cr_assert_worker((memcmp(received + sizeof(received) - 6, "suffix", 6)) == (0));
    if (!drain)
      cr_assert_worker((send_with_timeout(client, "!", 1, NS_PER_SEC_INT)) == (1));
    char byte;
    cr_assert_worker(recv_with_timeout(client, &byte, 1, 5 * NS_PER_SEC_INT) <= 0);
    CLEAR_ERRNO_ALL();
  } else if (!expire && !cancel) {
    char expected[256 * 1024], received[sizeof(expected)];
    memset(expected, 0x5a, sizeof(expected));
    cr_assert_worker((send_with_timeout(client, expected, sizeof(expected), 10 * NS_PER_SEC_INT)) ==
                     ((ssize_t)sizeof(expected)));
    cr_assert_worker((recv_with_timeout(client, received, sizeof(received), 10 * NS_PER_SEC_INT)) ==
                     ((ssize_t)sizeof(received)));
    cr_assert_worker((memcmp(expected, received, sizeof(expected))) == (0));
    socket_shutdown(client, SHUT_WR);
  } else if (!push) {
    char byte;
    cr_assert_worker(recv_with_timeout(client, &byte, 1, 5 * NS_PER_SEC_INT) <= 0);
    CLEAR_ERRNO_ALL();
  }
  cr_assert_worker((thread_join(&thread, NULL)) == (ASCIICHAT_OK));
  cr_assert_worker(fixture.accepted == 1 && fixture.closed == 1);
  cr_assert_worker((fixture.completions) == ((push ? (expire ? 1 : 3) : cancel ? 256 : fixture.jobs)));
  if (push && !expire) {
    cr_assert_worker((fixture.stats.zerocopy_calls) == (fixture.stats.zerocopy_completions));
    log_warn("Event output: zerocopy=%d submissions=%llu completions=%llu copied=%llu", zerocopy,
             (unsigned long long)fixture.stats.zerocopy_calls, (unsigned long long)fixture.stats.zerocopy_completions,
             (unsigned long long)fixture.stats.zerocopy_copied);
  }
  cr_assert_worker((platform_event_loop_destroy(fixture.loop)) == (ASCIICHAT_OK));
  socket_close(client);
}

void accepted_start_failure_test(void);

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  cr_assert_worker((socket_init()) == (ASCIICHAT_OK));
}

static void teardown(void) {
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  cr_assert_worker((stats.outstanding) == (0));
  buffer_system_cleanup();
  cr_assert_worker((buffer_process_reserved()) == (0));
  socket_cleanup();
  log_destroy();
}

TestSuite(network_events, .init = setup, .fini = teardown, .timeout = 30);

Test(network_events, accepted_start_failure) {
  accepted_start_failure_test();
}

Test(network_events, decoder_fragmentation_and_reuse) {
  decoder_tests();
}

Test(network_events, echo) {
  event_test(false, false, false, false, false);
}

Test(network_events, deadline) {
  event_test(true, false, false, false, false);
}

Test(network_events, cancel_echo) {
  event_test(false, true, false, false, false);
}

Test(network_events, push) {
  event_test(false, false, true, false, false);
}

Test(network_events, zerocopy_push) {
  event_test(false, false, true, true, false);
}

Test(network_events, cancel_zerocopy) {
  event_test(false, true, false, true, false);
}

Test(network_events, expire_push) {
  event_test(true, false, true, false, false);
}

Test(network_events, expire_zerocopy) {
  event_test(true, false, true, true, false);
}

Test(network_events, drain_push) {
  event_test(false, false, true, false, true);
}

Test(network_events, drain_zerocopy) {
  event_test(false, false, true, true, true);
}
