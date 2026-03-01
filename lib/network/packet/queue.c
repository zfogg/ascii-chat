/**
 * @file packet_queue.c
 * @ingroup packet_queue
 * @brief 📬 Lock-free packet queue with per-client isolation and memory pooling
 */

#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/network/crc32.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ============================================================================
 * Memory Pool Implementation
 * ============================================================================
 */

node_pool_t *node_pool_create(size_t pool_size) {
  if (pool_size == 0) {
    return NULL;
  }

  node_pool_t *pool;
  pool = SAFE_MALLOC(sizeof(node_pool_t), node_pool_t *);

  // Allocate all nodes at once
  pool->nodes = SAFE_MALLOC(sizeof(packet_node_t) * pool_size, packet_node_t *);

  // Link all nodes into free list (using atomic stores for consistency with atomic type)
  for (size_t i = 0; i < pool_size - 1; i++) {
    atomic_ptr_store(&pool->nodes[i].next, (void *)&pool->nodes[i + 1]);
  }
  atomic_ptr_store(&pool->nodes[pool_size - 1].next, NULL);

  // Initialize atomics for lock-free operations
  atomic_ptr_store(&pool->free_list, (void *)&pool->nodes[0]);
  pool->pool_size = pool_size;
  atomic_store_u64(&pool->used_count, 0);

  NAMED_REGISTER_NODE_POOL(pool, "node_pool");

  return pool;
}

void node_pool_destroy(node_pool_t *pool) {
  if (!pool) {
    return;
  }

  NAMED_UNREGISTER(pool);

  SAFE_FREE(pool->nodes);
  SAFE_FREE(pool);
}

packet_node_t *node_pool_get(node_pool_t *pool) {
  if (!pool) {
    // No pool, fallback to malloc
    packet_node_t *node;
    node = SAFE_MALLOC(sizeof(packet_node_t), packet_node_t *);
    return node;
  }

  // Lock-free pop from free_list stack using CAS (same pattern as buffer_pool)
  packet_node_t *node = (packet_node_t *)atomic_ptr_load(&pool->free_list);
  while (node) {
    packet_node_t *next = (packet_node_t *)atomic_ptr_load(&node->next);
    if (atomic_ptr_cas(&pool->free_list, &node, next)) {
      // Successfully popped - clear next pointer and increment used_count
      atomic_ptr_store(&node->next, (void *)(packet_node_t *)NULL);
      atomic_fetch_add_u64(&pool->used_count, 1);
      return node;
    }
    // CAS failed - reload and retry (another thread grabbed the node)
    node = (packet_node_t *)atomic_ptr_load(&pool->free_list);
  }

  // Pool exhausted, fallback to malloc
  node = SAFE_MALLOC(sizeof(packet_node_t), packet_node_t *);
  size_t used = atomic_load_u64(&pool->used_count);
  log_debug("Memory pool exhausted, falling back to SAFE_MALLOC (used: %zu/%zu)", used, pool->pool_size);

  return node;
}

