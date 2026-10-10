/** Native transport correctness and reproducible loopback measurements. */
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)
#define BUDGET (10 * NS_PER_SEC_INT)

static void pair(socket_t sockets[2]) {
  socket_t listener = socket_create("vector-listener", AF_INET, SOCK_STREAM, 0);
  CHECK(listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  CHECK(socket_listen(listener, 1) == 0);
  socklen_t len = sizeof(addr);
  CHECK(socket_getsockname(listener, (struct sockaddr *)&addr, &len) == 0);
  sockets[0] = socket_create("vector-sender", AF_INET, SOCK_STREAM, 0);
  CHECK(socket_connect(sockets[0], (struct sockaddr *)&addr, sizeof(addr)) == 0);
  sockets[1] = socket_accept(listener, NULL, NULL, "vector-peer");
  CHECK(sockets[1] != INVALID_SOCKET_VALUE);
  socket_close(listener);
}

static void crypto_pair(crypto_context_t *a, crypto_context_t *b) {
  CHECK(crypto_init(a) == CRYPTO_OK);
  CHECK(crypto_init(b) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(a, b->public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(b, a->public_key) == CRYPTO_OK);
}

typedef struct {
  acip_transport_t *transport;
  const uint8_t *expected;
  size_t len;
  size_t iterations;
} receive_context_t;

static void *receive_packets(void *opaque) {
  receive_context_t *ctx = opaque;
  for (size_t i = 0; i < ctx->iterations; ++i) {
    void *data = NULL, *allocation = NULL;
    size_t len = 0;
    CHECK(acip_transport_recv(ctx->transport, &data, &len, &allocation) == ASCIICHAT_OK);
    CHECK(data == allocation);
    CHECK(len == sizeof(packet_header_t) + ctx->len);
    packet_header_t header;
    memcpy(&header, data, sizeof(header));
    CHECK(NET_TO_HOST_U16(header.type) == PACKET_TYPE_IMAGE_FRAME);
    CHECK(NET_TO_HOST_U32(header.length) == ctx->len);
    CHECK(memcmp((uint8_t *)data + sizeof(header), ctx->expected, ctx->len) == 0);
    buffer_pool_free(NULL, allocation, len);
  }
  return NULL;
}

static int compare_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

// Reproduce the previous TCP assembly path for a same-process comparison.
// Encryption still covers the same complete inner packet.
static asciichat_error_t assembled_send(socket_t sock, crypto_context_t *crypto, const void *data, size_t len) {
  if (!crypto) {
    socket_buffer_t slice = {data, len};
    return socket_sendv_all(sock, &slice, 1, BUDGET);
  }
  size_t capacity = len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  uint8_t *ciphertext = buffer_pool_alloc(NULL, capacity);
  CHECK(ciphertext);
  size_t encrypted_len = 0;
  CHECK(crypto_encrypt(crypto, data, len, ciphertext, capacity, &encrypted_len) == CRYPTO_OK);
  packet_header_t outer = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                           .type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED),
                           .length = HOST_TO_NET_U32((uint32_t)encrypted_len),
                           .crc32 = HOST_TO_NET_U32(asciichat_crc32(ciphertext, encrypted_len)),
                           .client_id = 0};
  size_t wire_size = sizeof(outer) + encrypted_len;
  uint8_t *wire = buffer_pool_alloc(NULL, wire_size);
  CHECK(wire);
  memcpy(wire, &outer, sizeof(outer));
  memcpy(wire + sizeof(outer), ciphertext, encrypted_len);
  socket_buffer_t slice = {wire, wire_size};
  asciichat_error_t result = socket_sendv_all(sock, &slice, 1, BUDGET);
  buffer_pool_free(NULL, wire, wire_size);
  buffer_pool_free(NULL, ciphertext, capacity);
  return result;
}

