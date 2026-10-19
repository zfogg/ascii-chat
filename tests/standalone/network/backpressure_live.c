#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <rtc/rtc.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/buffer.h>
#include <libwebsockets.h>
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    if (!(x))                                                                                                          \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #x);                                                      \
  } while (0)
#define META_COUNT 8192

typedef struct fixture fixture_t;
typedef struct endpoint {
  fixture_t *fixture;
  struct endpoint *other;
  webrtc_peer_connection_t *pc;
  webrtc_data_channel_t *dc;
  struct sockaddr_in address;
  atomic_t opened;
} endpoint_t;

typedef struct {
  uint32_t crc;
  uint64_t captured;
} frame_meta_t;
struct fixture {
  acip_transport_t *sender;
  socket_t receiver, relay;
  packet_decoder_t decoder;
  struct lws_context *context;
  struct lws *wsi;
  endpoint_t endpoints[2];
  asciichat_thread_t relay_thread, service_thread, proxy_thread;
  socket_t proxy_listener;
  struct sockaddr_in proxy_target;
  atomic_t proxy_running;
  atomic_t running, gated, relayed, throttled;
  mutex_t mutex;
  frame_meta_t meta[META_COUNT];
  size_t meta_next;
  uint64_t start, generated[3], delivered[3], controls[3], keyframes[3], rejected, age_max[3], age_sum[3];
  size_t peak_queue, peak_backend;
  uint64_t last_delivered_age, last_audio_sequence, steady_frames, steady_age_sum, next_read, wake_due,
      recovery_requests;
  AVCodecContext *codec;
  AVFrame *decoded;
  AVPacket *packet;
  enum AVCodecID codec_id;
  bool encoded, slow, was_gated;
};

static int phase(fixture_t *f) {
  double elapsed = (double)(time_get_ns() - f->start) / NS_PER_SEC_INT;
  return elapsed < 2 ? 0 : elapsed < 5.0 ? 1 : 2;
}

static asciichat_error_t received(const void *data, size_t length, void *arg) {
  fixture_t *f = arg;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  uint16_t type = NET_TO_HOST_U16(header.type);
  uint32_t crc = NET_TO_HOST_U32(header.crc32);
  mutex_lock(&f->mutex);
  int stage = phase(f);
  if (type == PACKET_TYPE_PING || type == PACKET_TYPE_AUDIO_BATCH) {
    if (type == PACKET_TYPE_AUDIO_BATCH) {
      uint64_t sequence;
      memcpy(&sequence, (const uint8_t *)data + sizeof(header), sizeof(sequence));
      CHECK(sequence == f->last_audio_sequence + 1);
      f->last_audio_sequence = sequence;
    }
    ++f->controls[stage];
    mutex_unlock(&f->mutex);
    return ASCIICHAT_OK;
  }
  uint64_t captured = 0;
  for (size_t i = 0; i < META_COUNT; ++i)
    if (f->meta[i].crc == crc && f->meta[i].captured) {
      captured = f->meta[i].captured;
      break;
    }
  CHECK(captured);
  bool keyframe = false;
  if (f->encoded) {
    CHECK(length > sizeof(header) + 5);
    const uint8_t *payload = (const uint8_t *)data + sizeof(header);
    keyframe = (payload[0] & 1) != 0;
    if (keyframe) {
      /* Recreate the decoder on every IDR. This proves parameter sets and
       * picture references are sufficient without previously delivered frames. */
      avcodec_free_context(&f->codec);
      const AVCodec *codec = avcodec_find_decoder(f->codec_id);
      CHECK(codec);
      f->codec = avcodec_alloc_context3(codec);
      CHECK(f->codec && avcodec_open2(f->codec, codec, NULL) >= 0);
    }
    CHECK(f->codec);
    CHECK(av_new_packet(f->packet, (int)(length - sizeof(header) - 5)) == 0);
    memcpy(f->packet->data, payload + 5, f->packet->size);
    CHECK(avcodec_send_packet(f->codec, f->packet) >= 0);
    av_packet_unref(f->packet);
    CHECK(avcodec_receive_frame(f->codec, f->decoded) == 0);
    CHECK(f->decoded->decode_error_flags == 0 && !(f->decoded->flags & AV_FRAME_FLAG_CORRUPT));
    CHECK(f->decoded->width == 160 && f->decoded->height == 90);
    av_frame_unref(f->decoded);
  }
  uint64_t age = time_get_ns() - captured;
  f->last_delivered_age = age;
  f->age_sum[stage] += age;
  uint64_t delivered_at = time_get_ns() - f->start;
  if (delivered_at >= 13000 * NS_PER_MS_INT && delivered_at < 15000 * NS_PER_MS_INT) {
    ++f->steady_frames;
    f->steady_age_sum += age;
  }
  if (age > f->age_max[stage])
    f->age_max[stage] = age;
  ++f->delivered[stage];
  if (keyframe)
    ++f->keyframes[stage];
  mutex_unlock(&f->mutex);
  return ASCIICHAT_OK;
}