void node_pool_put(node_pool_t *pool, packet_node_t *node) {
  if (!node) {
    return;
  }

  if (!pool) {
    // No pool, just free
    SAFE_FREE(node);
    return;
  }

  // Check if this node is from our pool
  bool is_pool_node = (node >= pool->nodes && node < pool->nodes + pool->pool_size);

  if (is_pool_node) {
    // Lock-free push to free_list stack using CAS
    packet_node_t *head = (packet_node_t *)atomic_ptr_load(&pool->free_list);
    do {
      atomic_ptr_store(&node->next, (void *)head);
    } while (!atomic_ptr_cas(&pool->free_list, &head, node));
    atomic_fetch_sub_u64(&pool->used_count, 1);
  } else {
    // This was malloc'd, so free it
    SAFE_FREE(node);
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
  queue = SAFE_MALLOC(sizeof(packet_queue_t), packet_queue_t *);

  // Initialize atomic fields
  // For atomic pointer types, use atomic_store with relaxed ordering for initialization
  atomic_ptr_store(&queue->head, (void *)(packet_node_t *)NULL);
  atomic_ptr_store(&queue->tail, (void *)(packet_node_t *)NULL);
  atomic_store_u64(&queue->count, 0);
  queue->max_size = max_size;
  atomic_store_u64(&queue->bytes_queued, 0);

  // Create memory pools if requested
  queue->node_pool = node_pool_size > 0 ? node_pool_create(node_pool_size) : NULL;
  queue->buffer_pool = use_buffer_pool ? buffer_pool_create(0, 0) : NULL;

  // Initialize atomic statistics
  atomic_store_u64(&queue->packets_enqueued, 0);
  atomic_store_u64(&queue->packets_dequeued, 0);
  atomic_store_u64(&queue->packets_dropped, 0);
  atomic_store_bool(&queue->shutdown, false);

  NAMED_REGISTER_PACKET_QUEUE(queue, "packet_queue");
  if (queue->node_pool)
    NAMED_REGISTER_NODE_POOL(queue->node_pool, "queue_node_pool");

  return queue;
}

void packet_queue_destroy(packet_queue_t *queue) {
  if (!queue)
    return;

  NAMED_UNREGISTER(queue);

  // Signal shutdown first
  packet_queue_stop(queue);

  // Clear any remaining packets
  packet_queue_clear(queue);

  // Destroy memory pools if present
  if (queue->node_pool) {
    node_pool_destroy(queue->node_pool);
  }
  if (queue->buffer_pool) {
    buffer_pool_log_stats(queue->buffer_pool, "packet_queue");
    buffer_pool_destroy(queue->buffer_pool);
  }

  // No mutex/cond to destroy (lock-free design)

  SAFE_FREE(queue);
}

int packet_queue_enqueue(packet_queue_t *queue, packet_type_t type, const void *data, size_t data_len,
                         uint32_t client_id, bool copy_data) {
  if (!queue)
    return -1;

  // Check if shutdown (atomic read with acquire semantics)
  if (atomic_load_bool(&queue->shutdown)) {
    return -1;
  }

  // Check if queue is full and drop oldest packet if needed (lock-free)
  size_t current_count = atomic_load_u64(&queue->count);
  if (queue->max_size > 0 && current_count >= queue->max_size) {
    // Drop oldest packet (head) using atomic compare-and-swap
    packet_node_t *head = (packet_node_t *)atomic_ptr_load(&queue->head);
    if (head) {
      packet_node_t *next = (packet_node_t *)atomic_ptr_load(&head->next);
      // Atomically update head pointer
      if (atomic_ptr_cas(&queue->head, &head, next)) {
        // Successfully claimed head node
        if (next == NULL) {
          // Queue became empty, also update tail
          atomic_ptr_store(&queue->tail, (void *)(packet_node_t *)NULL);
        }

        // Update counters atomically
        size_t bytes = head->packet.data_len;
        atomic_fetch_sub_u64(&queue->bytes_queued, bytes);
        atomic_fetch_sub_u64(&queue->count, 1);
        atomic_fetch_add_u64(&queue->packets_dropped, 1);

        // Free dropped packet data
        if (head->packet.owns_data && head->packet.data) {
          buffer_pool_free(head->packet.buffer_pool, head->packet.data, head->packet.data_len);
        }
        node_pool_put(queue->node_pool, head);

        log_dev_every(4500 * US_PER_MS_INT, "Dropped packet from queue (full): type=%d, client=%u", type, client_id);
      }
      // If CAS failed, another thread already dequeued - continue to enqueue
    }
  }

  // Create new node (use pool if available)
  packet_node_t *node = node_pool_get(queue->node_pool);
  if (!node) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet node");
    return -1;
  }

  // Build packet header
  node->packet.header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
  node->packet.header.type = HOST_TO_NET_U16((uint16_t)type);
  node->packet.header.length = HOST_TO_NET_U32((uint32_t)data_len);
  node->packet.header.client_id = HOST_TO_NET_U32(client_id);
  // Calculate CRC32 for the data (0 for empty packets)
  node->packet.header.crc32 = HOST_TO_NET_U32(data_len > 0 ? asciichat_crc32(data, data_len) : 0);

  // Handle data
  if (data_len > 0 && data) {
    if (copy_data) {
      // Try to allocate from buffer pool (local or global)
      if (queue->buffer_pool) {
        node->packet.data = buffer_pool_alloc(queue->buffer_pool, data_len);
        node->packet.buffer_pool = queue->buffer_pool;
      } else {
        // Use global pool if no local pool
        node->packet.data = buffer_pool_alloc(NULL, data_len);
        node->packet.buffer_pool = buffer_pool_get_global();
      }
      SAFE_MEMCPY(node->packet.data, data_len, data, data_len);
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
  atomic_ptr_store(&node->next, (void *)(packet_node_t *)NULL);

  // Add to queue using lock-free CAS-based enqueue (Michael-Scott algorithm)
  while (true) {
    packet_node_t *tail = (packet_node_t *)atomic_ptr_load(&queue->tail);

    if (tail == NULL) {
      // Empty queue - atomically set both head and tail
      packet_node_t *expected = NULL;
      if (atomic_ptr_cas(&queue->head, &expected, node)) {
        // Successfully set head (queue was empty)
        atomic_ptr_store(&queue->tail, (void *)node);
        break; // Enqueue successful
      }
      // CAS failed - another thread initialized queue, retry
      continue;
    }

    // Queue is non-empty - try to append to tail
    packet_node_t *next = (packet_node_t *)atomic_ptr_load(&tail->next);
    packet_node_t *current_tail = (packet_node_t *)atomic_ptr_load(&queue->tail);

    // Verify tail hasn't changed (ABA problem mitigation)
    if (tail != current_tail) {
      // Tail was updated by another thread, retry
      continue;
    }

    if (next == NULL) {
      // Tail is actually the last node - try to link new node
      packet_node_t *expected_null = NULL;
      if (atomic_ptr_cas(&tail->next, &expected_null, node)) {
        // Successfully linked node - try to swing tail forward (best-effort, ignore failure)
        atomic_ptr_cas(&queue->tail, &tail, node);
        break; // Enqueue successful
      }
      // CAS failed - another thread appended to tail, retry
    } else {
      // Tail is lagging behind - help advance it
      atomic_ptr_cas(&queue->tail, &tail, next);
      // Retry with new tail
    }
  }

  // Update counters atomically
  atomic_fetch_add_u64(&queue->count, (size_t)1);
  atomic_fetch_add_u64(&queue->bytes_queued, data_len);
  atomic_fetch_add_u64(&queue->packets_enqueued, (uint64_t)1);

  return 0;
}

int packet_queue_enqueue_packet(packet_queue_t *queue, const queued_packet_t *packet) {
  if (!queue || !packet) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: queue=%p, packet=%p", queue, packet);
    return -1;
  }

  // Validate packet before enqueueing
  if (!packet_queue_validate_packet(packet)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Refusing to enqueue invalid packet");
    return -1;
  }

  // Check if shutdown (atomic read with acquire semantics)
  if (atomic_load_bool(&queue->shutdown)) {
    return -1;
  }

  // Check if queue is full and drop oldest packet if needed (lock-free)
  size_t current_count = atomic_load_u64(&queue->count);
  if (queue->max_size > 0 && current_count >= queue->max_size) {
    // Drop oldest packet (head) using atomic compare-and-swap
    packet_node_t *head = (packet_node_t *)atomic_ptr_load(&queue->head);
    if (head) {
      packet_node_t *next = (packet_node_t *)atomic_ptr_load(&head->next);
      // Atomically update head pointer
      if (atomic_ptr_cas(&queue->head, &head, next)) {
        // Successfully claimed head node
        if (next == NULL) {
          // Queue became empty, also update tail
          atomic_ptr_store(&queue->tail, (void *)(packet_node_t *)NULL);
        }

        // Update counters atomically
        size_t bytes = head->packet.data_len;
        atomic_fetch_sub_u64(&queue->bytes_queued, bytes);
        atomic_fetch_sub_u64(&queue->count, 1);
        atomic_fetch_add_u64(&queue->packets_dropped, 1);

        // Free dropped packet data
        if (head->packet.owns_data && head->packet.data) {
          buffer_pool_free(head->packet.buffer_pool, head->packet.data, head->packet.data_len);
        }
        node_pool_put(queue->node_pool, head);
      }
      // If CAS failed, another thread already dequeued - continue to enqueue
    }
  }

  // Create new node (use pool if available)
  packet_node_t *node = node_pool_get(queue->node_pool);
  if (!node) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet node");
    return -1;
  }

  // Copy the packet header
  SAFE_MEMCPY(&node->packet, sizeof(queued_packet_t), packet, sizeof(queued_packet_t));

  // Deep copy the data if needed
  if (packet->data && packet->data_len > 0 && packet->owns_data) {
    // If the packet owns its data, we need to make a copy
    // Try to allocate from buffer pool (local or global)
    void *data_copy;
    if (queue->buffer_pool) {
      data_copy = buffer_pool_alloc(queue->buffer_pool, packet->data_len);
      node->packet.buffer_pool = queue->buffer_pool;
    } else {
      // Use global pool if no local pool
      data_copy = buffer_pool_alloc(NULL, packet->data_len);
      node->packet.buffer_pool = buffer_pool_get_global();
    }
    SAFE_MEMCPY(data_copy, packet->data_len, packet->data, packet->data_len);
    node->packet.data = data_copy;
    node->packet.owns_data = true;
  } else {
    // Either no data or packet doesn't own it (shared reference is OK)
    node->packet.data = packet->data;
    node->packet.owns_data = packet->owns_data;
    node->packet.buffer_pool = packet->buffer_pool; // Preserve original pool reference
  }

  atomic_ptr_store(&node->next, (void *)(packet_node_t *)NULL);

  // Add to queue using lock-free CAS-based enqueue (Michael-Scott algorithm)
  while (true) {
    packet_node_t *tail = (packet_node_t *)atomic_ptr_load(&queue->tail);

    if (tail == NULL) {
      // Empty queue - atomically set both head and tail
      packet_node_t *expected = NULL;
      if (atomic_ptr_cas(&queue->head, &expected, node)) {
        // Successfully set head (queue was empty)
        atomic_ptr_store(&queue->tail, (void *)node);
        break; // Enqueue successful
      }
      // CAS failed - another thread initialized queue, retry
      continue;
    }

    // Queue is non-empty - try to append to tail
    packet_node_t *next = (packet_node_t *)atomic_ptr_load(&tail->next);
    packet_node_t *current_tail = (packet_node_t *)atomic_ptr_load(&queue->tail);

    // Verify tail hasn't changed (ABA problem mitigation)
    if (tail != current_tail) {
      // Tail was updated by another thread, retry
      continue;
    }

    if (next == NULL) {
      // Tail is actually the last node - try to link new node
      packet_node_t *expected_null = NULL;
      if (atomic_ptr_cas(&tail->next, &expected_null, node)) {
        // Successfully linked node - try to swing tail forward (best-effort, ignore failure)
        atomic_ptr_cas(&queue->tail, &tail, node);
        break; // Enqueue successful
      }
      // CAS failed - another thread appended to tail, retry
    } else {
      // Tail is lagging behind - help advance it
      atomic_ptr_cas(&queue->tail, &tail, next);
      // Retry with new tail
    }
  }

  // Update counters atomically
  atomic_fetch_add_u64(&queue->count, (size_t)1);
  atomic_fetch_add_u64(&queue->bytes_queued, packet->data_len);
  atomic_fetch_add_u64(&queue->packets_enqueued, (uint64_t)1);

  return 0;
}