static void roundtrip(size_t len, bool encrypted, size_t iterations, bool baseline) {
  socket_t sockets[2];
  pair(sockets);
  int small = 4096;
  CHECK(socket_setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
  crypto_context_t a, b;
  crypto_pair(&a, &b);
  acip_transport_t *sender = acip_tcp_transport_create("vector-send", sockets[0], encrypted ? &a : NULL);
  acip_transport_t *receiver = acip_tcp_transport_create("vector-recv", sockets[1], encrypted ? &b : NULL);
  CHECK(sender && receiver);
  CHECK(acip_tcp_transport_set_zerocopy(sender, true) == ASCIICHAT_OK);
  uint8_t *payload = buffer_pool_alloc(NULL, len ? len : 1);
  CHECK(payload);
  for (size_t i = 0; i < len; ++i)
    payload[i] = (uint8_t)(i % 251);
  receive_context_t ctx = {receiver, payload, len, iterations};
  asciichat_thread_t thread;
  CHECK(asciichat_thread_create(&thread, "vector-receiver", receive_packets, &ctx) == 0);
  uint64_t *durations = buffer_pool_alloc(NULL, iterations * sizeof(uint64_t));
  CHECK(durations);
  socket_io_stats_reset();
  uint64_t start = time_get_ns();
  for (size_t i = 0; i < iterations; ++i) {
    uint64_t before = time_get_ns();
    if (baseline) {
      size_t size = sizeof(packet_header_t) + len;
      uint8_t *assembled = buffer_pool_alloc(NULL, size);
      CHECK(assembled);
      packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                                .type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME),
                                .length = HOST_TO_NET_U32((uint32_t)len),
                                .crc32 = HOST_TO_NET_U32(asciichat_crc32(payload, len)),
                                .client_id = 0};
      memcpy(assembled, &header, sizeof(header));
      memcpy(assembled + sizeof(header), payload, len);
      CHECK(assembled_send(sockets[0], encrypted ? &a : NULL, assembled, size) == ASCIICHAT_OK);
      buffer_pool_free(NULL, assembled, size);
    } else {
      size_t split = len / 3;
      socket_buffer_t slices[] = {{payload, split}, {NULL, 0}, {payload + split, len - split}};
      CHECK(packet_send_via_transportv(sender, PACKET_TYPE_IMAGE_FRAME, slices, 3, 0) == ASCIICHAT_OK);
    }
    durations[i] = time_get_ns() - before;
  }
  CHECK(asciichat_thread_join(&thread, NULL) == 0);
  uint64_t elapsed = time_get_ns() - start;
  qsort(durations, iterations, sizeof(uint64_t), compare_u64);
  log_warn("network-bench mode=%s encrypted=%d bytes=%zu iterations=%zu MiB/s=%.2f p50_us=%.1f p95_us=%.1f p99_us=%.1f",
           baseline ? "assembled" : "vectored", encrypted, len, iterations,
           (double)len * (double)iterations / ((double)elapsed / 1e9) / 1048576.0, durations[iterations / 2] / 1000.0,
           durations[iterations * 95 / 100] / 1000.0, durations[iterations * 99 / 100] / 1000.0);
  socket_io_stats_t stats = socket_io_stats_get();
  log_warn("network-io calls=%llu bytes=%llu zerocopy_calls=%llu completions=%llu copied=%llu",
           (unsigned long long)stats.send_calls, (unsigned long long)stats.sent_bytes,
           (unsigned long long)stats.zerocopy_calls, (unsigned long long)stats.zerocopy_completions,
           (unsigned long long)stats.zerocopy_copied);
  buffer_pool_free(NULL, durations, iterations * sizeof(uint64_t));
  buffer_pool_free(NULL, payload, len);
  acip_transport_destroy(sender);
  acip_transport_destroy(receiver);
  crypto_destroy(&a);
  crypto_destroy(&b);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
}

static void crc_vectors(void) {
  const char *known = "123456789";
  CHECK(asciichat_crc32(known, 9) == 0xe3069283U);
  for (size_t split = 0; split <= 9; ++split) {
    uint32_t crc = asciichat_crc32_update(0, known, split);
    crc = asciichat_crc32_update(crc, known + split, 9 - split);
    CHECK(crc == 0xe3069283U);
    CHECK(asciichat_crc32_sw_update(asciichat_crc32_sw(known, split), known + split, 9 - split) == crc);
  }
}

