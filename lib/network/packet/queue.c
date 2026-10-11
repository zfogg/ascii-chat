#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/network/crc32.h>
#include <string.h>

typedef struct packet_node {
  queued_packet_t packet;
  struct packet_node *next;
} packet_node_t;

struct packet_queue {
  mutex_t mutex;
  packet_node_t *head;
  packet_node_t *tail;
  buffer_domain_t *domain;
  packet_queue_config_t config;
  size_t count;
  size_t bytes;
  uint64_t enqueued;
  uint64_t dequeued;
  uint64_t dropped;
  bool stopped;
};

packet_queue_t *packet_queue_create_config(const packet_queue_config_t *config) {
  if (!config || !config->max_bytes ||
      (config->overflow != PACKET_QUEUE_REJECT && config->overflow != PACKET_QUEUE_DROP_OLDEST)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Queue needs a byte limit and a valid overflow policy");
    return NULL;
  }
  packet_queue_t *queue = SAFE_CALLOC(1, sizeof(*queue), packet_queue_t *);
  queue->config = *config;
  if (mutex_init(&queue->mutex, "packet_queue") != 0) {
    SAFE_FREE(queue);
    return NULL;
  }
  buffer_domain_config_t memory = {64 * 1024 * 1024, 8 * 1024 * 1024, 5000000000ULL, false};
  if (buffer_domain_create(&memory, &queue->domain) != ASCIICHAT_OK) {
    mutex_destroy(&queue->mutex);
    SAFE_FREE(queue);
    return NULL;
  }
  return queue;
}

packet_queue_t *packet_queue_create(size_t max_packets) {
  packet_queue_config_t config = {max_packets, 16 * 1024 * 1024, PACKET_QUEUE_REJECT};
  return packet_queue_create_config(&config);
}

/* All list access, including reclamation, is serialized by the queue mutex. */
static packet_node_t *pop_locked(packet_queue_t *queue) {
  packet_node_t *node = queue->head;
  if (node) {
    queue->head = node->next;
    if (!queue->head)
      queue->tail = NULL;
    queue->count--;
    queue->bytes -= node->packet.data_len;
    node->next = NULL;
  }
  return node;
}

void packet_queue_free_packet(queued_packet_t *packet) {
  if (!packet)
    return;
  buffer_release(packet->data);
  /* packet is the first member of the owning node allocation. */
  buffer_release(packet);
}

