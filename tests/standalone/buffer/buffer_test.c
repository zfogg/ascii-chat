#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/atomic.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "Buffer contract failed: %s, line %d", #expr, __LINE__);                           \
  } while (0)

static void allocation_contracts(void) {
  size_t baseline = buffer_process_reserved();
  buffer_domain_t *domain = NULL;
  buffer_domain_config_t config = {1024 * 1024, 512 * 1024, 0, false};
  CHECK(buffer_domain_create(&config, &domain) == ASCIICHAT_OK);
  const size_t sizes[] = {0, 1, 63, 64, 65, 4095, 4096, 4097, 65535};
  for (size_t alignment = sizeof(void *); alignment <= 4096; alignment *= 2) {
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
      void *data = NULL;
      CHECK(buffer_acquire(domain, sizes[i], alignment, &data) == ASCIICHAT_OK);
      CHECK(data && (uintptr_t)data % alignment == 0);
      CHECK(buffer_capacity(data) >= sizes[i]);
      CHECK(buffer_size(data) == sizes[i]);
      memset(data, 0xab, sizes[i]);
      buffer_release(data);
      CHECK(data == NULL);
    }
  }
  void *bad = NULL;
  CHECK(buffer_acquire(domain, SIZE_MAX, 64, &bad) != ASCIICHAT_OK && !bad);
  CHECK(buffer_acquire(domain, 100, 3, &bad) != ASCIICHAT_OK && !bad);
  CHECK(buffer_acquire(domain, 2 * 1024 * 1024, 64, &bad) != ASCIICHAT_OK && !bad);
  buffer_domain_trim(domain);
  buffer_stats_t stats;
  buffer_domain_stats(domain, &stats);
  CHECK(stats.reserved_bytes == 0 && stats.outstanding == 0);
  void *large = buffer_alloc(domain, 32768);
  void *small = buffer_alloc(domain, 64);
  CHECK(large && small);
  void *address = large;
  buffer_release(large);
  buffer_release(small);
  large = buffer_alloc(domain, 32768);
  CHECK(large == address); /* A smaller returned buffer cannot hide this bin. */
  memset(large, 0xab, 32768);
  const void *shared = buffer_share(large);
  CHECK(shared == large);
  CHECK(buffer_set_size(large, 4) != ASCIICHAT_OK);
  buffer_release(large);
  CHECK(((const unsigned char *)shared)[0] == 0xab);
  buffer_domain_close(&domain);
  CHECK(domain == NULL);
  CHECK(buffer_capacity(shared) >= 32768);
  buffer_release(shared); /* The final reference destroys the closed domain. */
  CHECK(buffer_process_reserved() == baseline);
}

static void workspace_contracts(void) {
  buffer_domain_t *domain = NULL;
  buffer_domain_config_t config = {4096, 2048, 0, false};
  CHECK(buffer_domain_create(&config, &domain) == ASCIICHAT_OK);
  buffer_workspace_t workspace = {0};
  CHECK(buffer_workspace_reserve(&workspace, domain, 100) == ASCIICHAT_OK);
  memcpy(workspace.data, "retained", 9);
  workspace.length = 9;
  void *original = workspace.data;
  CHECK(buffer_workspace_reserve(&workspace, domain, 90) == ASCIICHAT_OK);
  CHECK(workspace.data == original);
  CHECK(buffer_workspace_reserve(&workspace, domain, 500) == ASCIICHAT_OK);
  CHECK(strcmp(workspace.data, "retained") == 0);
  original = workspace.data;
  CHECK(buffer_workspace_reserve(&workspace, domain, 8192) != ASCIICHAT_OK);
  CHECK(workspace.data == original && workspace.length == 9);
  buffer_domain_close(&domain);
  buffer_workspace_destroy(&workspace);
  CHECK(!workspace.data && !workspace.length);
}

