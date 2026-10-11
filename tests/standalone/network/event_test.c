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

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)

static size_t packets;
static asciichat_error_t count_packet(const void *data, size_t size, void *arg) {
  (void)arg;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  CHECK(size == sizeof(header) + NET_TO_HOST_U32(header.length));
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
    CHECK(packet_decoder_feed(&decoder, wire, split, count_packet, NULL) == ASCIICHAT_OK);
    CHECK(packet_decoder_feed(&decoder, wire + split, sizeof(wire) - split, count_packet, NULL) == ASCIICHAT_OK);
    CHECK(!packet_decoder_pending(&decoder));
  }
  CHECK(packets == sizeof(wire) + 1);
  uint8_t batch[sizeof(wire) * 2];
  memcpy(batch, wire, sizeof(wire));
  memcpy(batch + sizeof(wire), wire, sizeof(wire));
  CHECK(packet_decoder_feed(&decoder, batch, sizeof(batch), count_packet, NULL) == ASCIICHAT_OK);
  wire[sizeof(wire) - 1] ^= 1;
  CHECK(packet_decoder_feed(&decoder, wire, sizeof(wire), count_packet, NULL) != ASCIICHAT_OK);
  CHECK(packet_decoder_feed(&decoder, wire, sizeof(wire), count_packet, NULL) != ASCIICHAT_OK);
  packet_decoder_reset(&decoder);
  header.length = HOST_TO_NET_U32(MAX_PACKET_SIZE + 1);
  CHECK(packet_decoder_feed(&decoder, &header, sizeof(header), count_packet, NULL) != ASCIICHAT_OK);
  CHECK(decoder.packet == NULL);
  packet_decoder_reset(&decoder);
  header.length = 0;
  header.crc32 = 0;
  CHECK(packet_decoder_feed(&decoder, &header, sizeof(header), count_packet, NULL) == ASCIICHAT_OK);
  CHECK(!packet_decoder_pending(&decoder));
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
} fixture_t;

static void write_done(void *arg, asciichat_error_t result) {
  fixture_t *fixture = arg;
  if (fixture->expire && fixture->push)
    CHECK(result == ASCIICHAT_OK || result == ERROR_NETWORK);
  else
    CHECK(result == (fixture->cancel ? ERROR_NETWORK : ASCIICHAT_OK));
  ++fixture->completions;
  if (fixture->push && fixture->acknowledged && fixture->completions == 3)
    platform_event_stream_close(fixture->stream, ASCIICHAT_OK);
}
static void work(void *arg) {
  fixture_t *fixture = arg;
  socket_buffer_t buffer = {fixture->data, fixture->length};
  CHECK(platform_event_stream_write(fixture->stream, &buffer, 1, write_done, fixture) == ASCIICHAT_OK);
  ++fixture->jobs;
}
static void work_done(platform_event_stream_t *stream, void *arg, bool closing) {
  (void)arg;
  if (!closing)
    CHECK(platform_event_stream_resume(stream) == ASCIICHAT_OK);
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
  CHECK(platform_event_stream_work(stream, work, work_done, fixture) == ASCIICHAT_OK);
}
static void closed(void *arg, asciichat_error_t result) {
  fixture_t *fixture = arg;
  CHECK(result == (fixture->cancel ? ERROR_NETWORK : fixture->expire ? ERROR_NETWORK_TIMEOUT : ASCIICHAT_OK));
  ++fixture->closed;
  platform_event_loop_stop(fixture->loop);
}
static void accepted(platform_event_stream_t *stream, void *arg) {
  fixture_t *fixture = arg;
  ++fixture->accepted;
  fixture->stream = stream;
  if (!fixture->zerocopy)
    platform_event_stream_set_zerocopy(stream, false);
  CHECK(platform_event_stream_start(stream, read_data, closed, fixture) == ASCIICHAT_OK);
  CHECK(platform_event_stream_deadline(stream, fixture->expire ? 50 : 10000) == ASCIICHAT_OK);
  socket_buffer_t oversize = {"x", 17 * 1024 * 1024};
  CHECK(platform_event_stream_write(stream, &oversize, 1, NULL, NULL) == ERROR_NETWORK_SIZE);
  CLEAR_ERRNO_ALL();
  if (fixture->push) {
    size_t length = fixture->expire ? 8 * 1024 * 1024 : 256 * 1024;
    char *bytes = buffer_alloc(NULL, length);
    CHECK(bytes != NULL);
    memset(bytes, 0x5a, length);
    socket_buffer_t buffer = {bytes, length};
    socket_buffer_t prefix = {"prefix", 6}, suffix = {"suffix", 6};
    if (!fixture->expire)
      CHECK(platform_event_stream_write(stream, &prefix, 1, write_done, fixture) == ASCIICHAT_OK);
    CHECK(platform_event_stream_write(stream, &buffer, 1, write_done, fixture) == ASCIICHAT_OK);
    // Submission copies the caller's data before returning.
    memset(bytes, 0xa5, length);
    buffer_release(bytes);
    if (!fixture->expire)
      CHECK(platform_event_stream_write(stream, &suffix, 1, write_done, fixture) == ASCIICHAT_OK);
    if (fixture->drain) {
      platform_event_stream_request_close(stream, ASCIICHAT_OK);
      CHECK(platform_event_stream_write(stream, &suffix, 1, NULL, NULL) != ASCIICHAT_OK);
      CLEAR_ERRNO_ALL();
    }
  } else if (fixture->cancel) {
    char bytes[65536] = {0};
    socket_buffer_t buffer = {bytes, sizeof(bytes)};
    for (unsigned i = 0; i < 256; ++i)
      CHECK(platform_event_stream_write(stream, &buffer, 1, write_done, fixture) == ASCIICHAT_OK);
    CHECK(platform_event_stream_write(stream, &buffer, 1, write_done, fixture) == ERROR_NETWORK_SIZE);
    CHECK(platform_event_stream_queued_bytes(stream) == 16 * 1024 * 1024);
    CLEAR_ERRNO_ALL();
    platform_event_stream_close(stream, ERROR_NETWORK);
  }
}
static void *run_loop(void *arg) {
  fixture_t *fixture = arg;
  socket_io_stats_reset();
  CHECK(platform_event_loop_run(fixture->loop) == ASCIICHAT_OK);
  fixture->stats = socket_io_stats_get();
  return NULL;
}