static void deadline_and_validation(void) {
  socket_t sockets[2];
  pair(sockets);
  size_t sent = 123;
  socket_buffer_t invalid = {NULL, 1};
  CHECK(socket_sendv(sockets[0], &invalid, 1, &sent) == ERROR_INVALID_PARAM);
  CHECK(sent == 0);
  CLEAR_ERRNO_ALL();
  invalid = (socket_buffer_t){"x", SIZE_MAX};
  CHECK(socket_sendv(sockets[0], &invalid, 1, &sent) == ERROR_INVALID_PARAM);
  CHECK(socket_sendv(sockets[0], &invalid, SOCKET_IOV_MAX + 1, &sent) == ERROR_INVALID_PARAM);
  int small = 1024;
  CHECK(socket_setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
  CHECK(socket_setsockopt(sockets[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small)) == 0);
  size_t len = 8 * 1024 * 1024;
  uint8_t *data = buffer_pool_alloc(NULL, len);
  CHECK(data);
  memset(data, 0x5a, len);
  socket_buffer_t slices[] = {{data, 17}, {data + 17, len - 17}};
  uint64_t before = time_get_ns();
  asciichat_error_t blocked = ASCIICHAT_OK;
  for (size_t i = 0; i < 64 && blocked == ASCIICHAT_OK; ++i)
    blocked = socket_sendv_all(sockets[0], slices, 2, 50 * NS_PER_MS_INT);
  CHECK(blocked != ASCIICHAT_OK);
  CHECK(time_get_ns() - before < 2 * NS_PER_SEC_INT);
  CHECK(socket_sendv_all(sockets[0], slices, 2, NS_PER_MS_INT) != ASCIICHAT_OK);
  buffer_pool_free(NULL, data, len);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
}

static void *send_concurrent(void *opaque) {
  receive_context_t *ctx = opaque;
  for (size_t i = 0; i < ctx->iterations; ++i)
    CHECK(packet_send_via_transport(ctx->transport, PACKET_TYPE_IMAGE_FRAME, ctx->expected, ctx->len, 0) ==
          ASCIICHAT_OK);
  return NULL;
}

static void concurrent_senders(void) {
  socket_t sockets[2];
  pair(sockets);
  crypto_context_t a, b;
  crypto_pair(&a, &b);
  acip_transport_t *sender = acip_tcp_transport_create("concurrent-send", sockets[0], &a);
  acip_transport_t *receiver = acip_tcp_transport_create("concurrent-recv", sockets[1], &b);
  uint8_t payload[2048];
  memset(payload, 0x37, sizeof(payload));
  receive_context_t send_ctx = {sender, payload, sizeof(payload), 24};
  receive_context_t recv_ctx = {receiver, payload, sizeof(payload), 96};
  asciichat_thread_t reader, writers[4];
  CHECK(asciichat_thread_create(&reader, "concurrent-reader", receive_packets, &recv_ctx) == 0);
  for (size_t i = 0; i < 4; ++i)
    CHECK(asciichat_thread_create(&writers[i], "concurrent-writer", send_concurrent, &send_ctx) == 0);
  for (size_t i = 0; i < 4; ++i)
    CHECK(asciichat_thread_join(&writers[i], NULL) == 0);
  CHECK(asciichat_thread_join(&reader, NULL) == 0);
  acip_transport_destroy(sender);
  acip_transport_destroy(receiver);
  crypto_destroy(&a);
  crypto_destroy(&b);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
}

static void full_duplex(void) {
  socket_t sockets[2];
  pair(sockets);
  crypto_context_t crypto[2];
  crypto_pair(&crypto[0], &crypto[1]);
  acip_transport_t *transports[2] = {acip_tcp_transport_create("duplex-a", sockets[0], &crypto[0]),
                                     acip_tcp_transport_create("duplex-b", sockets[1], &crypto[1])};
  size_t len = 128 * 1024;
  uint8_t *payload = buffer_pool_alloc(NULL, len);
  CHECK(payload && transports[0] && transports[1]);
  CHECK(acip_tcp_transport_set_zerocopy(transports[0], true) == ASCIICHAT_OK);
  CHECK(acip_tcp_transport_set_zerocopy(transports[1], true) == ASCIICHAT_OK);
  memset(payload, 0x35, len);
  receive_context_t contexts[] = {{transports[0], payload, len, 8}, {transports[1], payload, len, 8}};
  asciichat_thread_t readers[2], writers[2];
  for (size_t i = 0; i < 2; ++i) {
    CHECK(asciichat_thread_create(&readers[i], "duplex-receiver", receive_packets, &contexts[i]) == 0);
    CHECK(asciichat_thread_create(&writers[i], "duplex-sender", send_concurrent, &contexts[i]) == 0);
  }
  for (size_t i = 0; i < 2; ++i) {
    CHECK(asciichat_thread_join(&writers[i], NULL) == 0);
    CHECK(asciichat_thread_join(&readers[i], NULL) == 0);
    acip_transport_destroy(transports[i]);
    crypto_destroy(&crypto[i]);
    socket_close(sockets[i]);
  }
  buffer_pool_free(NULL, payload, len);
}

static size_t capture_calls;
static asciichat_error_t capture_message(acip_transport_t *transport, const void *data, size_t len) {
  (void)transport;
  CHECK(len == sizeof(packet_header_t) + 9);
  CHECK(memcmp((const char *)data + sizeof(packet_header_t), "123456789", 9) == 0);
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  CHECK(NET_TO_HOST_U32(header.crc32) == 0xe3069283U);
  ++capture_calls;
  return ASCIICHAT_OK;
}

static void message_fallback(void) {
  acip_transport_methods_t methods = {.send = capture_message};
  acip_transport_t transport = {.methods = &methods};
  socket_buffer_t slices[] = {{"123", 3}, {NULL, 0}, {"456789", 6}};
  CHECK(packet_send_via_transportv(&transport, PACKET_TYPE_IMAGE_FRAME, slices, 3, 0) == ASCIICHAT_OK);
  CHECK(capture_calls == 1);
  slices[1] = (socket_buffer_t){NULL, 1};
  CHECK(packet_send_via_transportv(&transport, PACKET_TYPE_IMAGE_FRAME, slices, 3, 0) != ASCIICHAT_OK);
  CHECK(capture_calls == 1);
}

static void zerocopy_cancellation(void) {
  socket_t sockets[2];
  pair(sockets);
  int small = 1024;
  CHECK(socket_setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
  CHECK(socket_setsockopt(sockets[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small)) == 0);
  socket_send_buffer_t owned = {0};
  CHECK(socket_send_buffer_alloc(8 * 1024 * 1024, &owned) == ASCIICHAT_OK);
  memset(owned.data, 0xa5, owned.capacity);
  bool supported, copied;
  uint64_t before = time_get_ns();
  asciichat_error_t result =
      socket_send_zerocopy(sockets[0], &owned, owned.capacity, 50 * NS_PER_MS_INT, &supported, &copied);
  CHECK(time_get_ns() - before < 2 * NS_PER_SEC_INT);
  if (supported)
    CHECK(result == ERROR_NETWORK);
  else
    CHECK(result == ASCIICHAT_OK);
  // Safe even after timeout with kernel references still outstanding.
  socket_send_buffer_free(&owned);
  CHECK(!owned.data);
  socket_send_buffer_free(&owned);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
}

static void malformed_and_disconnect(void) {
  for (int kind = 0; kind < 4; ++kind) {
    socket_t sockets[2];
    pair(sockets);
    acip_transport_t *receiver = acip_tcp_transport_create("invalid-receiver", sockets[1], NULL);
    receiver->receive_timeout_ns = 20 * NS_PER_MS_INT;
    packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                              .type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME),
                              .length = HOST_TO_NET_U32(1),
                              .crc32 = 0,
                              .client_id = 0};
    if (kind == 0)
      header.length = HOST_TO_NET_U32((uint32_t)MAX_PACKET_SIZE + 1);
    if (kind == 1)
      header.magic = 0;
    socket_buffer_t slices[] = {{&header, kind == 3 ? 1 : sizeof(header)}, {"x", kind == 2 ? 1 : 0}};
    CHECK(socket_sendv_all(sockets[0], slices, 2, BUDGET) == ASCIICHAT_OK);
    void *data = NULL, *allocation = NULL;
    size_t len = 0;
    CHECK(acip_transport_recv(receiver, &data, &len, &allocation) != ASCIICHAT_OK);
    CHECK(!acip_transport_is_connected(receiver));
    acip_transport_destroy(receiver);
    socket_close(sockets[0]);
    socket_close(sockets[1]);
  }
}