static void queue_contracts(void) {
  packet_queue_config_t config = {2, 128, PACKET_QUEUE_REJECT};
  packet_queue_t *queue = packet_queue_create_config(&config);
  CHECK(queue);
  void *data = buffer_alloc(NULL, 80);
  CHECK(data);
  memset(data, 42, 80);
  void *original = data;
  CHECK(packet_queue_push_take(queue, PACKET_TYPE_PING, &data, 80, 1) == ASCIICHAT_OK);
  CHECK(!data);
  data = buffer_alloc(NULL, 80);
  CHECK(data);
  CHECK(packet_queue_push_take(queue, PACKET_TYPE_PING, &data, 80, 2) != ASCIICHAT_OK);
  CHECK(data); /* Byte limit rejects without consuming ownership. */
  buffer_release(data);
  queued_packet_t *packet = packet_queue_try_dequeue(queue);
  CHECK(packet && packet->data == original && packet->data_len == 80);
  CHECK(packet_queue_push_copy(queue, PACKET_TYPE_PING, "copy", 5, 3) == ASCIICHAT_OK);
  packet_queue_stop(queue);
  CHECK(!packet_queue_try_dequeue(queue));
  data = buffer_alloc(NULL, 10);
  CHECK(data);
  CHECK(packet_queue_push_take(queue, PACKET_TYPE_PING, &data, 10, 4) != ASCIICHAT_OK);
  CHECK(data);
  buffer_release(data);
  packet_queue_destroy(queue); /* Clears a stopped queue, preserves dequeued owners. */
  CHECK(((const unsigned char *)packet->data)[0] == 42);
  packet_queue_free_packet(packet);

  config.overflow = PACKET_QUEUE_DROP_OLDEST;
  queue = packet_queue_create_config(&config);
  for (unsigned i = 0; i < 10; i++)
    CHECK(packet_queue_push_copy(queue, PACKET_TYPE_IMAGE_FRAME, &i, sizeof(i), i) == ASCIICHAT_OK);
  uint64_t enqueued = 0, dropped = 0;
  packet_queue_get_stats(queue, &enqueued, NULL, &dropped);
  CHECK(enqueued == 10 && dropped == 8 && packet_queue_size(queue) == 2);
  packet = packet_queue_dequeue(queue);
  CHECK(packet && NET_TO_HOST_U32(packet->header.client_id) == 8);
  packet_queue_free_packet(packet);
  packet_queue_destroy(queue);

  queue = packet_queue_create(2);
  data = buffer_alloc(NULL, 4);
  CHECK(data);
  memcpy(data, "abc", 4);
  CHECK(packet_queue_push_shared(queue, PACKET_TYPE_PING, data, 4, 0) == ASCIICHAT_OK);
  buffer_release(data);
  packet = packet_queue_dequeue(queue);
  CHECK(packet && strcmp(packet->data, "abc") == 0);
  packet_queue_free_packet(packet);
  CHECK(packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 0, 0) == ASCIICHAT_OK);
  packet = packet_queue_dequeue(queue);
  CHECK(packet && !packet->data && packet->data_len == 0);
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
    CHECK(data);
    memset(data, (int)(i & 255), size);
    const void *shared = buffer_share(data);
    buffer_release(data);
    CHECK(((const unsigned char *)shared)[size - 1] == (i & 255));
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
  CHECK(buffer_domain_create(&config, &stress.domain) == ASCIICHAT_OK);
  atomic_store_u64(&stress.running, 4);
  asciichat_thread_t workers[4], trimmer;
  CHECK(asciichat_thread_create(&trimmer, "buffer-trim", trim_worker, &stress) == 0);
  for (unsigned i = 0; i < 4; i++)
    CHECK(asciichat_thread_create(&workers[i], "buffer-allocate", allocate_worker, &stress) == 0);
  for (unsigned i = 0; i < 4; i++)
    CHECK(asciichat_thread_join(&workers[i], NULL) == 0);
  CHECK(asciichat_thread_join(&trimmer, NULL) == 0);
  buffer_stats_t stats;
  buffer_domain_stats(stress.domain, &stats);
  CHECK(!stats.outstanding && !stats.live_bytes && !stats.requested_bytes);
  buffer_domain_close(&stress.domain);
  CHECK(buffer_process_reserved() == baseline);
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
    CHECK(data);
    ((unsigned *)data)[0] = i;
    ((unsigned *)data)[1] = ~i;
    asciichat_error_t result;
    while ((result = packet_queue_push_take(producer->stress->queue, PACKET_TYPE_PING, &data, 2 * sizeof(unsigned),
                                            i)) != ASCIICHAT_OK) {
      CHECK(result == ERROR_BUFFER && data);
      platform_sleep_ns(100000);
    }
    CHECK(!data);
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
    CHECK(packet->data_len == 2 * sizeof(unsigned) && data[0] < 2000 && data[1] == ~data[0]);
    CHECK(atomic_fetch_add_u64(&stress->seen[data[0]], 1) == 0);
    atomic_fetch_add_u64(&stress->consumed, 1);
    packet_queue_free_packet(packet);
  }
  return NULL;
}

