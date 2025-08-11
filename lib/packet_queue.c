#include "packet_queue.h"
#include "buffer_pool.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Memory Pool Implementation
 * ============================================================================
 */

node_pool_t *node_pool_create(size_t pool_size) {
  if (pool_size == 0) {
    return NULL;
  }

  node_pool_t *pool;
  SAFE_MALLOC(pool, sizeof(node_pool_t), node_pool_t *);

  // Allocate all nodes at once
  SAFE_MALLOC(pool->nodes, sizeof(packet_node_t) * pool_size, packet_node_t *);

  // Link all nodes into free list
  for (size_t i = 0; i < pool_size - 1; i++) {
    pool->nodes[i].next = &pool->nodes[i + 1];
  }
  pool->nodes[pool_size - 1].next = NULL;

  pool->free_list = &pool->nodes[0];
  pool->pool_size = pool_size;
  pool->used_count = 0;

  pthread_mutex_init(&pool->pool_mutex, NULL);

  return pool;
}

void node_pool_destroy(node_pool_t *pool) {
  if (!pool) {
    return;
  }

  pthread_mutex_destroy(&pool->pool_mutex);
  free(pool->nodes);
  free(pool);
}

packet_node_t *node_pool_get(node_pool_t *pool) {
  if (!pool) {
    // No pool, fallback to malloc
    packet_node_t *node;
    SAFE_MALLOC(node, sizeof(packet_node_t), packet_node_t *);
    return node;
  }

  pthread_mutex_lock(&pool->pool_mutex);

  packet_node_t *node = pool->free_list;
  if (node) {
    pool->free_list = node->next;
    pool->used_count++;
    node->next = NULL; // Clear next pointer
  }

  pthread_mutex_unlock(&pool->pool_mutex);

  if (!node) {
    // Pool exhausted, fallback to malloc
    SAFE_MALLOC(node, sizeof(packet_node_t), packet_node_t *);
    log_debug("Memory pool exhausted, falling back to malloc (used: %zu/%zu)", pool->used_count, pool->pool_size);
  }

  return node;
}

void node_pool_put(node_pool_t *pool, packet_node_t *node) {
  if (!node) {
    return;
  }

  if (!pool) {
    // No pool, just free
    free(node);
    return;
  }

  // Check if this node is from our pool
  bool is_pool_node = (node >= pool->nodes && node < pool->nodes + pool->pool_size);

  if (is_pool_node) {
    pthread_mutex_lock(&pool->pool_mutex);

    // Return to free list
    node->next = pool->free_list;
    pool->free_list = node;
    pool->used_count--;

    pthread_mutex_unlock(&pool->pool_mutex);
  } else {
    // This was malloc'd, so free it
    free(node);
  }
}

/* ============================================================================
 * Packet Queue Implementation
 * ============================================================================
 */

packet_queue_t *packet_queue_create(size_t max_size) {
  return packet_queue_create_with_pool(max_size, 0); // No pool by default
}

packet_queue_t *packet_queue_create_with_pool(size_t max_size, size_t pool_size) {
  return packet_queue_create_with_pools(max_size, pool_size, false);
}