static unsigned media_messages;
static asciichat_error_t check_media(acip_transport_t *transport, const void *data, size_t len) {
  (void)transport;
  packet_header_t packet;
  memcpy(&packet, data, sizeof(packet));
  const uint8_t *body = (const uint8_t *)data + sizeof(packet);
  size_t body_len = len - sizeof(packet);
  CHECK(NET_TO_HOST_U32(packet.length) == body_len);
  CHECK(NET_TO_HOST_U32(packet.crc32) == asciichat_crc32(body, body_len));
  switch (NET_TO_HOST_U16(packet.type)) {
  case PACKET_TYPE_AUDIO_BATCH: {
    float samples[] = {0.0f, 1.0f, -1.0f, 0.5f};
    audio_batch_packet_t header;
    memcpy(&header, body, sizeof(header));
    CHECK(header.batch_count == 1 && header.total_samples == 4);
    CHECK(body_len == sizeof(header) + sizeof(samples));
    CHECK(memcmp(body + sizeof(header), samples, sizeof(samples)) == 0);
    break;
  }
  case PACKET_TYPE_AUDIO_OPUS_BATCH: {
    CHECK(body_len == 26);
    uint32_t count;
    uint16_t lengths[2];
    memcpy(&count, body + 8, sizeof(count));
    memcpy(lengths, body + 16, sizeof(lengths));
    CHECK(NET_TO_HOST_U32(count) == 2);
    CHECK(NET_TO_HOST_U16(lengths[0]) == 2 && NET_TO_HOST_U16(lengths[1]) == 4);
    CHECK(memcmp(body + 20, "abcdef", 6) == 0);
    break;
  }
  case PACKET_TYPE_IMAGE_FRAME: {
    image_frame_packet_t header;
    memcpy(&header, body, sizeof(header));
    CHECK(NET_TO_HOST_U32(header.width) == 2 && NET_TO_HOST_U32(header.height) == 2);
    CHECK(body_len == sizeof(header) + 12);
    CHECK(memcmp(body + sizeof(header), "abcdefghijkl", 12) == 0);
    break;
  }
  case PACKET_TYPE_ASCII_FRAME: {
    ascii_frame_packet_t header;
    memcpy(&header, body, sizeof(header));
    CHECK(NET_TO_HOST_U32(header.original_size) == 4096);
    char decoded[4096];
    if (NET_TO_HOST_U32(header.flags) & FRAME_FLAG_IS_COMPRESSED) {
      CHECK(decompress_data(body + sizeof(header), body_len - sizeof(header), decoded, sizeof(decoded)) ==
            ASCIICHAT_OK);
    } else {
      CHECK(body_len == sizeof(header) + sizeof(decoded));
      memcpy(decoded, body + sizeof(header), sizeof(decoded));
    }
    for (size_t i = 0; i < sizeof(decoded); ++i)
      CHECK(decoded[i] == 'A');
    CHECK(NET_TO_HOST_U32(header.checksum) == asciichat_crc32(decoded, sizeof(decoded)));
    break;
  }
  default:
    CHECK(false);
  }
  ++media_messages;
  return ASCIICHAT_OK;
}

