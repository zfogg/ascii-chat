#include "../worker_assert.h"
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <libplatform/thread.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <libplatform/atomic.h>
#include <string.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/network/websocket/internal.h>
#include <libwebsockets.h>

static void allocation_contracts(void) {
  size_t baseline = buffer_process_reserved();
  buffer_domain_t *domain = NULL;
  buffer_domain_config_t config = {1024 * 1024, 512 * 1024, 0, false};
  ASCIICHAT_REQUIRE_EQ(buffer_domain_create(&config, &domain), ASCIICHAT_OK);
  const size_t sizes[] = {0, 1, 63, 64, 65, 4095, 4096, 4097, 65535};
  for (size_t alignment = sizeof(void *); alignment <= 4096; alignment *= 2) {
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
      void *data = NULL;
      ASCIICHAT_REQUIRE_EQ(buffer_acquire(domain, sizes[i], alignment, &data), ASCIICHAT_OK);
      ASCIICHAT_REQUIRE(data && (uintptr_t)data % alignment == 0);
      ASCIICHAT_REQUIRE(buffer_capacity(data) >= sizes[i]);
      ASCIICHAT_REQUIRE_EQ(buffer_size(data), sizes[i]);
      memset(data, 0xab, sizes[i]);
      buffer_release(data);
      ASCIICHAT_REQUIRE_EQ(data, NULL);
    }
  }
  void *bad = NULL;
  ASCIICHAT_REQUIRE(buffer_acquire(domain, SIZE_MAX, 64, &bad) != ASCIICHAT_OK && !bad);
  ASCIICHAT_REQUIRE(buffer_acquire(domain, 100, 3, &bad) != ASCIICHAT_OK && !bad);
  ASCIICHAT_REQUIRE(buffer_acquire(domain, 2 * 1024 * 1024, 64, &bad) != ASCIICHAT_OK && !bad);
  buffer_domain_trim(domain);
  buffer_stats_t stats;
  buffer_domain_stats(domain, &stats);
  ASCIICHAT_REQUIRE(stats.reserved_bytes == 0 && stats.outstanding == 0);
  void *large = buffer_alloc(domain, 32768);
  void *small = buffer_alloc(domain, 64);
  ASCIICHAT_REQUIRE(large && small);
  void *address = large;
  buffer_release(large);
  buffer_release(small);
  large = buffer_alloc(domain, 32768);
  ASCIICHAT_REQUIRE_EQ(large, address); /* A smaller returned buffer cannot hide this bin. */
  memset(large, 0xab, 32768);
  ASCIICHAT_REQUIRE(buffer_is_unique(large));
  const void *shared = buffer_share(large);
  ASCIICHAT_REQUIRE_EQ(shared, large);
  ASCIICHAT_REQUIRE(!buffer_is_unique(large));
  ASCIICHAT_REQUIRE_NE(buffer_set_size(large, 4), ASCIICHAT_OK);
  buffer_release(large);
  ASCIICHAT_REQUIRE(buffer_is_unique(shared));
  ASCIICHAT_REQUIRE_EQ(((const unsigned char *)shared)[0], 0xab);
  buffer_domain_close(&domain);
  ASCIICHAT_REQUIRE_EQ(domain, NULL);
  ASCIICHAT_REQUIRE(buffer_capacity(shared) >= 32768);
  buffer_release(shared); /* The final reference destroys the closed domain. */
  ASCIICHAT_REQUIRE_EQ(buffer_process_reserved(), baseline);
}