queued_packet_t *packet_queue_dequeue(packet_queue_t *queue) {
  // Non-blocking dequeue (same as try_dequeue for lock-free design)
  return packet_queue_try_dequeue(queue);
}

queued_packet_t *packet_queue_try_dequeue(packet_queue_t *queue) {
  if (!queue)
    return NULL;

  // Check if shutdown (atomic read with acquire semantics)
  if (atomic_load_bool(&queue->shutdown)) {
    return NULL;
  }

  // Check if queue is empty (atomic read with acquire semantics)
  size_t current_count = atomic_load_u64(&queue->count);
  if (current_count == 0) {
    return NULL;
  }

  // Remove from head atomically (lock-free dequeue)
  packet_node_t *head = (packet_node_t *)atomic_ptr_load(&queue->head);
  if (!head) {
    return NULL;
  }

  // Atomically update head pointer
  packet_node_t *next = (packet_node_t *)atomic_ptr_load(&head->next);
  if (atomic_ptr_cas(&queue->head, &head, next)) {
    // Successfully claimed head node
    if (next == NULL) {
      // Queue became empty, also update tail atomically
      atomic_ptr_store(&queue->tail, (void *)(packet_node_t *)NULL);
    }

    // Update counters atomically
    size_t bytes = head->packet.data_len;
    atomic_fetch_sub_u64(&queue->bytes_queued, bytes);
    atomic_fetch_sub_u64(&queue->count, 1);
    atomic_fetch_add_u64(&queue->packets_dequeued, (uint64_t)1);

    // Verify packet magic number for corruption detection
    uint64_t magic = NET_TO_HOST_U64(head->packet.header.magic);
    if (magic != PACKET_MAGIC) {
      SET_ERRNO(ERROR_BUFFER, "CORRUPTION: Invalid magic in try_dequeued packet: 0x%llx (expected 0x%llx), type=%u",
                magic, PACKET_MAGIC, NET_TO_HOST_U16(head->packet.header.type));
      // Still return node to pool but don't return corrupted packet
      node_pool_put(queue->node_pool, head);
      return NULL;
    }

    // Validate CRC if there's data
    if (head->packet.data_len > 0 && head->packet.data) {
      uint32_t expected_crc = NET_TO_HOST_U32(head->packet.header.crc32);
      uint32_t actual_crc = asciichat_crc32(head->packet.data, head->packet.data_len);
      if (actual_crc != expected_crc) {
        SET_ERRNO(ERROR_BUFFER,
                  "CORRUPTION: CRC mismatch in try_dequeued packet: got 0x%x, expected 0x%x, type=%u, len=%zu",
                  actual_crc, expected_crc, NET_TO_HOST_U16(head->packet.header.type), head->packet.data_len);
        // Free data if packet owns it
        if (head->packet.owns_data && head->packet.data) {
          // Use buffer_pool_free for global pool allocations, buffer_pool_free for local pools
          if (head->packet.buffer_pool) {
            buffer_pool_free(head->packet.buffer_pool, head->packet.data, head->packet.data_len);
          } else {
            // This was allocated from global pool or malloc, use buffer_pool_free which handles both
            buffer_pool_free(NULL, head->packet.data, head->packet.data_len);
          }
          // Clear pointer to prevent double-free when packet is copied later.
          head->packet.data = NULL;
          head->packet.owns_data = false;
        }
        node_pool_put(queue->node_pool, head);
        return NULL;
      }
    }

    // Extract packet and return node to pool
    queued_packet_t *packet;
    packet = SAFE_MALLOC(sizeof(queued_packet_t), queued_packet_t *);
    SAFE_MEMCPY(packet, sizeof(queued_packet_t), &head->packet, sizeof(queued_packet_t));
    node_pool_put(queue->node_pool, head);
    return packet;
  }

  // CAS failed - another thread dequeued, retry if needed (or return NULL for non-blocking)
  return NULL;
}