static void feed(fixture_t *f, const void *bytes, size_t length) {
  CHECK(packet_decoder_feed(&f->decoder, bytes, length, received, f) == ASCIICHAT_OK);
}

static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  (void)user;
  fixture_t *f = lws_context_user(lws_get_context(wsi));
  if (reason == LWS_CALLBACK_ESTABLISHED) {
    f->wsi = wsi;
    socket_optimize_for_streaming(lws_get_socket_fd(wsi));

  } else if (reason == LWS_CALLBACK_RECEIVE) {
    feed(f, in, len);
  } else if (reason == LWS_CALLBACK_CLOSED || reason == LWS_CALLBACK_WSI_DESTROY) {
    if (f->wsi == wsi)
      f->wsi = NULL;
  }
  return 0;
}

static socket_t bound_socket(int type, struct sockaddr_in *address) {
  socket_t sock = socket_create("backpressure-test", AF_INET, type, 0);
  CHECK(sock != INVALID_SOCKET_VALUE);
  *address = (struct sockaddr_in){.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(sock, (struct sockaddr *)address, sizeof(*address)) == 0);
  socklen_t length = sizeof(*address);
  CHECK(socket_getsockname(sock, (struct sockaddr *)address, &length) == 0);
  return sock;
}

static void description(webrtc_peer_connection_t *pc, const char *sdp, const char *type, void *arg) {
  (void)pc;
  endpoint_t *endpoint = arg;
  char message_limit[64];
  safe_snprintf(message_limit, sizeof(message_limit), "a=max-message-size:%u", ACIP_OUTBOUND_SCTP_RESERVE);
  CHECK(strstr(sdp, message_limit));
  // Candidate callbacks advertise only the relay. Remove candidates embedded
  // in the SDP so ICE cannot silently select a direct loopback shortcut.
  char *filtered = SAFE_MALLOC(strlen(sdp) + 1, char *);
  size_t output = 0;
  const char *line = sdp;
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t size = end ? (size_t)(end - line + 1) : strlen(line);
    if (strncmp(line, "a=candidate:", 12) && strncmp(line, "a=end-of-candidates", 19)) {
      memcpy(filtered + output, line, size);
      output += size;
    }
    line += size;
  }
  filtered[output] = 0;
  CHECK(webrtc_set_remote_description(endpoint->other->pc, filtered, type) == ASCIICHAT_OK);
  SAFE_FREE(filtered);
}