static void workspace_contracts(void) {
  buffer_domain_t *domain = NULL;
  buffer_domain_config_t config = {4096, 2048, 0, false};
  ASCIICHAT_REQUIRE_EQ(buffer_domain_create(&config, &domain), ASCIICHAT_OK);
  buffer_workspace_t workspace = {0};
  ASCIICHAT_REQUIRE_EQ(buffer_workspace_reserve(&workspace, domain, 100), ASCIICHAT_OK);
  memcpy(workspace.data, "retained", 9);
  workspace.length = 9;
  void *original = workspace.data;
  ASCIICHAT_REQUIRE_EQ(buffer_workspace_reserve(&workspace, domain, 90), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(workspace.data, original);
  ASCIICHAT_REQUIRE_EQ(buffer_workspace_reserve(&workspace, domain, 500), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(workspace.data, "retained"), 0);
  original = workspace.data;
  ASCIICHAT_REQUIRE_NE(buffer_workspace_reserve(&workspace, domain, 8192), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(workspace.data == original && workspace.length == 9);
  buffer_domain_close(&domain);
  buffer_workspace_destroy(&workspace);
  ASCIICHAT_REQUIRE(!workspace.data && !workspace.length);
}

static void queue_contracts(void) {
  packet_queue_config_t config = {2, 128, PACKET_QUEUE_REJECT};
  packet_queue_t *queue = packet_queue_create_config(&config);
  ASCIICHAT_REQUIRE(queue);
  void *data = buffer_alloc(NULL, 80);
  ASCIICHAT_REQUIRE(data);
  memset(data, 42, 80);
  void *original = data;
  ASCIICHAT_REQUIRE_EQ(packet_queue_push_take(queue, PACKET_TYPE_PING, &data, 80, 1), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(!data);
  data = buffer_alloc(NULL, 80);
  ASCIICHAT_REQUIRE(data);
  ASCIICHAT_REQUIRE_NE(packet_queue_push_take(queue, PACKET_TYPE_PING, &data, 80, 2), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(data); /* Byte limit rejects without consuming ownership. */
  buffer_release(data);
  queued_packet_t *packet = packet_queue_try_dequeue(queue);
  ASCIICHAT_REQUIRE(packet && packet->data == original && packet->data_len == 80);
  ASCIICHAT_REQUIRE_EQ(packet_queue_push_copy(queue, PACKET_TYPE_PING, "copy", 5, 3), ASCIICHAT_OK);
  packet_queue_stop(queue);
  ASCIICHAT_REQUIRE(!packet_queue_try_dequeue(queue));
  data = buffer_alloc(NULL, 10);
  ASCIICHAT_REQUIRE(data);
  ASCIICHAT_REQUIRE_NE(packet_queue_push_take(queue, PACKET_TYPE_PING, &data, 10, 4), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(data);
  buffer_release(data);
  packet_queue_destroy(queue); /* Clears a stopped queue, preserves dequeued owners. */
  ASCIICHAT_REQUIRE_EQ(((const unsigned char *)packet->data)[0], 42);
  packet_queue_free_packet(packet);

  config.overflow = PACKET_QUEUE_DROP_OLDEST;
  queue = packet_queue_create_config(&config);
  for (unsigned i = 0; i < 10; i++)
    ASCIICHAT_REQUIRE_EQ(packet_queue_push_copy(queue, PACKET_TYPE_IMAGE_FRAME, &i, sizeof(i), i), ASCIICHAT_OK);
  uint64_t enqueued = 0, dropped = 0;
  packet_queue_get_stats(queue, &enqueued, NULL, &dropped);
  ASCIICHAT_REQUIRE(enqueued == 10 && dropped == 8 && packet_queue_size(queue) == 2);
  packet = packet_queue_dequeue(queue);
  ASCIICHAT_REQUIRE(packet && NET_TO_HOST_U32(packet->header.client_id) == 8);
  packet_queue_free_packet(packet);
  packet_queue_destroy(queue);

  queue = packet_queue_create(2);
  data = buffer_alloc(NULL, 4);
  ASCIICHAT_REQUIRE(data);
  memcpy(data, "abc", 4);
  ASCIICHAT_REQUIRE_EQ(packet_queue_push_shared(queue, PACKET_TYPE_PING, data, 4, 0), ASCIICHAT_OK);
  buffer_release(data);
  packet = packet_queue_dequeue(queue);
  ASCIICHAT_REQUIRE(packet && strcmp(packet->data, "abc") == 0);
  packet_queue_free_packet(packet);
  ASCIICHAT_REQUIRE_EQ(packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 0, 0), ASCIICHAT_OK);
  packet = packet_queue_dequeue(queue);
  ASCIICHAT_REQUIRE(packet && !packet->data && packet->data_len == 0);
  packet_queue_free_packet(packet);
  packet_queue_destroy(queue);
}

typedef struct {
  buffer_domain_t *domain;
  atomic_t running;
} stress_t;

static void *allocate_worker(void *arg) {
  stress_t *stress = arg;
  for (unsigned i = 0; i < 5000; i++) {
    size_t size = 64u << (i % 8);
    unsigned char *data = buffer_alloc(stress->domain, size);
    ASCIICHAT_WORKER_ASSERT(data);
    memset(data, (int)(i & 255), size);
    const void *shared = buffer_share(data);
    buffer_release(data);
    ASCIICHAT_WORKER_ASSERT((((const unsigned char *)shared)[size - 1]) == ((i & 255)));
    buffer_release(shared);
  }
  atomic_fetch_sub_u64(&stress->running, 1);
  return NULL;
}

static void *trim_worker(void *arg) {
  stress_t *stress = arg;
  while (atomic_load_u64(&stress->running))
    buffer_domain_trim(stress->domain);
  return NULL;
}

static void concurrent_contracts(void) {
  size_t baseline = buffer_process_reserved();
  stress_t stress = {0};
  buffer_domain_config_t config = {4 * 1024 * 1024, 2 * 1024 * 1024, 0, false};
  ASCIICHAT_REQUIRE_EQ(buffer_domain_create(&config, &stress.domain), ASCIICHAT_OK);
  atomic_store_u64(&stress.running, 4);
  platform_thread_t workers[4], trimmer;
  ASCIICHAT_REQUIRE_EQ(platform_thread_create(&trimmer, "buffer-trim", trim_worker, &stress), 0);
  for (unsigned i = 0; i < 4; i++)
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&workers[i], "buffer-allocate", allocate_worker, &stress), 0);
  for (unsigned i = 0; i < 4; i++)
    ASCIICHAT_REQUIRE_EQ(thread_join(&workers[i], NULL), 0);
  ASCIICHAT_REQUIRE_EQ(thread_join(&trimmer, NULL), 0);
  buffer_stats_t stats;
  buffer_domain_stats(stress.domain, &stats);
  ASCIICHAT_REQUIRE(!stats.outstanding && !stats.live_bytes && !stats.requested_bytes);
  buffer_domain_close(&stress.domain);
  ASCIICHAT_REQUIRE_EQ(buffer_process_reserved(), baseline);
}

typedef struct {
  packet_queue_t *queue;
  atomic_t producers;
  atomic_t consumed;
  atomic_t seen[2000];
} queue_stress_t;

typedef struct {
  queue_stress_t *stress;
  unsigned start;
} producer_t;

static void *queue_producer(void *arg) {
  producer_t *producer = arg;
  for (unsigned i = producer->start; i < producer->start + 1000; i++) {
    void *data = buffer_alloc(NULL, 2 * sizeof(unsigned));
    ASCIICHAT_WORKER_ASSERT(data);
    ((unsigned *)data)[0] = i;
    ((unsigned *)data)[1] = ~i;
    asciichat_error_t result;
    while ((result = packet_queue_push_take(producer->stress->queue, PACKET_TYPE_PING, &data, 2 * sizeof(unsigned),
                                            i)) != ASCIICHAT_OK) {
      ASCIICHAT_WORKER_ASSERT(result == ERROR_BUFFER && data);
      platform_sleep_ns(100000);
    }
    ASCIICHAT_WORKER_ASSERT(!data);
  }
  atomic_fetch_sub_u64(&producer->stress->producers, 1);
  return NULL;
}

static void *queue_consumer(void *arg) {
  queue_stress_t *stress = arg;
  for (;;) {
    queued_packet_t *packet = packet_queue_try_dequeue(stress->queue);
    if (!packet) {
      if (!atomic_load_u64(&stress->producers) && packet_queue_is_empty(stress->queue))
        break;
      platform_sleep_ns(100000);
      continue;
    }
    const unsigned *data = packet->data;
    ASCIICHAT_WORKER_ASSERT(packet->data_len == 2 * sizeof(unsigned) && data[0] < 2000 && data[1] == ~data[0]);
    ASCIICHAT_WORKER_ASSERT((atomic_fetch_add_u64(&stress->seen[data[0]], 1)) == (0));
    atomic_fetch_add_u64(&stress->consumed, 1);
    packet_queue_free_packet(packet);
  }
  return NULL;
}

static void concurrent_queue_contracts(void) {
  queue_stress_t *stress = SAFE_CALLOC(1, sizeof(*stress), queue_stress_t *);
  stress->queue = packet_queue_create_config(&(packet_queue_config_t){32, 512, PACKET_QUEUE_REJECT});
  ASCIICHAT_REQUIRE(stress->queue);
  atomic_store_u64(&stress->producers, 2);
  producer_t producers[2] = {{stress, 0}, {stress, 1000}};
  platform_thread_t threads[4];
  for (unsigned i = 0; i < 2; i++) {
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&threads[i], "queue-producer", queue_producer, &producers[i]), 0);
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&threads[i + 2], "queue-consumer", queue_consumer, stress), 0);
  }
  for (unsigned i = 0; i < 4; i++)
    ASCIICHAT_REQUIRE_EQ(thread_join(&threads[i], NULL), 0);
  ASCIICHAT_REQUIRE_EQ(atomic_load_u64(&stress->consumed), 2000);
  packet_queue_destroy(stress->queue);
  SAFE_FREE(stress);
}

