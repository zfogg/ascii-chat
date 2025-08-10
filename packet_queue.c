#include "packet_queue.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

packet_queue_t *packet_queue_create(size_t max_size) {
  packet_queue_t *queue;
  SAFE_MALLOC(queue, sizeof(packet_queue_t), packet_queue_t *);

  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;
  queue->max_size = max_size;
  queue->bytes_queued = 0;

  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->not_empty, NULL);
  pthread_cond_init(&queue->not_full, NULL);

  queue->packets_enqueued = 0;
  queue->packets_dequeued = 0;
  queue->packets_dropped = 0;
  queue->shutdown = false;

  return queue;
}

void packet_queue_destroy(packet_queue_t *queue) {
  if (!queue)
    return;

  // Signal shutdown first
  packet_queue_shutdown(queue);

  // Clear any remaining packets
  packet_queue_clear(queue);

  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->not_empty);
  pthread_cond_destroy(&queue->not_full);

  free(queue);
}

int packet_queue_enqueue(packet_queue_t *queue, packet_type_t type, const void *data, size_t data_len,
                         uint32_t client_id, bool copy_data) {
  if (!queue)
    return -1;

  pthread_mutex_lock(&queue->mutex);

  // Check if shutdown
  if (queue->shutdown) {
    pthread_mutex_unlock(&queue->mutex);
    return -1;
  }

  // Check if queue is full
  if (queue->max_size > 0 && queue->count >= queue->max_size) {
    // Drop oldest packet (head)
    if (queue->head) {
      packet_node_t *old_head = queue->head;
      queue->head = queue->head->next;
      if (queue->head == NULL) {
        queue->tail = NULL;
      }

      queue->bytes_queued -= old_head->packet.data_len;
      if (old_head->packet.owns_data && old_head->packet.data) {
        free(old_head->packet.data);
      }
      free(old_head);
      queue->count--;
      queue->packets_dropped++;

      log_debug("Dropped packet from queue (full): type=%d, client=%u", type, client_id);
    }
  }

  // Create new node
  packet_node_t *node;
  SAFE_MALLOC(node, sizeof(packet_node_t), packet_node_t *);

  // Build packet header
  node->packet.header.magic = htonl(PACKET_MAGIC);
  node->packet.header.type = htons((uint16_t)type);
  node->packet.header.length = htonl((uint32_t)data_len);
  node->packet.header.sequence = htonl(get_next_sequence());
  node->packet.header.client_id = htonl(client_id);

  // Calculate CRC on the data
  uint32_t crc = 0;
  if (data_len > 0 && data) {
    crc = asciichat_crc32(data, data_len);
  }
  node->packet.header.crc32 = htonl(crc);

  // Handle data
  if (data_len > 0 && data) {
    if (copy_data) {
      // Copy the data
      SAFE_MALLOC(node->packet.data, data_len, void *);
      memcpy(node->packet.data, data, data_len);
      node->packet.owns_data = true;
    } else {
      // Use the data pointer directly (caller must ensure it stays valid)
      node->packet.data = (void *)data;
      node->packet.owns_data = false;
    }
  } else {
    node->packet.data = NULL;
    node->packet.owns_data = false;
  }

  node->packet.data_len = data_len;
  node->next = NULL;

  // Add to queue
  if (queue->tail) {
    queue->tail->next = node;
  } else {
    queue->head = node;
  }
  queue->tail = node;

  queue->count++;
  queue->bytes_queued += data_len;
  queue->packets_enqueued++;

  // Signal that queue is not empty
  pthread_cond_signal(&queue->not_empty);

  pthread_mutex_unlock(&queue->mutex);

  return 0;
}

int packet_queue_enqueue_packet(packet_queue_t *queue, const queued_packet_t *packet) {
  if (!queue || !packet)
    return -1;

  pthread_mutex_lock(&queue->mutex);

  // Check if shutdown
  if (queue->shutdown) {
    pthread_mutex_unlock(&queue->mutex);
    return -1;
  }

  // Check if queue is full
  if (queue->max_size > 0 && queue->count >= queue->max_size) {
    // Drop oldest packet
    if (queue->head) {
      packet_node_t *old_head = queue->head;
      queue->head = queue->head->next;
      if (queue->head == NULL) {
        queue->tail = NULL;
      }

      queue->bytes_queued -= old_head->packet.data_len;
      if (old_head->packet.owns_data && old_head->packet.data) {
        free(old_head->packet.data);
      }
      free(old_head);
      queue->count--;
      queue->packets_dropped++;
    }
  }

  // Create new node
  packet_node_t *node;
  SAFE_MALLOC(node, sizeof(packet_node_t), packet_node_t *);

  // Copy the packet
  memcpy(&node->packet, packet, sizeof(queued_packet_t));
  node->next = NULL;

  // Add to queue
  if (queue->tail) {
    queue->tail->next = node;
  } else {
    queue->head = node;
  }
  queue->tail = node;

  queue->count++;
  queue->bytes_queued += packet->data_len;
  queue->packets_enqueued++;

  // Signal that queue is not empty
  pthread_cond_signal(&queue->not_empty);

  pthread_mutex_unlock(&queue->mutex);

  return 0;
}