static void candidate(webrtc_peer_connection_t *pc, const char *ice, const char *mid, void *arg) {
  (void)pc;
  endpoint_t *endpoint = arg;
  char foundation[128], protocol[32], ip[128];
  unsigned component, priority, port;
  int consumed = 0;
  CHECK(sscanf(ice, "%127s %u %31s %u %127s %u%n", foundation, &component, protocol, &priority, ip, &port, &consumed) ==
        6);
  if (strcmp(protocol, "UDP") && strcmp(protocol, "udp"))
    return;
  mutex_lock(&endpoint->fixture->mutex);
  endpoint->address = (struct sockaddr_in){.sin_family = AF_INET,
                                           .sin_port = HOST_TO_NET_U16((uint16_t)port),
                                           .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  mutex_unlock(&endpoint->fixture->mutex);
  struct sockaddr_in relay;
  socklen_t size = sizeof(relay);
  CHECK(socket_getsockname(endpoint->fixture->relay, (struct sockaddr *)&relay, &size) == 0);
  char rewritten[2048];
  safe_snprintf(rewritten, sizeof(rewritten), "%s %u %s %u 127.0.0.1 %u%s", foundation, component, protocol, priority,
                NET_TO_HOST_U16(relay.sin_port), ice + consumed);
  CHECK(webrtc_add_remote_candidate(endpoint->other->pc, rewritten, mid) == ASCIICHAT_OK);
}

static void opened(webrtc_data_channel_t *dc, void *arg) {
  endpoint_t *endpoint = arg;
  endpoint->dc = dc;
  atomic_store_bool(&endpoint->opened, true);
}

static void message(webrtc_data_channel_t *dc, const uint8_t *data, size_t length, void *arg) {
  (void)dc;
  endpoint_t *endpoint = arg;
  feed(endpoint->fixture, data, length);
}

/* Forward through a bounded, paced UDP relay during saturation. Keeping packets
 * queued rather than blackholing them preserves SCTP congestion recovery. */
static void *relay_loop(void *arg) {
  fixture_t *f = arg;
  typedef struct {
    struct sockaddr_in target;
    size_t size;
    uint8_t data[2048];
  } datagram_t;
  datagram_t *pending = SAFE_CALLOC(1024, sizeof(*pending), datagram_t *);
  size_t head = 0, count = 0;
  uint64_t next = 0;
  uint8_t buffer[65536];
  while (atomic_load_bool(&f->running)) {
    struct sockaddr_in source;
    socklen_t length = sizeof(source);
    ssize_t n = socket_recvfrom(f->relay, buffer, sizeof(buffer), 0, (struct sockaddr *)&source, &length);
    if (n > 0) {
      CHECK(n <= 2048 && count < 1024);
      mutex_lock(&f->mutex);
      struct sockaddr_in target =
          source.sin_port == f->endpoints[0].address.sin_port ? f->endpoints[1].address : f->endpoints[0].address;
      mutex_unlock(&f->mutex);
      datagram_t *entry = &pending[(head + count++) % 1024];
      entry->target = target;
      entry->size = (size_t)n;
      memcpy(entry->data, buffer, entry->size);
    }
    uint64_t now = time_get_ns();
    bool gated = atomic_load_bool(&f->gated);
    while (count && (!gated || now >= next)) {
      datagram_t *entry = &pending[head];
      atomic_fetch_add_u64(&f->relayed, 1);
      if (gated)
        atomic_fetch_add_u64(&f->throttled, 1);
      if (entry->target.sin_port)
        socket_sendto(f->relay, entry->data, entry->size, 0, (struct sockaddr *)&entry->target, sizeof(entry->target));
      head = (head + 1) % 1024;
      --count;
      next = now + 4 * NS_PER_MS_INT;
      if (gated)
        break;
    }
    if (n <= 0)
      platform_sleep_us(0);
  }
  SAFE_FREE(pending);
  return NULL;
}

static void pump(fixture_t *f, const char *transport) {
  bool gated = atomic_load_bool(&f->gated);
  uint64_t now = time_get_ns();
  bool read_ready = !gated || now >= f->next_read;
  if (!strcmp(transport, "tcp") && gated != f->was_gated) {
    int size = gated ? 64 * 1024 : 2 * 1024 * 1024;
    CHECK(socket_setsockopt(f->receiver, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == 0);
    f->was_gated = gated;
  }
  if (!strcmp(transport, "tcp") && read_ready) {
    uint8_t buffer[65536];
    if (gated)
      f->next_read = now + 64 * NS_PER_MS_INT;
    for (;;) {
      ssize_t length = socket_recv(f->receiver, buffer, gated ? 4096 : sizeof(buffer), 0);
      if (length <= 0)
        break;
      feed(f, buffer, (size_t)length);
      if (gated)
        break;
    }
  }
  if (!strcmp(transport, "websocket") && now >= f->wake_due) {
    lws_cancel_service(f->context);
    f->wake_due = now + NS_PER_MS_INT;
  }
}

static void *websocket_receiver_loop(void *arg) {
  fixture_t *f = arg;
  while (atomic_load_bool(&f->running)) {
    lws_service(f->context, 1);
    platform_sleep_us(0);
  }
  return NULL;
}

/* A transparent TCP relay throttles WebSocket wire bytes without modifying
 * libwebsockets receive state or coupling its service loop to video encoding. */
static void *tcp_proxy_loop(void *arg) {
  fixture_t *f = arg;
  socket_t incoming = socket_accept(f->proxy_listener, NULL, NULL, "proxy_in");
  CHECK(incoming != INVALID_SOCKET_VALUE);
  socket_close(f->proxy_listener);
  socket_t outgoing = socket_create("proxy_out", AF_INET, SOCK_STREAM, 0);
  CHECK(socket_connect(outgoing, (struct sockaddr *)&f->proxy_target, sizeof(f->proxy_target)) == 0);
  socket_optimize_for_streaming(incoming);
  socket_optimize_for_streaming(outgoing);
  int receive_size = 64 * 1024;
  CHECK(socket_setsockopt(incoming, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof(receive_size)) == 0);
  socket_set_nonblocking(incoming, true);
  socket_set_nonblocking(outgoing, true);
  uint8_t buffers[2][65536];
  size_t sizes[2] = {0}, offsets[2] = {0};
  uint64_t next = 0;
  while (atomic_load_bool(&f->proxy_running)) {
    bool stop = false;
    for (int direction = 0; direction < 2 && !stop; ++direction) {
      socket_t source = direction ? outgoing : incoming, target = direction ? incoming : outgoing;
      bool limited = !direction && atomic_load_bool(&f->gated);
      for (int burst = 0; burst < 16; ++burst) {
        uint64_t now = time_get_ns();
        if (limited && now < next)
          break;
        if (offsets[direction] == sizes[direction]) {
          ssize_t n = socket_recv(source, buffers[direction], limited ? 4096 : sizeof(buffers[direction]), 0);
          if (!n) {
            stop = true;
            break;
          }
          if (n < 0) {
            if (!socket_is_would_block_error(socket_get_last_error()))
              stop = true;
            break;
          }
          sizes[direction] = (size_t)n;
          offsets[direction] = 0;
        }
        size_t amount = sizes[direction] - offsets[direction];
        if (limited && amount > 4096)
          amount = 4096;
        ssize_t sent = socket_send(target, buffers[direction] + offsets[direction], amount, 0);
        if (sent > 0) {
          offsets[direction] += (size_t)sent;
          if (limited) {
            next = now + 64 * NS_PER_MS_INT;
            break;
          }
        } else {
          if (sent < 0 && !socket_is_would_block_error(socket_get_last_error()))
            stop = true;
          break;
        }
      }
    }
    if (stop)
      break;
    platform_sleep_ms(1);
  }
  socket_close(incoming);
  socket_close(outgoing);
  return NULL;
}

static void setup(fixture_t *f, const char *transport, bool slow) {
  f->slow = slow;
  CHECK(mutex_init(&f->mutex, "backpressure_fixture") == ASCIICHAT_OK);
  f->start = time_get_ns();
  f->receiver = f->relay = INVALID_SOCKET_VALUE;
  struct sockaddr_in address;
  if (!strcmp(transport, "tcp")) {
    socket_t listener = bound_socket(SOCK_STREAM, &address);
    int receive_size = 64 * 1024;
    CHECK(socket_setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof(receive_size)) == 0);
    CHECK(socket_listen(listener, 1) == 0);
    socket_t outgoing = socket_create("backpressure-outgoing", AF_INET, SOCK_STREAM, 0);
    CHECK(socket_connect(outgoing, (struct sockaddr *)&address, sizeof(address)) == 0);
    f->receiver = socket_accept(listener, NULL, NULL, "backpressure-incoming");
    CHECK(f->receiver != INVALID_SOCKET_VALUE);
    socket_close(listener);
    socket_optimize_for_streaming(outgoing);
    socket_optimize_for_streaming(f->receiver);
    CHECK(socket_setsockopt(f->receiver, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof(receive_size)) == 0);

    socket_set_nonblocking(f->receiver, true);
    f->sender = acip_async_tcp_client_create(&outgoing, NULL);
  } else if (!strcmp(transport, "websocket")) {
    socket_t reserve = bound_socket(SOCK_STREAM, &address);
    socket_close(reserve);
    static const struct lws_protocols protocols[] = {{"acip", websocket_callback, 0, 512 * 1024, 0, NULL, 0},
                                                     LWS_PROTOCOL_LIST_TERM};
    struct lws_context_creation_info info = {0};
    info.port = NET_TO_HOST_U16(address.sin_port);
    info.protocols = protocols;
    info.user = f;
    info.iface = "127.0.0.1";
    f->context = lws_create_context(&info);
    CHECK(f->context);
    atomic_store_bool(&f->running, true);
    CHECK(asciichat_thread_create(&f->service_thread, "websocket_receiver", websocket_receiver_loop, f) ==
          ASCIICHAT_OK);
    f->proxy_target = address;
    struct sockaddr_in proxy_address;
    f->proxy_listener = bound_socket(SOCK_STREAM, &proxy_address);
    int receive_size = 64 * 1024;
    CHECK(socket_setsockopt(f->proxy_listener, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof(receive_size)) == 0);
    CHECK(socket_listen(f->proxy_listener, 1) == 0);
    atomic_store_bool(&f->proxy_running, true);
    CHECK(asciichat_thread_create(&f->proxy_thread, "tcp_proxy", tcp_proxy_loop, f) == ASCIICHAT_OK);
    char url[128];
    safe_snprintf(url, sizeof(url), "ws://127.0.0.1:%u", NET_TO_HOST_U16(proxy_address.sin_port));
    f->sender = acip_websocket_client_transport_create("backpressure", url, NULL);
    CHECK(f->sender);
    uint64_t start = time_get_ns();
    while (!acip_transport_is_connected(f->sender) && time_get_ns() - start < 5 * NS_PER_SEC_INT) {
      pump(f, transport);
      platform_sleep_ms(1);
    }
    CHECK(acip_transport_is_connected(f->sender));
  } else {
    f->relay = bound_socket(SOCK_DGRAM, &address);
    socket_set_nonblocking(f->relay, true);
    atomic_store_bool(&f->running, true);
    CHECK(asciichat_thread_create(&f->relay_thread, "test_udp_relay", relay_loop, f) == ASCIICHAT_OK);
    for (int i = 0; i < 2; ++i) {
      endpoint_t *e = &f->endpoints[i];
      e->fixture = f;
      e->other = &f->endpoints[1 - i];
      webrtc_config_t config = {.bind_address = "127.0.0.1",
                                .on_local_description = description,
                                .on_local_candidate = candidate,
                                .on_datachannel_open = opened,
                                .on_datachannel_message = i == 1 ? message : NULL,
                                .user_data = e};
      CHECK(webrtc_create_peer_connection(&config, &e->pc) == ASCIICHAT_OK);
    }
    CHECK(webrtc_create_datachannel(f->endpoints[0].pc, "acip", &f->endpoints[0].dc) == ASCIICHAT_OK);
    CHECK(webrtc_create_offer(f->endpoints[0].pc) == ASCIICHAT_OK);
    uint64_t start = time_get_ns();
    while ((!atomic_load_bool(&f->endpoints[0].opened) || !atomic_load_bool(&f->endpoints[1].opened)) &&
           time_get_ns() - start < 15 * NS_PER_SEC_INT)
      platform_sleep_ms(5);
    CHECK(atomic_load_bool(&f->endpoints[0].opened) && atomic_load_bool(&f->endpoints[1].opened));
    for (int i = 0; i < 2; ++i) {
      size_t max_message_size = 0;
      CHECK(webrtc_datachannel_get_max_message_size(f->endpoints[i].dc, &max_message_size) == ASCIICHAT_OK);
      CHECK(max_message_size == ACIP_OUTBOUND_SCTP_RESERVE);
    }
    char local[1024], remote[1024], foundation[128], protocol[32], ip[128];
    unsigned component, priority, port;
    CHECK(rtcGetSelectedCandidatePair(webrtc_get_rtc_id(f->endpoints[0].pc), local, sizeof(local), remote,
                                      sizeof(remote)) >= 0);
    CHECK(sscanf(remote, "%127s %u %31s %u %127s %u", foundation, &component, protocol, &priority, ip, &port) == 6);
    CHECK(port == NET_TO_HOST_U16(address.sin_port));
    f->sender = acip_webrtc_transport_create(f->endpoints[0].pc, f->endpoints[0].dc, NULL);
  }
  CHECK(f->sender);
}

static void teardown(fixture_t *f, const char *transport) {
  acip_transport_destroy(f->sender);
  if (f->context) {
    atomic_store_bool(&f->proxy_running, false);
    asciichat_thread_join(&f->proxy_thread, NULL);
    atomic_store_bool(&f->running, false);
    lws_cancel_service(f->context);
    asciichat_thread_join(&f->service_thread, NULL);
    lws_context_destroy(f->context);
  }
  if (f->receiver != INVALID_SOCKET_VALUE)
    socket_close(f->receiver);
  if (!strcmp(transport, "webrtc")) {
    for (int i = 0; i < 2; ++i)
      webrtc_datachannel_destroy(f->endpoints[i].dc);
    for (int i = 0; i < 2; ++i)
      webrtc_peer_connection_destroy(f->endpoints[i].pc);
    atomic_store_bool(&f->running, false);
    asciichat_thread_join(&f->relay_thread, NULL);
    socket_close(f->relay);
  }
  packet_decoder_reset(&f->decoder);
  avcodec_free_context(&f->codec);
  av_frame_free(&f->decoded);
  av_packet_free(&f->packet);
  mutex_destroy(&f->mutex);
}

int main(int argc, char **argv) {
  CHECK(argc == 4);
  const char *transport = argv[1], *encoding = argv[2];
  log_init(NULL, LOG_ERROR, false, false);
  asciichat_errno_suppress(true);
  av_log_set_level(AV_LOG_ERROR);
  lws_set_log_level(LLL_ERR, NULL);
  CHECK(platform_init() == ASCIICHAT_OK);
  CHECK(webrtc_init() == ASCIICHAT_OK);
  fixture_t *f = SAFE_CALLOC(1, sizeof(*f), fixture_t *);
  setup(f, transport, true);
  fixture_t *healthy = SAFE_CALLOC(1, sizeof(*healthy), fixture_t *);
  setup(healthy, transport, false);
  bool encoded = strcmp(encoding, "raw") != 0;
  f->encoded = healthy->encoded = encoded;
  f->codec_id = !strcmp(encoding, "h264") ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
  healthy->codec_id = f->codec_id;
  healthy->decoded = av_frame_alloc();
  healthy->packet = av_packet_alloc();
  f->decoded = av_frame_alloc();
  f->packet = av_packet_alloc();
  h265_encoder_t *encoder =
      encoded ? video_encoder_create(!strcmp(encoding, "h264") ? VIDEO_CODEC_H264 : VIDEO_CODEC_H265, 160, 90) : NULL;
  CHECK(!encoded || encoder);
  size_t capacity = 160 * 90 * 3 + 65536;
  uint8_t *rgb = SAFE_MALLOC(160 * 90 * 3, uint8_t *), *payload = SAFE_MALLOC(capacity, uint8_t *);
  uint64_t frame_number = 0;
  uint16_t type = !encoded                    ? PACKET_TYPE_IMAGE_FRAME
                  : !strcmp(encoding, "h264") ? PACKET_TYPE_IMAGE_FRAME_H264
                                              : PACKET_TYPE_IMAGE_FRAME_H265;
  FILE *csv = fopen(argv[3], "w");
  CHECK(csv);
  fputs("elapsed_s,phase,application_bytes,backend_bytes,backend_reserved_bytes,oldest_queued_frame_ms,delivered_"
        "frames,dropped_frames,last_delivered_frame_age_ms\n",
        csv);
  if (encoded) {
    memset(rgb, 0, 160 * 90 * 3);
    size_t warmup_size = capacity;
    CHECK(h265_encode(encoder, 160, 90, rgb, payload, &warmup_size) == ASCIICHAT_OK);
  }
  healthy->start = f->start = time_get_ns();
  uint64_t next = f->start, end = f->start + 15000 * NS_PER_MS_INT;
  size_t length = 0;
  while (time_get_ns() < end) {
    int stage = phase(f);
    atomic_store_bool(&f->gated, stage == 1);
    pump(f, transport);
    pump(healthy, transport);
    uint64_t now = time_get_ns();
    if (now < next) {
      platform_sleep_ms(1);
      continue;
    }
    next = now + NS_PER_SEC_INT / 60;
    uint64_t captured = now;
    ++f->generated[stage];
    uint32_t seed = (uint32_t)++frame_number;
    for (size_t i = 0; i < 160 * 90 * 3; ++i) {
      seed = seed * 1664525U + 1013904223U;
      rgb[i] = (uint8_t)(seed >> 24);
    }
    length = capacity;
    if (encoded) {
      bool slow_needs_keyframe = acip_outbound_needs_keyframe(f->sender, type, 0);
      if (slow_needs_keyframe && frame_number > 1)
        ++f->recovery_requests;
      if (slow_needs_keyframe || acip_outbound_needs_keyframe(healthy->sender, type, 0))
        h265_encoder_request_keyframe(encoder);
      CHECK(h265_encode(encoder, 160, 90, rgb, payload, &length) == ASCIICHAT_OK && length);
    } else {
      image_frame_packet_t image = {.width = HOST_TO_NET_U32(80), .height = HOST_TO_NET_U32(45)};
      length = sizeof(image) + 80 * 45 * 3;
      memcpy(payload, &image, sizeof(image));
      memcpy(payload + sizeof(image), rgb, 80 * 45 * 3);
    }
    // Legal filler NAL units make codec wire load deterministic across encoder
    // bitrate defaults. The receiver still decodes the actual encoded picture.
    if (encoded && length < 16 * 1024 - 8) {
      payload[length++] = 0;
      payload[length++] = 0;
      payload[length++] = 0;
      payload[length++] = 1;
      payload[length++] = type == PACKET_TYPE_IMAGE_FRAME_H264 ? 12 : (38 << 1);
      if (type == PACKET_TYPE_IMAGE_FRAME_H265)
        payload[length++] = 1;
      memset(payload + length, 0xff, 16 * 1024 - length - 1);
      length = 16 * 1024;
      payload[length - 1] = 0x80;
    }
    uint32_t crc = asciichat_crc32(payload, length);
    mutex_lock(&f->mutex);
    f->meta[f->meta_next++ % META_COUNT] = (frame_meta_t){crc, captured};
    mutex_unlock(&f->mutex);
    mutex_lock(&healthy->mutex);
    healthy->meta[healthy->meta_next++ % META_COUNT] = (frame_meta_t){crc, captured};
    mutex_unlock(&healthy->mutex);
    asciichat_error_t healthy_result = packet_send_via_transport(healthy->sender, type, payload, length, 0);
    CHECK(healthy_result == ASCIICHAT_OK || healthy_result == ERROR_BUFFER_FULL);
    if (healthy_result != ASCIICHAT_OK)
      CLEAR_ERRNO();
    asciichat_error_t result = packet_send_via_transport(f->sender, type, payload, length, 0);
    if (result == ERROR_BUFFER_FULL) {
      ++f->rejected;
      CLEAR_ERRNO();
    } else
      CHECK(result == ASCIICHAT_OK);
    CHECK(packet_send_via_transport(f->sender, PACKET_TYPE_PING, NULL, 0, 0) == ASCIICHAT_OK);
    if (frame_number % 15 == 0) {
      uint8_t audio[16] = {0};
      uint64_t sequence = frame_number / 15;
      memcpy(audio, &sequence, sizeof(sequence));
      CHECK(packet_send_via_transport(f->sender, PACKET_TYPE_AUDIO_BATCH, audio, sizeof(audio), 0) == ASCIICHAT_OK);
    }
    acip_outbound_stats_t stats;
    CHECK(acip_outbound_snapshot(f->sender, &stats) == ASCIICHAT_OK);
    size_t queued = stats.application_bytes + stats.backend_bytes + stats.backend_reserved_bytes;
    CHECK(queued <= ACIP_OUTBOUND_BUDGET);
    if (queued > f->peak_queue)
      f->peak_queue = queued;
    if (stats.backend_bytes > f->peak_backend)
      f->peak_backend = stats.backend_bytes;
    mutex_lock(&f->mutex);
    fprintf(csv, "%.6f,%d,%zu,%zu,%zu,%.3f,%llu,%llu,%.3f\n", (double)(now - f->start) / NS_PER_SEC_INT, stage,
            stats.application_bytes, stats.backend_bytes, stats.backend_reserved_bytes,
            (double)stats.oldest_frame_age_ns / NS_PER_MS_INT,
            (unsigned long long)(f->delivered[0] + f->delivered[1] + f->delivered[2]),
            (unsigned long long)stats.dropped, (double)f->last_delivered_age / NS_PER_MS_INT);
    mutex_unlock(&f->mutex);
  }
  atomic_store_bool(&f->gated, false);
  for (int i = 0; i < 200; ++i) {
    pump(f, transport);
    pump(healthy, transport);
    platform_sleep_ms(1);
  }
  acip_outbound_stats_t stats;
  CHECK(acip_outbound_snapshot(f->sender, &stats) == ASCIICHAT_OK);
  double measured_seconds = (double)(time_get_ns() - f->start) / NS_PER_SEC_INT;
  double recovery_seconds = measured_seconds - 5.0, steady_seconds = 2.0;
  mutex_lock(&f->mutex);
  mutex_lock(&healthy->mutex);
  CHECK(stats.dropped + f->rejected > 0);
  CHECK(f->delivered[0] > 0 && f->delivered[2] > 0 && f->controls[2] > 0);
  CHECK(f->steady_frames > 0 && f->last_delivered_age < 250 * NS_PER_MS_INT);
  CHECK(f->steady_age_sum / f->steady_frames < 250 * NS_PER_MS_INT);
  CHECK(f->controls[1] > 0);
  if (encoded)
    CHECK(f->keyframes[2] > 0 && stats.recovery_frames > 1 && f->recovery_requests > 0);
  fclose(csv);
  log_error("RESULT transport=%s codec=%s peak_queue=%zu peak_backend=%zu dropped=%llu rejected=%llu baseline_fps=%.2f "
            "recovery_fps=%.2f baseline_age_ms=%.2f recovery_age_ms=%.2f recovery_max_age_ms=%.2f "
            "recovery_keyframes=%llu controls=%llu",
            transport, encoding, f->peak_queue, f->peak_backend, (unsigned long long)stats.dropped,
            (unsigned long long)f->rejected, (double)f->delivered[0] / 2, (double)f->delivered[2] / recovery_seconds,
            (double)f->age_sum[0] / (double)f->delivered[0] / NS_PER_MS_INT,
            (double)f->age_sum[2] / (double)f->delivered[2] / NS_PER_MS_INT, (double)f->age_max[2] / NS_PER_MS_INT,
            (unsigned long long)f->keyframes[2], (unsigned long long)f->controls[2]);
  log_error("HEALTHY transport=%s codec=%s baseline_fps=%.2f saturation_fps=%.2f recovery_fps=%.2f", transport,
            encoding, (double)healthy->delivered[0] / 2, (double)healthy->delivered[1] / 3.0,
            (double)healthy->delivered[2] / recovery_seconds);
  CHECK(healthy->delivered[0] > 0 && healthy->delivered[1] > 0 && healthy->delivered[2] > 0);
  log_error("SOURCE baseline_fps=%.2f saturation_fps=%.2f recovery_fps=%.2f", (double)f->generated[0] / 2,
            (double)f->generated[1] / 3, (double)f->generated[2] / recovery_seconds);
  CHECK(healthy->delivered[1] >= f->generated[1] * 0.90);
  if (!strcmp(transport, "webrtc")) {
    log_error("RELAY forwarded=%llu throttled=%llu", (unsigned long long)atomic_load_u64(&f->relayed),
              (unsigned long long)atomic_load_u64(&f->throttled));
    CHECK(atomic_load_u64(&f->throttled) > 0);
  }
  log_error("DURATION baseline_s=2 throttle_s=3 recovery_s=%.6f steady_s=%.6f", recovery_seconds, steady_seconds);
  log_error("STEADY fps=%.2f age_ms=%.2f controls_during_throttle=%llu recovery_requests=%llu",
            (double)f->steady_frames / steady_seconds,
            (double)f->steady_age_sum / (double)f->steady_frames / NS_PER_MS_INT, (unsigned long long)f->controls[1],
            (unsigned long long)f->recovery_requests);
  mutex_unlock(&healthy->mutex);
  mutex_unlock(&f->mutex);
  // Destroy with valid queued output. Replaying a dependent encoded packet
  // would itself corrupt the decoder and would not test queue cancellation.
  atomic_store_bool(&f->gated, true);
  for (int i = 0; i < 20; ++i) {
    if (encoded) {
      h265_encoder_request_keyframe(encoder);
      length = capacity;
      CHECK(h265_encode(encoder, 160, 90, rgb, payload, &length) == ASCIICHAT_OK);
    }
    mutex_lock(&f->mutex);
    f->meta[f->meta_next++ % META_COUNT] = (frame_meta_t){asciichat_crc32(payload, length), time_get_ns()};
    mutex_unlock(&f->mutex);
    asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
    packet_send_via_transport(f->sender, type, payload, length, 0);
    asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
  }
  teardown(f, transport);
  teardown(healthy, transport);
  SAFE_FREE(healthy);
  h265_encoder_destroy(encoder);
  SAFE_FREE(rgb);
  SAFE_FREE(payload);
  SAFE_FREE(f);
  webrtc_destroy();
  platform_destroy();
  buffer_stats_t buffers;
  buffer_domain_stats(buffer_default_domain(), &buffers);
  CHECK(buffers.outstanding == 0);
  buffer_system_cleanup();
  log_destroy();
  return 0;
}
