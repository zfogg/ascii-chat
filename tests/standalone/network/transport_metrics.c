/* Synthetic ACIP packet delivery measurements; this does not measure acoustic
 * latency, capture devices, or rendering. All peers use password authentication
 * and application encryption, in addition to QUIC TLS or WebRTC DTLS. */
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/acip/quic.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/buffer.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    if (!(x))                                                                                                          \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #x);                                                      \
  } while (0)
#define PASSWORD "transport-metrics-password"
#define SAMPLE_LIMIT 4096

typedef struct peer {
  struct peer *other;
  webrtc_peer_connection_t *pc;
  webrtc_data_channel_t *dc;
  acip_transport_t *transport;
  acip_security_t *security;
  atomic_t opened, ready;
} peer_t;

typedef struct {
  acip_transport_t *transport;
  crypto_handshake_context_t *handshake;
  asciichat_error_t result;
} client_negotiation_t;

typedef struct {
  uint64_t sequence, captured;
} stamp_t;

typedef struct {
  uint64_t ages[SAMPLE_LIMIT], count, bytes, last, max_gap, interruptions;
} samples_t;

typedef struct {
  acip_transport_t *transport;
  atomic_t running;
  uint64_t start, first_recovery[3], received[3], last_sequence[3];
  uint64_t out_of_order[3];
  bool datagrams;
  samples_t samples[2][3];
  asciichat_error_t result;
} receiver_t;