static void media_wire_compatibility(void) {
  acip_transport_methods_t methods = {.send = check_media};
  acip_transport_t transport = {.methods = &methods};
  float samples[] = {0.0f, 1.0f, -1.0f, 0.5f};
  uint16_t sizes[] = {2, 4};
  char text[4096];
  memset(text, 'A', sizeof(text));
  CHECK(acip_send_audio_batch(&transport, samples, 4, 1) == ASCIICHAT_OK);
  CHECK(acip_send_audio_opus_batch(&transport, "abcdef", 6, sizes, 2, 48000, 20) == ASCIICHAT_OK);
  CHECK(acip_send_image_frame(&transport, "abcdefghijkl", 2, 2, 0) == ASCIICHAT_OK);
  CHECK(acip_send_ascii_frame(&transport, text, sizeof(text), 64, 64, "test") == ASCIICHAT_OK);
  CHECK(media_messages == 4);
}

static void peer_exchange(socket_t sock, bool server) {
  crypto_context_t crypto;
  CHECK(crypto_init(&crypto) == CRYPTO_OK);
  uint8_t peer_key[32];
  socket_buffer_t key = {crypto.public_key, sizeof(peer_key)};
  CHECK(socket_sendv_all(sock, &key, 1, BUDGET) == ASCIICHAT_OK);
  CHECK(recv_with_timeout(sock, peer_key, sizeof(peer_key), BUDGET) == sizeof(peer_key));
  CHECK(crypto_set_peer_public_key(&crypto, peer_key) == CRYPTO_OK);
  acip_transport_t *transport = acip_tcp_transport_create("cross-os-peer", sock, &crypto);
  CHECK(transport);
  CHECK(acip_tcp_transport_set_zerocopy(transport, true) == ASCIICHAT_OK);
  uint8_t *payload = buffer_pool_alloc(NULL, 256 * 1024);
  CHECK(payload);
  for (size_t i = 0; i < 256 * 1024; ++i)
    payload[i] = (uint8_t)(i % 251);
  size_t lengths[] = {0, 256, 65536, 256 * 1024};
  socket_io_stats_reset();
  for (size_t i = 0; i < 4; ++i) {
    receive_context_t ctx = {transport, payload, lengths[i], 1};
    if (server)
      receive_packets(&ctx);
    CHECK(packet_send_via_transport(transport, PACKET_TYPE_IMAGE_FRAME, payload, lengths[i], 0) == ASCIICHAT_OK);
    if (!server)
      receive_packets(&ctx);
  }
  socket_io_stats_t stats = socket_io_stats_get();
  log_warn("Cross-OS encrypted exchange passed; zerocopy=%llu completions=%llu copied=%llu",
           (unsigned long long)stats.zerocopy_calls, (unsigned long long)stats.zerocopy_completions,
           (unsigned long long)stats.zerocopy_copied);
  buffer_pool_free(NULL, payload, 256 * 1024);
  acip_transport_destroy(transport);
  crypto_destroy(&crypto);
  socket_close(sock);
}