static void event_test(bool expire, bool cancel, bool push, bool zerocopy, bool drain) {
  fixture_t fixture = {.expire = expire, .cancel = cancel, .push = push, .zerocopy = zerocopy, .drain = drain};
  CHECK(platform_event_loop_create(&fixture.loop) == ASCIICHAT_OK);
  socket_t listener = socket_create("event-test-listener", AF_INET, SOCK_STREAM, 0);
  CHECK(listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  CHECK(socket_listen(listener, 16) == 0);
  socklen_t size = sizeof(address);
  CHECK(socket_getsockname(listener, (struct sockaddr *)&address, &size) == 0);
  platform_event_listener_t *event_listener;
  CHECK(platform_event_listen(fixture.loop, &listener, accepted, &fixture, &event_listener) == ASCIICHAT_OK);
  CHECK(listener == INVALID_SOCKET_VALUE);
  asciichat_thread_t thread;
  CHECK(asciichat_thread_create(&thread, "event_test", run_loop, &fixture) == ASCIICHAT_OK);
  socket_t client = socket_create("event-test-client", AF_INET, SOCK_STREAM, 0);
  if (push && expire) {
    int receive_buffer = 4096;
    CHECK(socket_setsockopt(client, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer)) == 0);
  }
  CHECK(socket_connect(client, (struct sockaddr *)&address, sizeof(address)) == 0);
  if (push && !expire) {
    char received[256 * 1024 + 12];
    CHECK(recv_with_timeout(client, received, sizeof(received), 10 * NS_PER_SEC_INT) == (ssize_t)sizeof(received));
    CHECK(memcmp(received, "prefix", 6) == 0);
    for (size_t i = 6; i < sizeof(received) - 6; ++i)
      CHECK(received[i] == 0x5a);
    CHECK(memcmp(received + sizeof(received) - 6, "suffix", 6) == 0);
    if (!drain)
      CHECK(send_with_timeout(client, "!", 1, NS_PER_SEC_INT) == 1);
    char byte;
    CHECK(recv_with_timeout(client, &byte, 1, 5 * NS_PER_SEC_INT) <= 0);
    CLEAR_ERRNO_ALL();
  } else if (!expire && !cancel) {
    char expected[256 * 1024], received[sizeof(expected)];
    memset(expected, 0x5a, sizeof(expected));
    CHECK(send_with_timeout(client, expected, sizeof(expected), 10 * NS_PER_SEC_INT) == (ssize_t)sizeof(expected));
    CHECK(recv_with_timeout(client, received, sizeof(received), 10 * NS_PER_SEC_INT) == (ssize_t)sizeof(received));
    CHECK(memcmp(expected, received, sizeof(expected)) == 0);
    socket_shutdown(client, SHUT_WR);
  } else if (!push) {
    char byte;
    CHECK(recv_with_timeout(client, &byte, 1, 5 * NS_PER_SEC_INT) <= 0);
    CLEAR_ERRNO_ALL();
  }
  CHECK(asciichat_thread_join(&thread, NULL) == ASCIICHAT_OK);
  CHECK(fixture.accepted == 1 && fixture.closed == 1);
  CHECK(fixture.completions == (push ? (expire ? 1 : 3) : cancel ? 256 : fixture.jobs));
  if (push && !expire) {
    CHECK(fixture.stats.zerocopy_calls == fixture.stats.zerocopy_completions);
    log_warn("Event output: zerocopy=%d submissions=%llu completions=%llu copied=%llu", zerocopy,
             (unsigned long long)fixture.stats.zerocopy_calls, (unsigned long long)fixture.stats.zerocopy_completions,
             (unsigned long long)fixture.stats.zerocopy_copied);
  }
  CHECK(platform_event_loop_destroy(fixture.loop) == ASCIICHAT_OK);
  socket_close(client);
}

int main(void) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  decoder_tests();
  event_test(false, false, false, false, false);
  event_test(true, false, false, false, false);
  event_test(false, true, false, false, false);
  event_test(false, false, true, false, false);
  event_test(false, false, true, true, false);
  event_test(false, true, false, true, false);
  event_test(true, false, true, false, false);
  event_test(true, false, true, true, false);
  event_test(false, false, true, false, true);
  event_test(false, false, true, true, true);
  buffer_stats_t buffers;
  buffer_domain_stats(buffer_default_domain(), &buffers);
  CHECK(buffers.outstanding == 0);
  buffer_system_cleanup();
  CHECK(buffer_process_reserved() == 0);
  socket_cleanup();
  log_warn("Event I/O checks passed (%s)", platform_event_backend());
  log_destroy();
  return 0;
}