static void process_budget_contracts(void) {
  size_t baseline = buffer_process_reserved();
  buffer_budget_t lease = {0};
  ASCIICHAT_REQUIRE_EQ(buffer_budget_reserve(&lease, 4096), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(buffer_process_reserved(), baseline + 4096);
  ASCIICHAT_REQUIRE(buffer_budget_reserve(&lease, 1) != ASCIICHAT_OK && lease.bytes == 4096);
  buffer_budget_release(&lease);
  buffer_budget_release(&lease);
  ASCIICHAT_REQUIRE(!lease.bytes && buffer_process_reserved() == baseline);
  ASCIICHAT_REQUIRE(buffer_budget_reserve(&lease, BUFFER_PROCESS_LIMIT) != ASCIICHAT_OK && !lease.bytes);
  buffer_domain_t *media = NULL, *control = NULL;
  buffer_domain_config_t config = {BUFFER_PROCESS_LIMIT, 0, 0, false};
  ASCIICHAT_REQUIRE_EQ(buffer_domain_create(&config, &media), ASCIICHAT_OK);
  config.control = true;
  ASCIICHAT_REQUIRE_EQ(buffer_domain_create(&config, &control), ASCIICHAT_OK);
  void *blocks[512] = {0};
  size_t count = 0;
  while (count < 512 && (blocks[count] = buffer_alloc(media, 1024 * 1024)))
    count++;
  ASCIICHAT_REQUIRE(count > 0 && count < 512);
  ASCIICHAT_REQUIRE(buffer_process_reserved() <= BUFFER_PROCESS_LIMIT - BUFFER_CONTROL_RESERVE);
  void *message = buffer_alloc(control, BUFFER_CONTROL_RESERVE / 2);
  ASCIICHAT_REQUIRE(message && buffer_process_reserved() <= BUFFER_PROCESS_LIMIT);
  buffer_domain_close(&media);
  buffer_domain_close(&control);
  for (size_t i = 0; i < count; i++)
    buffer_release(blocks[i]);
  buffer_release(message);
  ASCIICHAT_REQUIRE_EQ(buffer_process_reserved(), baseline);
}

static void media_contracts(void) {
  image_t *image = image_new(641, 479);
  ASCIICHAT_REQUIRE(image && (uintptr_t)image->pixels % 64 == 0);
  memset(image->pixels, 42, (size_t)image->w * image->h * sizeof(rgb_pixel_t));
  image_destroy(image);
  char input[8192];
  memset(input, 'a', sizeof(input));
  void *compressed = NULL;
  size_t length = 0;
  ASCIICHAT_REQUIRE_EQ(compress_data(input, sizeof(input), &compressed, &length, 1), ASCIICHAT_OK);
  char output[8192];
  ASCIICHAT_REQUIRE_EQ(decompress_data(compressed, length, output, sizeof(output)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(memcmp(input, output, sizeof(input)), 0);
  buffer_release(compressed);
  compression_workspace_t workspace = {0};
  ASCIICHAT_REQUIRE_EQ(compress_workspace(&workspace, input, sizeof(input), 1), ASCIICHAT_OK);
  void *storage = workspace.output.data;
  void *codec = workspace.codec;
  for (int i = 0; i < 100; i++) {
    input[i] = (char)i;
    ASCIICHAT_REQUIRE_EQ(compress_workspace(&workspace, input, sizeof(input), 1), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(workspace.output.data == storage && workspace.codec == codec);
    ASCIICHAT_REQUIRE_EQ(decompress_data(workspace.output.data, workspace.output.length, output, sizeof(output)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(memcmp(input, output, sizeof(input)), 0);
  }
  compression_workspace_destroy(&workspace);
  ASCIICHAT_REQUIRE(!workspace.codec && !workspace.output.data);
}

static void client_admission_respects_process_budget(void) {
  video_frame_buffer_t *incoming[MAX_CLIENTS] = {0}, *outgoing[MAX_CLIENTS] = {0};
  audio_ring_buffer_t *audio[MAX_CLIENTS] = {0};
  void *send[MAX_CLIENTS] = {0};
  size_t admitted = 0;
  for (size_t i = 0; i < MAX_CLIENTS; ++i) {
    char name[32];
    safe_snprintf(name, sizeof(name), "budget-client-%zu", i);
    incoming[i] = video_frame_buffer_create(name);
    outgoing[i] = video_frame_buffer_create(name);
    audio[i] = audio_ring_buffer_create_for_capture();
    send[i] = buffer_alloc(NULL, MAX_FRAME_BUFFER_SIZE);
    if (!incoming[i] || !outgoing[i] || !audio[i] || !send[i])
      break;
    ++admitted;
  }
  ASCIICHAT_REQUIRE_GT(admitted, 0, "The budget must admit at least one complete media client");
  ASCIICHAT_REQUIRE_LE(buffer_process_reserved(), BUFFER_PROCESS_LIMIT - BUFFER_CONTROL_RESERVE);
  // Large client limits can exceed the finite media budget. Even then, control
  // traffic must retain its reserved capacity and partial startup must be releasable.
  void *control = buffer_alloc(buffer_control_domain(), BUFFER_CONTROL_RESERVE / 2);
  ASCIICHAT_REQUIRE_NOT_NULL(control);
  buffer_release(control);
  for (size_t i = 0; i < MAX_CLIENTS; ++i) {
    if (incoming[i])
      video_frame_buffer_destroy(incoming[i]);
    if (outgoing[i])
      video_frame_buffer_destroy(outgoing[i]);
    if (audio[i])
      audio_ring_buffer_destroy(audio[i]);
    buffer_release(send[i]);
  }
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  ASCIICHAT_REQUIRE_EQ(stats.outstanding, 0);
  buffer_domain_trim(buffer_default_domain());
  video_frame_buffer_t *recovered = video_frame_buffer_create("after-budget-release");
  ASCIICHAT_REQUIRE_NOT_NULL(recovered, "Releasing clients must restore admission capacity");
  video_frame_buffer_destroy(recovered);
}

static void websocket_control_reserve_contract(void) {
  struct lws_context_creation_info info = {0};
  info.port = CONTEXT_PORT_NO_LISTEN;
  struct lws_context *context = lws_create_context(&info);
  ASCIICHAT_REQUIRE(context);
  websocket_transport_data_t ws = {.context = context, .is_connected = true};
  ws.send_queue = ringbuffer_create(sizeof(websocket_msg_t), 4);
  ASCIICHAT_REQUIRE(ws.send_queue);
  ASCIICHAT_REQUIRE_EQ(mutex_init(&ws.send_mutex, "reserve-send"), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_init(&ws.state_mutex, "reserve-state"), 0);
  crypto_context_t crypto, peer;
  ASCIICHAT_REQUIRE(crypto_init(&crypto) == CRYPTO_OK && crypto_init(&peer) == CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&crypto, peer.public_key), CRYPTO_OK);
  crypto.handshake_complete = peer.handshake_complete = true;
  acip_transport_t transport = {.impl_data = &ws};
  ASCIICHAT_REQUIRE(buffer_control_domain());
  // An oversized miss trims idle media storage, so cached blocks cannot mask pressure.
  ASCIICHAT_REQUIRE_EQ(buffer_alloc(NULL, BUFFER_DOMAIN_LIMIT), NULL);
  buffer_budget_t pressure = {0};
  ASCIICHAT_REQUIRE_EQ(
      buffer_budget_reserve(&pressure, BUFFER_PROCESS_LIMIT - BUFFER_CONTROL_RESERVE - buffer_process_reserved()),
      ASCIICHAT_OK);
  // Consume all non-control process allowance while leaving the reserved bytes available.
  packet_type_t types[] = {PACKET_TYPE_CRYPTO_PARAMETERS, PACKET_TYPE_ERROR_MESSAGE};
  for (int client = 0; client < 2; ++client) {
    ws.owns_context = client != 0;
    for (int encrypted = 0; encrypted < 2; ++encrypted) {
      transport.crypto_ctx = encrypted ? &crypto : NULL;
      for (size_t i = 0; i < 2; ++i) {
        packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC), .type = HOST_TO_NET_U16(types[i])};
        asciichat_error_t result = websocket_transport_send(&transport, &header, sizeof(header));
        ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "client=%d encrypted=%d type=%d: %s", client, encrypted, types[i],
                     asciichat_error_string(result));
        websocket_msg_t message;
        ASCIICHAT_REQUIRE(ringbuffer_read(ws.send_queue, &message));
        packet_header_t wire;
        memcpy(&wire, message.data + LWS_PRE, sizeof(wire));
        ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(wire.type),
                     (encrypted && types[i] == PACKET_TYPE_ERROR_MESSAGE ? PACKET_TYPE_ENCRYPTED : types[i]));
        buffer_release(message.data);
      }
    }
  }
  transport.crypto_ctx = NULL;
  packet_header_t media = {.magic = HOST_TO_NET_U64(PACKET_MAGIC), .type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME)};
  ASCIICHAT_REQUIRE_EQ(websocket_transport_send(&transport, &media, sizeof(media)), ERROR_MEMORY);
  buffer_budget_release(&pressure);
  crypto_destroy(&crypto);
  crypto_destroy(&peer);
  ringbuffer_destroy(ws.send_queue);
  mutex_destroy(&ws.send_mutex);
  mutex_destroy(&ws.state_mutex);
  lws_context_destroy(context);
}