void packet_queue_free_packet(queued_packet_t *packet) {
  if (!packet)
    return;

  // Check if packet was already freed (detect double-free)
  if (packet->header.magic != HOST_TO_NET_U64(PACKET_MAGIC)) {
    log_warn("Attempted double-free of packet (magic=0x%llx, expected=0x%llx)", NET_TO_HOST_U64(packet->header.magic),
             PACKET_MAGIC);
    return;
  }

  if (packet->owns_data && packet->data) {
    // Return to appropriate pool or free
    if (packet->buffer_pool) {
      buffer_pool_free(packet->buffer_pool, packet->data, packet->data_len);
    } else {
      // This was allocated from global pool or malloc, use buffer_pool_free which handles both
      buffer_pool_free(NULL, packet->data, packet->data_len);
    }
  }

  // Mark as freed to detect future double-free attempts
  // Use network byte order for consistency on big-endian systems
  packet->header.magic = HOST_TO_NET_U64(0xBEEFDEADULL); // Different magic in network byte order
  SAFE_FREE(packet);
}

size_t packet_queue_size(packet_queue_t *queue) {
  if (!queue)
    return 0;

  // Lock-free atomic read
  return atomic_load_u64(&queue->count);
}

bool packet_queue_is_empty(packet_queue_t *queue) {
  return packet_queue_size(queue) == 0;
}