static void concurrent_queue_contracts(void) {
  queue_stress_t *stress = SAFE_CALLOC(1, sizeof(*stress), queue_stress_t *);
  stress->queue = packet_queue_create_config(&(packet_queue_config_t){32, 512, PACKET_QUEUE_REJECT});
  CHECK(stress->queue);
  atomic_store_u64(&stress->producers, 2);
  producer_t producers[2] = {{stress, 0}, {stress, 1000}};
  asciichat_thread_t threads[4];
  for (unsigned i = 0; i < 2; i++) {
    CHECK(asciichat_thread_create(&threads[i], "queue-producer", queue_producer, &producers[i]) == 0);
    CHECK(asciichat_thread_create(&threads[i + 2], "queue-consumer", queue_consumer, stress) == 0);
  }
  for (unsigned i = 0; i < 4; i++)
    CHECK(asciichat_thread_join(&threads[i], NULL) == 0);
  CHECK(atomic_load_u64(&stress->consumed) == 2000);
  packet_queue_destroy(stress->queue);
  SAFE_FREE(stress);
}

static void process_budget_contracts(void) {
  size_t baseline = buffer_process_reserved();
  buffer_domain_t *media = NULL, *control = NULL;
  buffer_domain_config_t config = {BUFFER_PROCESS_LIMIT, 0, 0, false};
  CHECK(buffer_domain_create(&config, &media) == ASCIICHAT_OK);
  config.control = true;
  CHECK(buffer_domain_create(&config, &control) == ASCIICHAT_OK);
  void *blocks[512] = {0};
  size_t count = 0;
  while (count < 512 && (blocks[count] = buffer_alloc(media, 1024 * 1024)))
    count++;
  CHECK(count > 0 && count < 512);
  CHECK(buffer_process_reserved() <= BUFFER_PROCESS_LIMIT - BUFFER_CONTROL_RESERVE);
  void *message = buffer_alloc(control, BUFFER_CONTROL_RESERVE / 2);
  CHECK(message && buffer_process_reserved() <= BUFFER_PROCESS_LIMIT);
  buffer_domain_close(&media);
  buffer_domain_close(&control);
  for (size_t i = 0; i < count; i++)
    buffer_release(blocks[i]);
  buffer_release(message);
  CHECK(buffer_process_reserved() == baseline);
}

static void media_contracts(void) {
  image_t *image = image_new(641, 479);
  CHECK(image && (uintptr_t)image->pixels % 64 == 0);
  memset(image->pixels, 42, (size_t)image->w * image->h * sizeof(rgb_pixel_t));
  image_destroy(image);
  char input[8192];
  memset(input, 'a', sizeof(input));
  void *compressed = NULL;
  size_t length = 0;
  CHECK(compress_data(input, sizeof(input), &compressed, &length, 1) == ASCIICHAT_OK);
  char output[8192];
  CHECK(decompress_data(compressed, length, output, sizeof(output)) == ASCIICHAT_OK);
  CHECK(memcmp(input, output, sizeof(input)) == 0);
  buffer_release(compressed);
  compression_workspace_t workspace = {0};
  CHECK(compress_workspace(&workspace, input, sizeof(input), 1) == ASCIICHAT_OK);
  void *storage = workspace.output.data;
  void *codec = workspace.codec;
  for (int i = 0; i < 100; i++) {
    input[i] = (char)i;
    CHECK(compress_workspace(&workspace, input, sizeof(input), 1) == ASCIICHAT_OK);
    CHECK(workspace.output.data == storage && workspace.codec == codec);
    CHECK(decompress_data(workspace.output.data, workspace.output.length, output, sizeof(output)) == ASCIICHAT_OK);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
  }
  compression_workspace_destroy(&workspace);
  CHECK(!workspace.codec && !workspace.output.data);
}

int main(void) {
  log_init(NULL, LOG_WARN, false, false);
  buffer_system_init();
  allocation_contracts();
  workspace_contracts();
  queue_contracts();
  concurrent_contracts();
  concurrent_queue_contracts();
  process_budget_contracts();
  media_contracts();
  buffer_system_cleanup();
  CHECK(buffer_process_reserved() == 0);
  log_info("Buffer ownership, budgets, queues, concurrency and media contracts passed");
  return 0;
}