packet_queue_t *packet_queue_create_with_pools(size_t max_size, size_t node_pool_size, bool use_buffer_pool) {
  packet_queue_t *queue;
  SAFE_MALLOC(queue, sizeof(packet_queue_t), packet_queue_t *);

  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;
  queue->max_size = max_size;
  queue->bytes_queued = 0;

  // Create memory pools if requested
  queue->node_pool = node_pool_size > 0 ? node_pool_create(node_pool_size) : NULL;
  queue->buffer_pool = use_buffer_pool ? data_buffer_pool_create() : NULL;

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

  // Log buffer pool statistics before destroying
  if (queue->buffer_pool) {
    uint64_t hits, misses;
    data_buffer_pool_get_stats(queue->buffer_pool, &hits, &misses);
    if (hits + misses > 0) {
      log_info("Buffer pool stats: %llu hits (%.1f%%), %llu misses", (unsigned long long)hits,
               (double)hits * 100.0 / (hits + misses), (unsigned long long)misses);
    }
  }

  // Destroy memory pools if present
  if (queue->node_pool) {
    node_pool_destroy(queue->node_pool);
  }
  if (queue->buffer_pool) {
    data_buffer_pool_destroy(queue->buffer_pool);
  }

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
        data_buffer_pool_free(old_head->packet.buffer_pool, old_head->packet.data, old_head->packet.data_len);
      }
      node_pool_put(queue->node_pool, old_head);
      queue->count--;
      queue->packets_dropped++;

      log_debug("Dropped packet from queue (full): type=%d, client=%u", type, client_id);
    }
  }

  // Create new node (use pool if available)
  packet_node_t *node = node_pool_get(queue->node_pool);

  // Build packet header
  node->packet.header.magic = htonl(PACKET_MAGIC);
  node->packet.header.type = htons((uint16_t)type);
  node->packet.header.length = htonl((uint32_t)data_len);
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
      // Try to allocate from buffer pool (local or global)
      if (queue->buffer_pool) {
        node->packet.data = data_buffer_pool_alloc(queue->buffer_pool, data_len);
        node->packet.buffer_pool = queue->buffer_pool;
      } else {
        // Use global pool if no local pool
        node->packet.data = buffer_pool_alloc(data_len);
        node->packet.buffer_pool = data_buffer_pool_get_global();
      }
      memcpy(node->packet.data, data, data_len);
      node->packet.owns_data = true;
    } else {
      // Use the data pointer directly (caller must ensure it stays valid)
      node->packet.data = (void *)data;
      node->packet.owns_data = false;
      node->packet.buffer_pool = NULL;
    }
  } else {
    node->packet.data = NULL;
    node->packet.owns_data = false;
    node->packet.buffer_pool = NULL;
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

  // Validate packet before enqueueing
  if (!packet_queue_validate_packet(packet)) {
    log_error("Refusing to enqueue invalid packet");
    return -1;
  }

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
        data_buffer_pool_free(old_head->packet.buffer_pool, old_head->packet.data, old_head->packet.data_len);
      }
      node_pool_put(queue->node_pool, old_head);
      queue->count--;
      queue->packets_dropped++;
    }
  }

  // Create new node (use pool if available)
  packet_node_t *node = node_pool_get(queue->node_pool);

  // Copy the packet header
  memcpy(&node->packet, packet, sizeof(queued_packet_t));

  // Deep copy the data if needed
  if (packet->data && packet->data_len > 0 && packet->owns_data) {
    // If the packet owns its data, we need to make a copy
    // Try to allocate from buffer pool (local or global)
    void *data_copy;
    if (queue->buffer_pool) {
      data_copy = data_buffer_pool_alloc(queue->buffer_pool, packet->data_len);
      node->packet.buffer_pool = queue->buffer_pool;
    } else {
      // Use global pool if no local pool
      data_copy = buffer_pool_alloc(packet->data_len);
      node->packet.buffer_pool = data_buffer_pool_get_global();
    }
    memcpy(data_copy, packet->data, packet->data_len);
    node->packet.data = data_copy;
    node->packet.owns_data = true;
  } else {
    // Either no data or packet doesn't own it (shared reference is OK)
    node->packet.data = packet->data;
    node->packet.owns_data = packet->owns_data;
    node->packet.buffer_pool = packet->buffer_pool; // Preserve original pool reference
  }

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
    // Verify packet magic number for corruption detection
    uint32_t magic = ntohl(node->packet.header.magic);
    if (magic != PACKET_MAGIC) {
      log_error("CORRUPTION: Invalid magic in dequeued packet: 0x%x (expected 0x%x), type=%u", magic, PACKET_MAGIC,
                ntohs(node->packet.header.type));
      // Still return node to pool but don't return corrupted packet
      node_pool_put(queue->node_pool, node);
      return NULL;
    }

    // Validate CRC if there's data
    if (node->packet.data_len > 0 && node->packet.data) {
      uint32_t expected_crc = ntohl(node->packet.header.crc32);
      uint32_t actual_crc = asciichat_crc32(node->packet.data, node->packet.data_len);
      if (actual_crc != expected_crc) {
        log_error("CORRUPTION: CRC mismatch in dequeued packet: got 0x%x, expected 0x%x, type=%u, len=%zu", actual_crc,
                  expected_crc, ntohs(node->packet.header.type), node->packet.data_len);
        // Free data if packet owns it
        if (node->packet.owns_data && node->packet.data) {
          // Use buffer_pool_free for global pool allocations, data_buffer_pool_free for local pools
          if (node->packet.buffer_pool) {
            data_buffer_pool_free(node->packet.buffer_pool, node->packet.data, node->packet.data_len);
          } else {
            // This was allocated from global pool or malloc, use buffer_pool_free which handles both
            buffer_pool_free(node->packet.data, node->packet.data_len);
          }
          // CRITICAL: Clear pointer to prevent double-free when packet is copied later
          node->packet.data = NULL;
          node->packet.owns_data = false;
        }
        node_pool_put(queue->node_pool, node);
        return NULL;
      }
    }

    // Extract packet and return node to pool
    queued_packet_t *packet;
    SAFE_MALLOC(packet, sizeof(queued_packet_t), queued_packet_t *);
    memcpy(packet, &node->packet, sizeof(queued_packet_t));
    node_pool_put(queue->node_pool, node);
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
    // Verify packet magic number for corruption detection
    uint32_t magic = ntohl(node->packet.header.magic);
    if (magic != PACKET_MAGIC) {
      log_error("CORRUPTION: Invalid magic in try_dequeued packet: 0x%x (expected 0x%x), type=%u", magic, PACKET_MAGIC,
                ntohs(node->packet.header.type));
      // Still return node to pool but don't return corrupted packet
      node_pool_put(queue->node_pool, node);
      return NULL;
    }

    // Validate CRC if there's data
    if (node->packet.data_len > 0 && node->packet.data) {
      uint32_t expected_crc = ntohl(node->packet.header.crc32);
      uint32_t actual_crc = asciichat_crc32(node->packet.data, node->packet.data_len);
      if (actual_crc != expected_crc) {
        log_error("CORRUPTION: CRC mismatch in try_dequeued packet: got 0x%x, expected 0x%x, type=%u, len=%zu",
                  actual_crc, expected_crc, ntohs(node->packet.header.type), node->packet.data_len);
        // Free data if packet owns it
        if (node->packet.owns_data && node->packet.data) {
          // Use buffer_pool_free for global pool allocations, data_buffer_pool_free for local pools
          if (node->packet.buffer_pool) {
            data_buffer_pool_free(node->packet.buffer_pool, node->packet.data, node->packet.data_len);
          } else {
            // This was allocated from global pool or malloc, use buffer_pool_free which handles both
            buffer_pool_free(node->packet.data, node->packet.data_len);
          }
          // CRITICAL: Clear pointer to prevent double-free when packet is copied later
          node->packet.data = NULL;
          node->packet.owns_data = false;
        }
        node_pool_put(queue->node_pool, node);
        return NULL;
      }
    }

    // Extract packet and return node to pool
    queued_packet_t *packet;
    SAFE_MALLOC(packet, sizeof(queued_packet_t), queued_packet_t *);
    memcpy(packet, &node->packet, sizeof(queued_packet_t));
    node_pool_put(queue->node_pool, node);
    return packet;
  }

  return NULL;
}