bool packet_queue_is_full(packet_queue_t *queue) {
  if (!queue || queue->max_size == 0)
    return false;

  // Lock-free atomic read
  size_t count = atomic_load_u64(&queue->count);
  return (count >= queue->max_size);
}

void packet_queue_stop(packet_queue_t *queue) {
  if (!queue) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: queue=%p", queue);
    return;
  }

  // Lock-free atomic store (release semantics ensures visibility to other threads)
  atomic_store_u64(&queue->shutdown, true);
}

void packet_queue_clear(packet_queue_t *queue) {
  if (!queue)
    return;

  // Lock-free clear: drain queue by repeatedly dequeuing until empty
  queued_packet_t *packet;
  while ((packet = packet_queue_try_dequeue(queue)) != NULL) {
    packet_queue_free_packet(packet);
  }
}

void packet_queue_get_stats(packet_queue_t *queue, uint64_t *enqueued, uint64_t *dequeued, uint64_t *dropped) {
  if (!queue)
    return;

  // Lock-free atomic reads (acquire semantics for consistency)
  if (enqueued)
    *enqueued = atomic_load_u64(&queue->packets_enqueued);
  if (dequeued)
    *dequeued = atomic_load_u64(&queue->packets_dequeued);
  if (dropped)
    *dropped = atomic_load_u64(&queue->packets_dropped);
}