static void peer_mode(int argc, char **argv) {
  CHECK(argc >= 3);
  bool server = strcmp(argv[1], "--serve") == 0;
  unsigned port = (unsigned)strtoul(argv[2], NULL, 10);
  CHECK(port > 1024 && port < 65536);
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = HOST_TO_NET_U16((uint16_t)port)};
  socket_t sock = socket_create("cross-os", AF_INET, SOCK_STREAM, 0);
  CHECK(sock != INVALID_SOCKET_VALUE);
  if (server) {
    addr.sin_addr.s_addr = HOST_TO_NET_U32(INADDR_ANY);
    CHECK(socket_set_reuseaddr(sock, true) == 0);
    CHECK(socket_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    CHECK(socket_listen(sock, 1) == 0);
    log_warn("Cross-OS test listener ready on port %u (60 second deadline)", port);
    struct pollfd ready = {.fd = sock, .events = POLLIN};
    CHECK(socket_poll(&ready, 1, 60 * NS_PER_SEC_INT) > 0);
    socket_t peer = socket_accept(sock, NULL, NULL, "cross-os-accepted");
    CHECK(peer != INVALID_SOCKET_VALUE);
    socket_close(sock);
    peer_exchange(peer, true);
  } else {
    CHECK(argc == 4);
    unsigned a, b, c, d;
    CHECK(sscanf(argv[3], "%u.%u.%u.%u", &a, &b, &c, &d) == 4);
    CHECK(a <= 255 && b <= 255 && c <= 255 && d <= 255);
    addr.sin_addr.s_addr = HOST_TO_NET_U32((a << 24) | (b << 16) | (c << 8) | d);
    CHECK(connect_with_timeout(sock, (struct sockaddr *)&addr, sizeof(addr), 10));
    peer_exchange(sock, false);
  }
}

int main(int argc, char **argv) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  if (argc > 1 && (strcmp(argv[1], "--serve") == 0 || strcmp(argv[1], "--peer") == 0)) {
    peer_mode(argc, argv);
    socket_cleanup();
    log_destroy();
    return 0;
  }
  bool benchmark = argc > 1 && strcmp(argv[1], "--benchmark") == 0;
  bool assembled_only = argc > 2 && strcmp(argv[2], "assembled") == 0;
  bool vectored_only = argc > 2 && strcmp(argv[2], "vectored") == 0;
  if (!benchmark) {
    crc_vectors();
    deadline_and_validation();
    message_fallback();
    media_wire_compatibility();
    malformed_and_disconnect();
    concurrent_senders();
    full_duplex();
    zerocopy_cancellation();
    CLEAR_ERRNO_ALL();
  }
  size_t sizes[] = {0, 256, 4096, 65536, CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(packet_header_t)};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    for (int encrypted = 0; encrypted < 2; ++encrypted) {
      if (!assembled_only)
        roundtrip(sizes[i], encrypted != 0, benchmark ? 100 : 3, false);
      if (benchmark && !vectored_only)
        roundtrip(sizes[i], encrypted != 0, 100, true);
    }
  }
  if (!benchmark)
    roundtrip(MAX_PACKET_SIZE, false, 2, false);
  socket_cleanup();
  log_warn("Vectored networking checks passed");
  log_destroy();
  return 0;
}