static asciichat_error_t push(packet_queue_t *queue, packet_type_t type, const void *data, size_t length,
                              uint32_t client_id) {
  if (!queue || length > UINT32_MAX || (length && !data) || type == 0 || type > 10000)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid queued packet");
  mutex_lock(&queue->mutex);
  if (queue->stopped) {
    mutex_unlock(&queue->mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Packet queue stopped");
  }
  if (length > queue->config.max_bytes) {
    mutex_unlock(&queue->mutex);
    return SET_ERRNO(ERROR_BUFFER, "Packet exceeds queue byte limit");
  }
  bool full = (queue->config.max_packets && queue->count >= queue->config.max_packets) ||
              length > queue->config.max_bytes - queue->bytes;
  if (full && queue->config.overflow == PACKET_QUEUE_REJECT) {
    mutex_unlock(&queue->mutex);
    return SET_ERRNO(ERROR_BUFFER, "Packet queue is full");
  }
  packet_node_t *node = buffer_alloc(queue->domain, sizeof(*node));
  if (!node) {
    mutex_unlock(&queue->mutex);
    return SET_ERRNO(ERROR_MEMORY, "Queue memory budget exhausted");
  }
  /* Do not drop existing packets unless the new packet can be admitted. */
  while (queue->head && ((queue->config.max_packets && queue->count >= queue->config.max_packets) ||
                         length > queue->config.max_bytes - queue->bytes)) {
    packet_node_t *old = pop_locked(queue);
    queue->dropped++;
    packet_queue_free_packet(&old->packet);
  }
  memset(node, 0, sizeof(*node));
  node->packet.header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
  node->packet.header.type = HOST_TO_NET_U16((uint16_t)type);
  node->packet.header.length = HOST_TO_NET_U32((uint32_t)length);
  node->packet.header.client_id = HOST_TO_NET_U32(client_id);
  node->packet.header.crc32 = HOST_TO_NET_U32(length ? asciichat_crc32(data, length) : 0);
  node->packet.data = data;
  node->packet.data_len = length;
  if (queue->tail)
    queue->tail->next = node;
  else
    queue->head = node;
  queue->tail = node;
  queue->count++;
  queue->bytes += length;
  queue->enqueued++;
  mutex_unlock(&queue->mutex);
  return ASCIICHAT_OK;
}

asciichat_error_t packet_queue_push_take(packet_queue_t *queue, packet_type_t type, void **data, size_t length,
                                         uint32_t client_id) {
  if (!data || (length && !*data) || length > buffer_capacity(*data))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid owned packet buffer");
  asciichat_error_t result = push(queue, type, *data, length, client_id);
  if (result == ASCIICHAT_OK)
    *data = NULL;
  return result;
}

asciichat_error_t packet_queue_push_shared(packet_queue_t *queue, packet_type_t type, const void *data, size_t length,
                                           uint32_t client_id) {
  if (length > buffer_capacity(data))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Shared packet exceeds buffer capacity");
  const void *shared = buffer_share(data);
  if (data && !shared)
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot share packet buffer");
  asciichat_error_t result = push(queue, type, shared, length, client_id);
  if (result != ASCIICHAT_OK)
    buffer_release(shared);
  return result;
}

asciichat_error_t packet_queue_push_copy(packet_queue_t *queue, packet_type_t type, const void *data, size_t length,
                                         uint32_t client_id) {
  if (!queue || (length && !data) || length > queue->config.max_bytes)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid packet copy");
  void *copy = NULL;
  if (length) {
    copy = buffer_alloc(queue->domain, length);
    if (!copy)
      return SET_ERRNO(ERROR_MEMORY, "Cannot copy packet within memory budget");
    memcpy(copy, data, length);
  }
  asciichat_error_t result = packet_queue_push_take(queue, type, &copy, length, client_id);
  buffer_release(copy);
  return result;
}

queued_packet_t *packet_queue_try_dequeue(packet_queue_t *queue) {
  if (!queue)
    return NULL;
  mutex_lock(&queue->mutex);
  packet_node_t *node = queue->stopped ? NULL : pop_locked(queue);
  if (node)
    queue->dequeued++;
  mutex_unlock(&queue->mutex);
  if (node && !packet_queue_validate_packet(&node->packet)) {
    packet_queue_free_packet(&node->packet);
    return NULL;
  }
  return node ? &node->packet : NULL;
}

queued_packet_t *packet_queue_dequeue(packet_queue_t *queue) {
  return packet_queue_try_dequeue(queue);
}

void packet_queue_clear(packet_queue_t *queue) {
  if (!queue)
    return;
  mutex_lock(&queue->mutex);
  packet_node_t *node;
  while ((node = pop_locked(queue)))
    packet_queue_free_packet(&node->packet);
  mutex_unlock(&queue->mutex);
}

void packet_queue_stop(packet_queue_t *queue) {
  if (!queue)
    return;
  mutex_lock(&queue->mutex);
  queue->stopped = true;
  mutex_unlock(&queue->mutex);
}

void packet_queue_destroy(packet_queue_t *queue) {
  if (!queue)
    return;
  packet_queue_stop(queue);
  packet_queue_clear(queue);
  buffer_domain_close(&queue->domain);
  mutex_destroy(&queue->mutex);
  SAFE_FREE(queue);
}

size_t packet_queue_size(packet_queue_t *queue) {
  if (!queue)
    return 0;
  mutex_lock(&queue->mutex);
  size_t count = queue->count;
  mutex_unlock(&queue->mutex);
  return count;
}

bool packet_queue_is_empty(packet_queue_t *queue) {
  return packet_queue_size(queue) == 0;
}

bool packet_queue_is_full(packet_queue_t *queue) {
  if (!queue)
    return false;
  mutex_lock(&queue->mutex);
  bool full = (queue->config.max_packets && queue->count >= queue->config.max_packets) ||
              queue->bytes >= queue->config.max_bytes;
  mutex_unlock(&queue->mutex);
  return full;
}

void packet_queue_get_stats(packet_queue_t *queue, uint64_t *enqueued, uint64_t *dequeued, uint64_t *dropped) {
  if (!queue)
    return;
  mutex_lock(&queue->mutex);
  if (enqueued)
    *enqueued = queue->enqueued;
  if (dequeued)
    *dequeued = queue->dequeued;
  if (dropped)
    *dropped = queue->dropped;
  mutex_unlock(&queue->mutex);
}

bool packet_queue_validate_packet(const queued_packet_t *packet) {
  return packet && NET_TO_HOST_U64(packet->header.magic) == PACKET_MAGIC && NET_TO_HOST_U16(packet->header.type) > 0 &&
         NET_TO_HOST_U16(packet->header.type) <= 10000 && NET_TO_HOST_U32(packet->header.length) == packet->data_len &&
         (!packet->data_len ||
          (packet->data && NET_TO_HOST_U32(packet->header.crc32) == asciichat_crc32(packet->data, packet->data_len)));
}