bool packet_queue_validate_packet(const queued_packet_t *packet) {
  if (!packet) {
    return false;
  }

  // Check magic number
  uint64_t magic = NET_TO_HOST_U64(packet->header.magic);
  if (magic != PACKET_MAGIC) {
    SET_ERRNO(ERROR_BUFFER, "Invalid packet magic: 0x%llx (expected 0x%llx)", magic, PACKET_MAGIC);
    return false;
  }

  // Check packet type is valid (must be non-zero and within reasonable range)
  uint16_t type = NET_TO_HOST_U16(packet->header.type);
  if (type == 0 || type > 10000) {
    SET_ERRNO(ERROR_BUFFER, "Invalid packet type: %u", type);
    return false;
  }

  // Check length matches data_len
  uint32_t length = NET_TO_HOST_U32(packet->header.length);
  if (length != packet->data_len) {
    SET_ERRNO(ERROR_BUFFER, "Packet length mismatch: header says %u, data_len is %zu", length, packet->data_len);
    return false;
  }

  // Check CRC if there's data
  if (packet->data_len > 0 && packet->data) {
    uint32_t expected_crc = NET_TO_HOST_U32(packet->header.crc32);
    uint32_t actual_crc = asciichat_crc32(packet->data, packet->data_len);
    if (actual_crc != expected_crc) {
      SET_ERRNO(ERROR_BUFFER, "Packet CRC mismatch: got 0x%x, expected 0x%x", actual_crc, expected_crc);
      return false;
    }
  }

  return true;
}