queued_packet_t *packet_queue_dequeue(packet_queue_t *queue) {
  if (!queue)
    return NULL;

  pthread_mutex_lock(&queue->mutex);

  // Wait while queue is empty and not shutdown
  while (queue->count == 0 && !queue->shutdown) {
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
  }

  // Check if shutdown with empty queue
  if (queue->count == 0 && queue->shutdown) {
    pthread_mutex_unlock(&queue->mutex);
    return NULL;
  }

  // Remove from head
  packet_node_t *node = queue->head;
  if (node) {
    queue->head = node->next;
    if (queue->head == NULL) {
      queue->tail = NULL;
    }

    queue->count--;
    queue->bytes_queued -= node->packet.data_len;
    queue->packets_dequeued++;

    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
  }

  pthread_mutex_unlock(&queue->mutex);

  if (node) {
    // Extract packet and free node
    queued_packet_t *packet;
    SAFE_MALLOC(packet, sizeof(queued_packet_t), queued_packet_t *);
    memcpy(packet, &node->packet, sizeof(queued_packet_t));
    free(node);
    return packet;
  }

  return NULL;
}

queued_packet_t *packet_queue_try_dequeue(packet_queue_t *queue) {
  if (!queue)
    return NULL;

  pthread_mutex_lock(&queue->mutex);

  if (queue->count == 0) {
    pthread_mutex_unlock(&queue->mutex);
    return NULL;
  }

  // Remove from head
  packet_node_t *node = queue->head;
  if (node) {
    queue->head = node->next;
    if (queue->head == NULL) {
      queue->tail = NULL;
    }

    queue->count--;
    queue->bytes_queued -= node->packet.data_len;
    queue->packets_dequeued++;

    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
  }

  pthread_mutex_unlock(&queue->mutex);

  if (node) {
    // Extract packet and free node
    queued_packet_t *packet;
    SAFE_MALLOC(packet, sizeof(queued_packet_t), queued_packet_t *);
    memcpy(packet, &node->packet, sizeof(queued_packet_t));
    free(node);
    return packet;
  }

  return NULL;
}

void packet_queue_free_packet(queued_packet_t *packet) {
  if (!packet)
    return;

  if (packet->owns_data && packet->data) {
    free(packet->data);
  }
  free(packet);
}

size_t packet_queue_size(packet_queue_t *queue) {
  if (!queue)
    return 0;

  pthread_mutex_lock(&queue->mutex);
  size_t size = queue->count;
  pthread_mutex_unlock(&queue->mutex);

  return size;
}

bool packet_queue_is_empty(packet_queue_t *queue) {
  return packet_queue_size(queue) == 0;
}

bool packet_queue_is_full(packet_queue_t *queue) {
  if (!queue || queue->max_size == 0)
    return false;

  pthread_mutex_lock(&queue->mutex);
  bool full = (queue->count >= queue->max_size);
  pthread_mutex_unlock(&queue->mutex);

  return full;
}

void packet_queue_shutdown(packet_queue_t *queue) {
  if (!queue)
    return;

  pthread_mutex_lock(&queue->mutex);
  queue->shutdown = true;
  pthread_cond_broadcast(&queue->not_empty); // Wake all waiting threads
  pthread_cond_broadcast(&queue->not_full);
  pthread_mutex_unlock(&queue->mutex);
}

void packet_queue_clear(packet_queue_t *queue) {
  if (!queue)
    return;

  pthread_mutex_lock(&queue->mutex);

  while (queue->head) {
    packet_node_t *node = queue->head;
    queue->head = node->next;

    if (node->packet.owns_data && node->packet.data) {
      free(node->packet.data);
    }
    free(node);
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;
  queue->bytes_queued = 0;

  pthread_mutex_unlock(&queue->mutex);
}

void packet_queue_get_stats(packet_queue_t *queue, uint64_t *enqueued, uint64_t *dequeued, uint64_t *dropped) {
  if (!queue)
    return;

  pthread_mutex_lock(&queue->mutex);
  if (enqueued)
    *enqueued = queue->packets_enqueued;
  if (dequeued)
    *dequeued = queue->packets_dequeued;
  if (dropped)
    *dropped = queue->packets_dropped;
  pthread_mutex_unlock(&queue->mutex);
}