static void description(webrtc_peer_connection_t *pc, const char *sdp, const char *type, void *arg) {
  (void)pc;
  peer_t *p = arg;
  CHECK(webrtc_set_remote_description(p->other->pc, sdp, type) == ASCIICHAT_OK);
}
static void candidate(webrtc_peer_connection_t *pc, const char *ice, const char *mid, void *arg) {
  (void)pc;
  peer_t *p = arg;
  CHECK(webrtc_add_remote_candidate(p->other->pc, ice, mid) == ASCIICHAT_OK);
}
static void security_ready(acip_transport_t *transport, void *arg) {
  (void)transport;
  atomic_store_bool(&((peer_t *)arg)->ready, true);
}
static void opened(webrtc_data_channel_t *dc, void *arg) {
  peer_t *p = arg;
  p->dc = dc;
  atomic_store_bool(&p->opened, true);
}
static void wait_flag(atomic_t *flag) {
  uint64_t start = time_get_ns();
  while (!atomic_load_bool(flag) && time_get_ns() - start < 20 * NS_PER_SEC_INT)
    platform_sleep_ms(1);
  CHECK(atomic_load_bool(flag));
}
static void *client_negotiate(void *arg) {
  client_negotiation_t *n = arg;
  n->result = network_client_negotiate(n->transport, n->handshake, ACIP_CRYPTO_FULL, NETWORK_PROTOCOL_MEDIA);
  return NULL;
}
static void negotiate(acip_transport_t *sender, acip_transport_t *receiver, crypto_handshake_context_t handshake[2]) {
  for (int i = 0; i < 2; ++i)
    CHECK(crypto_handshake_init_with_password("transport-metrics", &handshake[i], i == 1, PASSWORD) == ASCIICHAT_OK);
  SAFE_STRNCPY(handshake[0].server_ip, "127.0.0.1", sizeof(handshake[0].server_ip));
  // This in-process fixture authenticates the shared password, without TOFU UI.
  handshake[0].addressless_transport = true;
  client_negotiation_t client = {.transport = sender, .handshake = &handshake[0]};
  network_negotiation_t server = {.protocol = NETWORK_PROTOCOL_MEDIA,
                                  .handshake = &handshake[1],
                                  .initialized = true,
                                  .password = PASSWORD,
                                  .require_auth = true,
                                  .require_encryption = true};
  asciichat_thread_t thread;
  CHECK(acthread_create(&thread, "metrics_negotiate", client_negotiate, &client) == ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  while (!server.ready && time_get_ns() - start < 20 * NS_PER_SEC_INT) {
    if (!acip_transport_has_pending_data(receiver)) {
      platform_sleep_ms(1);
      continue;
    }
    void *data = NULL, *owner = NULL;
    size_t length = 0;
    CHECK(acip_transport_recv(receiver, &data, &length, &owner) == ASCIICHAT_OK);
    CHECK(length >= sizeof(packet_header_t));
    packet_header_t header;
    memcpy(&header, data, sizeof(header));
    CHECK(network_negotiation_receive(&server, receiver, NET_TO_HOST_U16(header.type), (uint8_t *)data + sizeof(header),
                                      length - sizeof(header)) == ASCIICHAT_OK);
    buffer_release(owner);
  }
  CHECK(thread_join(&thread, NULL) == ASCIICHAT_OK);
  CHECK(server.ready && client.result == ASCIICHAT_OK);
}
static void *receive_loop(void *arg) {
  receiver_t *r = arg;
  while (atomic_load_bool(&r->running)) {
    if (!acip_transport_has_pending_data(r->transport)) {
      platform_sleep_ms(1);
      continue;
    }
    void *data = NULL, *owner = NULL;
    size_t length = 0;
    asciichat_error_t result = acip_transport_recv(r->transport, &data, &length, &owner);
    if (result != ASCIICHAT_OK) {
      if (atomic_load_bool(&r->running))
        r->result = result;
      break;
    }
    CHECK(length >= sizeof(packet_header_t) + sizeof(stamp_t));
    packet_header_t header;
    stamp_t stamp;
    memcpy(&header, data, sizeof(header));
    memcpy(&stamp, (uint8_t *)data + sizeof(header), sizeof(stamp));
    uint16_t type = NET_TO_HOST_U16(header.type);
    int flow = type == PACKET_TYPE_PING ? 0 : type == PACKET_TYPE_AUDIO_BATCH ? 1 : 2;
    uint64_t now = time_get_ns();
    CHECK(stamp.captured <= now);
    if (stamp.sequence <= r->last_sequence[flow]) {
      CHECK(r->datagrams && flow != 0);
      ++r->out_of_order[flow];
    } else
      r->last_sequence[flow] = stamp.sequence;
    int phase = stamp.captured - r->start < 5 * NS_PER_SEC_INT ? 0 : 1;
    samples_t *s = &r->samples[phase][flow];
    CHECK(s->count < SAMPLE_LIMIT);
    s->ages[s->count++] = now - stamp.captured;
    s->bytes += length;
    if (s->last) {
      uint64_t gap = now - s->last;
      if (gap > s->max_gap)
        s->max_gap = gap;
      if (flow == 1 && gap > 60 * NS_PER_MS_INT)
        ++s->interruptions;
    }
    s->last = now;
    if (phase && !r->first_recovery[flow] && now - stamp.captured < 100 * NS_PER_MS_INT)
      r->first_recovery[flow] = now - r->start - 5 * NS_PER_SEC_INT;
    ++r->received[flow];
    buffer_release(owner);
  }
  return NULL;
}
static int compare(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

int main(int argc, char **argv) {
  CHECK(argc == 4 || argc == 5);
  size_t visual_bytes = argc == 5 ? strtoul(argv[4], NULL, 10) : 32768;
  CHECK(visual_bytes >= 1024 && visual_bytes <= 262144);
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  CHECK(platform_init() == ASCIICHAT_OK);
  CHECK(webrtc_init() == ASCIICHAT_OK);
  const char *mode = argv[1];
  bool datagrams = !strcmp(mode, "quic-datagrams");
  uint64_t setup_start = time_get_ns();
  peer_t peers[2] = {0};
  crypto_handshake_context_t handshake[2] = {0};
  acip_quic_endpoint_t *endpoints[2] = {0};
  acip_transport_t *sender = NULL, *receiver = NULL;
  struct sockaddr_in local = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  if (!strcmp(mode, "webrtc")) {
    for (int i = 0; i < 2; ++i) {
      peer_t *p = &peers[i];
      p->other = &peers[1 - i];
      p->security = SAFE_CALLOC(1, sizeof(acip_security_t), acip_security_t *);
      p->security->mode = ACIP_CRYPTO_FULL;
      CHECK(crypto_handshake_init_with_password("metrics", &p->security->handshake, i == 1, PASSWORD) == ASCIICHAT_OK);
      webrtc_config_t config = {.bind_address = "127.0.0.1",
                                .on_local_description = description,
                                .on_local_candidate = candidate,
                                .on_datachannel_open = opened,
                                .user_data = p};
      CHECK(webrtc_create_peer_connection(&config, &p->pc) == ASCIICHAT_OK);
    }
    CHECK(webrtc_create_datachannel(peers[0].pc, "acip", &peers[0].dc) == ASCIICHAT_OK);
    CHECK(webrtc_create_offer(peers[0].pc) == ASCIICHAT_OK);
    for (int i = 0; i < 2; ++i)
      wait_flag(&peers[i].opened);
    for (int i = 0; i < 2; ++i) {
      peer_t *p = &peers[i];
      p->transport = acip_webrtc_transport_create_secure(p->pc, p->dc, p->security, security_ready, p);
      CHECK(p->transport);
    }
    CHECK(acip_security_start(peers[0].security, peers[0].transport) == ASCIICHAT_OK);
    for (int i = 0; i < 2; ++i)
      wait_flag(&peers[i].ready);
    sender = peers[0].transport;
    receiver = peers[1].transport;
  } else {
    if (!strcmp(mode, "quic") || datagrams) {
      acip_quic_config_t server = {
          .certificate_file = argv[2], .private_key_file = argv[3], .enable_datagrams = datagrams};
      acip_quic_config_t client = {.ca_file = argv[2],
                                   .enable_datagrams = datagrams,
                                   .prefer_audio_datagrams = datagrams,
                                   .prefer_visual_datagrams = datagrams};
      CHECK(quic_endpoint_open((struct sockaddr *)&local, sizeof(local), true, &server, &endpoints[1]) ==
            ASCIICHAT_OK);
      CHECK(quic_endpoint_open((struct sockaddr *)&local, sizeof(local), false, &client, &endpoints[0]) ==
            ASCIICHAT_OK);
      struct sockaddr_storage address;
      socklen_t length;
      CHECK(quic_endpoint_address(endpoints[1], &address, &length) == ASCIICHAT_OK);
      CHECK(quic_connect(endpoints[0], (struct sockaddr *)&address, length, "localhost", &sender) == ASCIICHAT_OK);
      uint64_t start = time_get_ns();
      while (!receiver && time_get_ns() - start < 20 * NS_PER_SEC_INT) {
        asciichat_error_t result = quic_accept(endpoints[1], &receiver);
        CHECK(result == ASCIICHAT_OK || result == ERROR_NETWORK_TIMEOUT);
        platform_sleep_ms(1);
      }
      CHECK(receiver);
      acip_transport_capabilities_t caps = {0};
      do {
        asciichat_error_t result = acip_transport_get_capabilities(sender, &caps);
        CHECK(result == ASCIICHAT_OK || result == ERROR_INVALID_STATE);
        if (!caps.independent_streams)
          platform_sleep_ms(1);
        CHECK(time_get_ns() - start < 20 * NS_PER_SEC_INT);
      } while (!caps.independent_streams);
    } else {
      CHECK(!strcmp(mode, "tcp"));
      socket_t listener = socket_create("metrics_listen", AF_INET, SOCK_STREAM, 0);
      CHECK(listener != INVALID_SOCKET_VALUE);
      CHECK(socket_bind(listener, (struct sockaddr *)&local, sizeof(local)) == 0);
      socklen_t length = sizeof(local);
      CHECK(socket_getsockname(listener, (struct sockaddr *)&local, &length) == 0);
      CHECK(socket_listen(listener, 1) == 0);
      socket_t outgoing = socket_create("metrics_client", AF_INET, SOCK_STREAM, 0);
      CHECK(socket_connect(outgoing, (struct sockaddr *)&local, sizeof(local)) == 0);
      socket_t incoming = socket_accept(listener, NULL, NULL, "metrics_server");
      CHECK(incoming != INVALID_SOCKET_VALUE);
      socket_close(listener);
      socket_optimize_for_streaming(outgoing);
      socket_optimize_for_streaming(incoming);
      sender = acip_async_tcp_client_create(&outgoing, NULL);
      receiver = acip_tcp_transport_create_owned("metrics", incoming, NULL);
      CHECK(sender && receiver);
    }
    negotiate(sender, receiver, handshake);
  }
  acip_transport_type_t expected = endpoints[0]  ? ACIP_TRANSPORT_QUIC
                                   : peers[0].pc ? ACIP_TRANSPORT_WEBRTC
                                                 : ACIP_TRANSPORT_TCP;
  CHECK(acip_transport_get_type(sender) == expected && acip_transport_get_type(receiver) == expected);
  if (datagrams) {
    acip_transport_capabilities_t caps;
    CHECK(acip_transport_get_capabilities(sender, &caps) == ASCIICHAT_OK && caps.datagrams &&
          caps.max_datagram_payload);
    CHECK(sender->prefer_audio_datagrams && sender->prefer_visual_datagrams);
  }
  log_warn("METRICS_SETUP transport=%s ms=%.3f", mode, (double)(time_get_ns() - setup_start) / NS_PER_MS_INT);
  receiver_t *r = SAFE_CALLOC(1, sizeof(receiver_t), receiver_t *);
  r->transport = receiver;
  r->start = time_get_ns();
  r->datagrams = datagrams;
  atomic_store_bool(&r->running, true);
  asciichat_thread_t thread;
  CHECK(acthread_create(&thread, "metrics_receiver", receive_loop, r) == ASCIICHAT_OK);
  log_warn("METRICS_READY");
  uint64_t due[3] = {r->start, r->start, r->start}, sequence[3] = {0}, sent[2][3] = {{0}}, rejected[2][3] = {{0}};
  size_t peak_queue = 0;
  uint64_t interval[3] = {100 * NS_PER_MS_INT, 20 * NS_PER_MS_INT, NS_PER_SEC_INT / 30};
  uint16_t types[3] = {PACKET_TYPE_PING, PACKET_TYPE_AUDIO_BATCH, PACKET_TYPE_IMAGE_FRAME};
  size_t sizes[3] = {32, 960, visual_bytes};
  uint8_t *payload = SAFE_CALLOC(1, visual_bytes, uint8_t *);
  while (time_get_ns() - r->start < 10 * NS_PER_SEC_INT) {
    uint64_t now = time_get_ns();
    int phase = now - r->start < 5 * NS_PER_SEC_INT ? 0 : 1;
    for (int flow = 0; flow < 3; ++flow)
      if (now >= due[flow]) {
        due[flow] = now + interval[flow];
        stamp_t stamp = {.sequence = ++sequence[flow], .captured = now};
        memcpy(payload, &stamp, sizeof(stamp));
        asciichat_error_t result = packet_send_via_transport(sender, types[flow], payload, sizes[flow], 0);
        CHECK(result == ASCIICHAT_OK || result == ERROR_BUFFER_FULL);
        if (result == ASCIICHAT_OK)
          ++sent[phase][flow];
        else
          ++rejected[phase][flow];
      }
    acip_outbound_stats_t stats;
    size_t queued;
    if (endpoints[0]) {
      CHECK(sender->methods->queued_bytes);
      queued = sender->methods->queued_bytes(sender);
    } else {
      CHECK(acip_outbound_snapshot(sender, &stats) == ASCIICHAT_OK);
      queued = stats.application_bytes + stats.backend_bytes + stats.backend_reserved_bytes;
    }
    CHECK(queued <= ACIP_OUTBOUND_BUDGET);
    if (queued > peak_queue)
      peak_queue = queued;
    platform_sleep_ms(1);
  }
  platform_sleep_ms(250);
  atomic_store_bool(&r->running, false);
  acip_transport_interrupt(receiver);
  CHECK(thread_join(&thread, NULL) == ASCIICHAT_OK && r->result == ASCIICHAT_OK);
  for (int phase = 0; phase < 2; ++phase)
    for (int flow = 0; flow < 3; ++flow) {
      samples_t *s = &r->samples[phase][flow];
      if (!s->count && datagrams && phase == 0 && flow == 2) {
        log_warn("METRICS phase=%d flow=%d count=0 sent=%llu rejected=%llu", phase, flow,
                 (unsigned long long)sent[phase][flow], (unsigned long long)rejected[phase][flow]);
        continue;
      }
      CHECK(s->count);
      qsort(s->ages, s->count, sizeof(uint64_t), compare);
      log_warn("METRICS phase=%d flow=%d count=%llu p50_ms=%.3f p95_ms=%.3f max_ms=%.3f max_gap_ms=%.3f "
               "interruptions=%llu bytes=%llu recovery_ms=%.3f sent=%llu rejected=%llu reordered=%llu",
               phase, flow, (unsigned long long)s->count, (double)s->ages[s->count / 2] / NS_PER_MS_INT,
               (double)s->ages[(s->count - 1) * 95 / 100] / NS_PER_MS_INT,
               (double)s->ages[s->count - 1] / NS_PER_MS_INT, (double)s->max_gap / NS_PER_MS_INT,
               (unsigned long long)s->interruptions, (unsigned long long)s->bytes,
               (double)r->first_recovery[flow] / NS_PER_MS_INT, (unsigned long long)sent[phase][flow],
               (unsigned long long)rejected[phase][flow], (unsigned long long)r->out_of_order[flow]);
    }
  for (int flow = 0; flow < 3; ++flow)
    CHECK(r->first_recovery[flow]);
  log_warn("METRICS_QUEUE peak_bytes=%zu limit_bytes=%u", peak_queue, ACIP_OUTBOUND_BUDGET);
  if (endpoints[0])
    for (int i = 0; i < 2; ++i)
      CHECK(quic_endpoint_stop(endpoints[i]) == ASCIICHAT_OK);
  acip_transport_destroy(sender);
  acip_transport_destroy(receiver);
  if (!strcmp(mode, "webrtc"))
    for (int i = 0; i < 2; ++i) {
      webrtc_datachannel_destroy(peers[i].dc);
      webrtc_peer_connection_destroy(peers[i].pc);
    }
  else {
    for (int i = 0; i < 2; ++i)
      crypto_handshake_destroy(&handshake[i]);
    if (endpoints[0])
      for (int i = 0; i < 2; ++i)
        CHECK(quic_endpoint_destroy(&endpoints[i]) == ASCIICHAT_OK);
  }
  SAFE_FREE(payload);
  SAFE_FREE(r);
  webrtc_destroy();
  platform_destroy();
  buffer_stats_t buffers;
  buffer_domain_stats(buffer_default_domain(), &buffers);
  CHECK(buffers.outstanding == 0);
  buffer_system_cleanup();
  log_destroy();
  return 0;
}