static void *share_worker(void *data) {
  for (unsigned i = 0; i < 10000; ++i) {
    const void *shared = buffer_share(data);
    ASCIICHAT_WORKER_ASSERT(shared && ((const unsigned char *)shared)[0] == 42);
    buffer_release(shared);
  }
  return NULL;
}

static void concurrent_references(void) {
  void *data = buffer_alloc(NULL, 128);
  ASCIICHAT_REQUIRE(data);
  memset(data, 42, 128);
  platform_thread_t threads[4];
  for (unsigned i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&threads[i], "buffer-share", share_worker, data), ASCIICHAT_OK);
  for (unsigned i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(thread_join(&threads[i], NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(buffer_is_unique(data));
  buffer_release(data);
}

typedef struct {
  _Atomic(unsigned) *ready;
  _Atomic(bool) *start;
  asciichat_error_t media_result;
  asciichat_error_t control_result;
} cold_start_worker_t;

static void *allocate_on_cold_start(void *arg) {
  cold_start_worker_t *worker = arg;
  atomic_fetch_add(worker->ready, 1);
  while (!atomic_load(worker->start))
    platform_sleep_us(100);
  void *media = NULL;
  void *control = NULL;
  worker->media_result = buffer_acquire(NULL, 4096, BUFFER_DEFAULT_ALIGNMENT, &media);
  buffer_domain_t *domain = buffer_control_domain();
  worker->control_result =
      domain ? buffer_acquire(domain, 64, BUFFER_DEFAULT_ALIGNMENT, &control) : ERROR_INVALID_STATE;
  buffer_release(media);
  buffer_release(control);
  return NULL;
}

static void concurrent_cold_start(void) {
  enum { WORKERS = 16, ROUNDS = 8 };
  for (unsigned round = 0; round < ROUNDS; ++round) {
    buffer_system_cleanup();
    _Atomic(unsigned) ready = 0;
    _Atomic(bool) start = false;
    cold_start_worker_t workers[WORKERS] = {0};
    platform_thread_t threads[WORKERS];
    for (unsigned i = 0; i < WORKERS; ++i) {
      workers[i].ready = &ready;
      workers[i].start = &start;
      workers[i].media_result = ERROR_INVALID_STATE;
      workers[i].control_result = ERROR_INVALID_STATE;
      ASCIICHAT_REQUIRE_EQ(platform_thread_create(&threads[i], "buffer-cold-start", allocate_on_cold_start, &workers[i]),
                   ASCIICHAT_OK);
    }
    uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
    while (atomic_load(&ready) != WORKERS && time_get_ns() < deadline)
      platform_sleep_us(100);
    bool all_ready = atomic_load(&ready) == WORKERS;
    atomic_store(&start, true);
    for (unsigned i = 0; i < WORKERS; ++i)
      ASCIICHAT_REQUIRE_EQ(thread_join(&threads[i], NULL), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(all_ready, "Cold-start workers failed to reach the start barrier");
    for (unsigned i = 0; i < WORKERS; ++i) {
      ASCIICHAT_REQUIRE_EQ(workers[i].media_result, ASCIICHAT_OK, "Media allocation failed in round %u worker %u", round, i);
      ASCIICHAT_REQUIRE_EQ(workers[i].control_result, ASCIICHAT_OK, "Control allocation failed in round %u worker %u", round,
                   i);
    }
    buffer_stats_t stats;
    buffer_domain_stats(buffer_default_domain(), &stats);
    ASCIICHAT_REQUIRE_EQ(stats.outstanding, 0);
    ASCIICHAT_REQUIRE_EQ(stats.failures, 0);
    buffer_domain_stats(buffer_control_domain(), &stats);
    ASCIICHAT_REQUIRE_EQ(stats.outstanding, 0);
    ASCIICHAT_REQUIRE_EQ(stats.failures, 0);
  }
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  buffer_system_init();
}

static void teardown(void) {
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  ASCIICHAT_REQUIRE_EQ(stats.outstanding, 0, "Test leaked buffer owners");
  buffer_system_cleanup();
  ASCIICHAT_REQUIRE_EQ(buffer_process_reserved(), 0);
  log_destroy();
}
TEST_CASE("buffer_ownership/allocation_contracts", "[buffer_ownership]") {
  allocation_contracts();
}

TEST_CASE("buffer_ownership/workspace_contracts", "[buffer_ownership]") {
  workspace_contracts();
}

TEST_CASE("buffer_ownership/queue_contracts", "[buffer_ownership]") {
  queue_contracts();
}

TEST_CASE("buffer_ownership/concurrent_contracts", "[buffer_ownership]") {
  concurrent_contracts();
}

TEST_CASE("buffer_ownership/concurrent_queue_contracts", "[buffer_ownership]") {
  concurrent_queue_contracts();
}

TEST_CASE("buffer_ownership/concurrent_references", "[buffer_ownership]") {
  concurrent_references();
}

TEST_CASE("buffer_ownership/process_budget_contracts", "[buffer_ownership]") {
  process_budget_contracts();
}

TEST_CASE("buffer_ownership/media_contracts", "[buffer_ownership]") {
  media_contracts();
}

TEST_CASE("buffer_ownership/client_admission_respects_process_budget", "[buffer_ownership]") {
  client_admission_respects_process_budget();
}

TEST_CASE("buffer_ownership/websocket_control_reserve_contract", "[buffer_ownership]") {
  websocket_control_reserve_contract();
}

TEST_CASE("buffer_ownership/concurrent_cold_start", "[buffer_ownership]") {
  concurrent_cold_start();
}