void packet_queue_free_packet(queued_packet_t *packet) {
  if (!packet)
    return;

  if (packet->owns_data && packet->data) {
    // Return to appropriate pool or free
    if (packet->buffer_pool) {
      data_buffer_pool_free(packet->buffer_pool, packet->data, packet->data_len);
    } else {
      // This was allocated from global pool or malloc, use buffer_pool_free which handles both
      buffer_pool_free(packet->data, packet->data_len);
    }
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
      // Use buffer_pool_free for global pool allocations, data_buffer_pool_free for local pools
      if (node->packet.buffer_pool) {
        data_buffer_pool_free(node->packet.buffer_pool, node->packet.data, node->packet.data_len);
      } else {
        // This was allocated from global pool or malloc, use buffer_pool_free which handles both
        buffer_pool_free(node->packet.data, node->packet.data_len);
      }
    }
    node_pool_put(queue->node_pool, node);
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

bool packet_queue_validate_packet(const queued_packet_t *packet) {
  if (!packet) {
    return false;
  }

  // Check magic number
  uint32_t magic = ntohl(packet->header.magic);
  if (magic != PACKET_MAGIC) {
    log_error("Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
    return false;
  }

  // Check packet type is valid
  uint16_t type = ntohs(packet->header.type);
  if (type < PACKET_TYPE_ASCII_FRAME || type > PACKET_TYPE_AUDIO_BATCH) {
    log_error("Invalid packet type: %u", type);
    return false;
  }

  // Check length matches data_len
  uint32_t length = ntohl(packet->header.length);
  if (length != packet->data_len) {
    log_error("Packet length mismatch: header says %u, data_len is %zu", length, packet->data_len);
    return false;
  }

  // Check CRC if there's data
  if (packet->data_len > 0 && packet->data) {
    uint32_t expected_crc = ntohl(packet->header.crc32);
    uint32_t actual_crc = asciichat_crc32(packet->data, packet->data_len);
    if (actual_crc != expected_crc) {
      log_error("Packet CRC mismatch: got 0x%x, expected 0x%x", actual_crc, expected_crc);
      return false;
    }
  }

  return true;
}