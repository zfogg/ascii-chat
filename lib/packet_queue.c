/**
 * @file packet_queue.c
 * @ingroup packet_queue
 * @brief 📬 Lock-free packet queue with per-client isolation and memory pooling
 */

#include "packet_queue.h"
#include "buffer_pool.h"
#include "common.h"
#include "asciichat_errno.h"
#include "crc32.h"
#include <stdatomic.h>
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

  // Link all nodes into free list
  for (size_t i = 0; i < pool_size - 1; i++) {
    pool->nodes[i].next = &pool->nodes[i + 1];
  }
  pool->nodes[pool_size - 1].next = NULL;

  pool->free_list = &pool->nodes[0];
  pool->pool_size = pool_size;
  pool->used_count = 0;

  mutex_init(&pool->pool_mutex);

  return pool;
}

void node_pool_destroy(node_pool_t *pool) {
  if (!pool) {
    return;
  }

  mutex_destroy(&pool->pool_mutex);
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

  mutex_lock(&pool->pool_mutex);

  packet_node_t *node = pool->free_list;
  if (node) {
    pool->free_list = node->next;
    pool->used_count++;
    node->next = NULL; // Clear next pointer
  }

  mutex_unlock(&pool->pool_mutex);

  if (!node) {
    // Pool exhausted, fallback to malloc
    node = SAFE_MALLOC(sizeof(packet_node_t), packet_node_t *);
    log_debug("Memory pool exhausted, falling back to SAFE_MALLOC(used: %zu/%zu, void *)", pool->used_count,
              pool->pool_size);
  }

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
    mutex_lock(&pool->pool_mutex);

    // Return to free list
    node->next = pool->free_list;
    pool->free_list = node;
    pool->used_count--;

    mutex_unlock(&pool->pool_mutex);
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
  atomic_store_explicit(&queue->head, (packet_node_t *)NULL, memory_order_relaxed);
  atomic_store_explicit(&queue->tail, (packet_node_t *)NULL, memory_order_relaxed);
  atomic_init(&queue->count, (size_t)0);
  queue->max_size = max_size;
  atomic_init(&queue->bytes_queued, (size_t)0);

  // Create memory pools if requested
  queue->node_pool = node_pool_size > 0 ? node_pool_create(node_pool_size) : NULL;
  queue->buffer_pool = use_buffer_pool ? data_buffer_pool_create() : NULL;

  // Initialize atomic statistics
  atomic_init(&queue->packets_enqueued, (uint64_t)0);
  atomic_init(&queue->packets_dequeued, (uint64_t)0);
  atomic_init(&queue->packets_dropped, (uint64_t)0);
  atomic_init(&queue->shutdown, false);

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
               (double)hits * 100.0 / (double)(hits + misses), (unsigned long long)misses);
    }
  }

  // Destroy memory pools if present
  if (queue->node_pool) {
    node_pool_destroy(queue->node_pool);
  }
  if (queue->buffer_pool) {
    data_buffer_pool_destroy(queue->buffer_pool);
  }

  // No mutex/cond to destroy (lock-free design)

  SAFE_FREE(queue);
}

int packet_queue_enqueue(packet_queue_t *queue, packet_type_t type, const void *data, size_t data_len,
                         uint32_t client_id, bool copy_data) {
  if (!queue)
    return -1;

  // Check if shutdown (atomic read with acquire semantics)
  if (atomic_load_explicit(&queue->shutdown, memory_order_acquire)) {
    return -1;
  }

  // Check if queue is full and drop oldest packet if needed (lock-free)
  size_t current_count = atomic_load_explicit(&queue->count, memory_order_acquire);
  if (queue->max_size > 0 && current_count >= queue->max_size) {
    // Drop oldest packet (head) using atomic compare-and-swap
    packet_node_t *head = atomic_load_explicit(&queue->head, memory_order_acquire);
    if (head) {
      packet_node_t *next = head->next;
      // Atomically update head pointer
      if (atomic_compare_exchange_weak(&queue->head, &head, next)) {
        // Successfully claimed head node
        if (next == NULL) {
          // Queue became empty, also update tail
          atomic_store_explicit(&queue->tail, (packet_node_t *)NULL, memory_order_release);
        }

        // Update counters atomically
        size_t bytes = head->packet.data_len;
        atomic_fetch_sub(&queue->bytes_queued, bytes);
        atomic_fetch_sub(&queue->count, (size_t)1);
        atomic_fetch_add(&queue->packets_dropped, (uint64_t)1);

        // Free dropped packet data
        if (head->packet.owns_data && head->packet.data) {
          data_buffer_pool_free(head->packet.buffer_pool, head->packet.data, head->packet.data_len);
        }
        node_pool_put(queue->node_pool, head);

        log_debug_every(1000000, "Dropped packet from queue (full): type=%d, client=%u", type, client_id);
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
  node->packet.header.magic = htonl(PACKET_MAGIC);
  node->packet.header.type = htons((uint16_t)type);
  node->packet.header.length = htonl((uint32_t)data_len);
  node->packet.header.client_id = htonl(client_id);
  // Calculate CRC32 for the data (0 for empty packets)
  node->packet.header.crc32 = htonl(data_len > 0 ? asciichat_crc32(data, data_len) : 0);

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
  node->next = NULL;

  // Add to queue atomically (lock-free enqueue)
  packet_node_t *tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
  if (tail) {
    // Queue has existing nodes - append to tail
    tail->next = node; // Publish node before updating tail
    atomic_store_explicit(&queue->tail, node, memory_order_release);
  } else {
    // Empty queue - atomically set both head and tail
    packet_node_t *expected = NULL;
    if (atomic_compare_exchange_weak_explicit(&queue->head, &expected, node, memory_order_release, memory_order_relaxed)) {
      // Successfully set head (queue was empty)
      atomic_store_explicit(&queue->tail, node, memory_order_release);
    } else {
      // Another thread added a node - append to new tail
      packet_node_t *new_tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
      if (new_tail) {
        new_tail->next = node;
        atomic_store_explicit(&queue->tail, node, memory_order_release);
      } else {
        // Retry: tail was NULL but head was set (race condition)
        tail = atomic_load_explicit(&queue->head, memory_order_acquire);
        if (tail) {
          // Find actual tail
          while (tail->next != NULL) {
            tail = tail->next;
          }
          tail->next = node;
          atomic_store_explicit(&queue->tail, node, memory_order_release);
        } else {
          // Should not happen, but handle gracefully
          node_pool_put(queue->node_pool, node);
          return -1;
        }
      }
    }
  }

  // Update counters atomically
  atomic_fetch_add(&queue->count, (size_t)1);
  atomic_fetch_add(&queue->bytes_queued, data_len);
  atomic_fetch_add(&queue->packets_enqueued, (uint64_t)1);

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
  if (atomic_load_explicit(&queue->shutdown, memory_order_acquire)) {
    return -1;
  }

  // Check if queue is full and drop oldest packet if needed (lock-free)
  size_t current_count = atomic_load_explicit(&queue->count, memory_order_acquire);
  if (queue->max_size > 0 && current_count >= queue->max_size) {
    // Drop oldest packet (head) using atomic compare-and-swap
    packet_node_t *head = atomic_load_explicit(&queue->head, memory_order_acquire);
    if (head) {
      packet_node_t *next = head->next;
      // Atomically update head pointer
      if (atomic_compare_exchange_weak(&queue->head, &head, next)) {
        // Successfully claimed head node
        if (next == NULL) {
          // Queue became empty, also update tail
          atomic_store_explicit(&queue->tail, (packet_node_t *)NULL, memory_order_release);
        }

        // Update counters atomically
        size_t bytes = head->packet.data_len;
        atomic_fetch_sub(&queue->bytes_queued, bytes);
        atomic_fetch_sub(&queue->count, (size_t)1);
        atomic_fetch_add(&queue->packets_dropped, (uint64_t)1);

        // Free dropped packet data
        if (head->packet.owns_data && head->packet.data) {
          data_buffer_pool_free(head->packet.buffer_pool, head->packet.data, head->packet.data_len);
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
      data_copy = data_buffer_pool_alloc(queue->buffer_pool, packet->data_len);
      node->packet.buffer_pool = queue->buffer_pool;
    } else {
      // Use global pool if no local pool
      data_copy = buffer_pool_alloc(packet->data_len);
      node->packet.buffer_pool = data_buffer_pool_get_global();
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

  node->next = NULL;

  // Add to queue atomically (lock-free enqueue - same logic as enqueue)
  packet_node_t *tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
  if (tail) {
    // Queue has existing nodes - append to tail
    tail->next = node; // Publish node before updating tail
    atomic_store_explicit(&queue->tail, node, memory_order_release);
  } else {
    // Empty queue - atomically set both head and tail
    packet_node_t *expected = NULL;
    if (atomic_compare_exchange_weak_explicit(&queue->head, &expected, node, memory_order_release, memory_order_relaxed)) {
      // Successfully set head (queue was empty)
      atomic_store_explicit(&queue->tail, node, memory_order_release);
    } else {
      // Another thread added a node - append to new tail
      packet_node_t *new_tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
      if (new_tail) {
        new_tail->next = node;
        atomic_store_explicit(&queue->tail, node, memory_order_release);
      } else {
        // Retry: tail was NULL but head was set (race condition)
        tail = atomic_load_explicit(&queue->head, memory_order_acquire);
        if (tail) {
          // Find actual tail
          while (tail->next != NULL) {
            tail = tail->next;
          }
          tail->next = node;
          atomic_store_explicit(&queue->tail, node, memory_order_release);
        } else {
          // Should not happen, but handle gracefully
          node_pool_put(queue->node_pool, node);
          return -1;
        }
      }
    }
  }

  // Update counters atomically
  atomic_fetch_add(&queue->count, (size_t)1);
  atomic_fetch_add(&queue->bytes_queued, packet->data_len);
  atomic_fetch_add(&queue->packets_enqueued, (uint64_t)1);

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
  if (atomic_load_explicit(&queue->shutdown, memory_order_acquire)) {
    return NULL;
  }

  // Check if queue is empty (atomic read with acquire semantics)
  size_t current_count = atomic_load_explicit(&queue->count, memory_order_acquire);
  if (current_count == 0) {
    return NULL;
  }

  // Remove from head atomically (lock-free dequeue)
  packet_node_t *head = atomic_load_explicit(&queue->head, memory_order_acquire);
  if (!head) {
    return NULL;
  }

  // Atomically update head pointer
  packet_node_t *next = head->next;
  if (atomic_compare_exchange_weak(&queue->head, &head, next)) {
    // Successfully claimed head node
    if (next == NULL) {
      // Queue became empty, also update tail atomically
      atomic_store_explicit(&queue->tail, (packet_node_t *)NULL, memory_order_release);
    }

    // Update counters atomically
    size_t bytes = head->packet.data_len;
    atomic_fetch_sub(&queue->bytes_queued, bytes);
    atomic_fetch_sub(&queue->count, (size_t)1);
    atomic_fetch_add(&queue->packets_dequeued, (uint64_t)1);

    // Verify packet magic number for corruption detection
    uint32_t magic = ntohl(head->packet.header.magic);
    if (magic != PACKET_MAGIC) {
      SET_ERRNO(ERROR_BUFFER, "CORRUPTION: Invalid magic in try_dequeued packet: 0x%x (expected 0x%x), type=%u", magic,
                PACKET_MAGIC, ntohs(head->packet.header.type));
      // Still return node to pool but don't return corrupted packet
      node_pool_put(queue->node_pool, head);
      return NULL;
    }

    // Validate CRC if there's data
    if (head->packet.data_len > 0 && head->packet.data) {
      uint32_t expected_crc = ntohl(head->packet.header.crc32);
      uint32_t actual_crc = asciichat_crc32(head->packet.data, head->packet.data_len);
      if (actual_crc != expected_crc) {
        SET_ERRNO(ERROR_BUFFER,
                  "CORRUPTION: CRC mismatch in try_dequeued packet: got 0x%x, expected 0x%x, type=%u, len=%zu",
                  actual_crc, expected_crc, ntohs(head->packet.header.type), head->packet.data_len);
        // Free data if packet owns it
        if (head->packet.owns_data && head->packet.data) {
          // Use buffer_pool_free for global pool allocations, data_buffer_pool_free for local pools
          if (head->packet.buffer_pool) {
            data_buffer_pool_free(head->packet.buffer_pool, head->packet.data, head->packet.data_len);
          } else {
            // This was allocated from global pool or malloc, use buffer_pool_free which handles both
            buffer_pool_free(head->packet.data, head->packet.data_len);
          }
          // CRITICAL: Clear pointer to prevent double-free when packet is copied later
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
  if (packet->header.magic != htonl(PACKET_MAGIC)) {
    log_warn("Attempted double-free of packet (magic=0x%x, expected=0x%x)", ntohl(packet->header.magic), PACKET_MAGIC);
    return;
  }

  if (packet->owns_data && packet->data) {
    // Return to appropriate pool or free
    if (packet->buffer_pool) {
      data_buffer_pool_free(packet->buffer_pool, packet->data, packet->data_len);
    } else {
      // This was allocated from global pool or malloc, use buffer_pool_free which handles both
      buffer_pool_free(packet->data, packet->data_len);
    }
  }

  // Mark as freed to detect future double-free attempts
  packet->header.magic = 0xDEADBEEF; // Use different magic to indicate freed packet
  SAFE_FREE(packet);
}

size_t packet_queue_size(packet_queue_t *queue) {
  if (!queue)
    return 0;

  // Lock-free atomic read
  return atomic_load_explicit(&queue->count, memory_order_acquire);
}

bool packet_queue_is_empty(packet_queue_t *queue) {
  return packet_queue_size(queue) == 0;
}

bool packet_queue_is_full(packet_queue_t *queue) {
  if (!queue || queue->max_size == 0)
    return false;

  // Lock-free atomic read
  size_t count = atomic_load_explicit(&queue->count, memory_order_acquire);
  return (count >= queue->max_size);
}

void packet_queue_shutdown(packet_queue_t *queue) {
  if (!queue) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: queue=%p", queue);
    return;
  }

  // Lock-free atomic store (release semantics ensures visibility to other threads)
  atomic_store_explicit(&queue->shutdown, true, memory_order_release);
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
    *enqueued = atomic_load_explicit(&queue->packets_enqueued, memory_order_acquire);
  if (dequeued)
    *dequeued = atomic_load_explicit(&queue->packets_dequeued, memory_order_acquire);
  if (dropped)
    *dropped = atomic_load_explicit(&queue->packets_dropped, memory_order_acquire);
}

bool packet_queue_validate_packet(const queued_packet_t *packet) {
  if (!packet) {
    return false;
  }

  // Check magic number
  uint32_t magic = ntohl(packet->header.magic);
  if (magic != PACKET_MAGIC) {
    SET_ERRNO(ERROR_BUFFER, "Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
    return false;
  }

  // Check packet type is valid
  uint16_t type = ntohs(packet->header.type);
  if (type < PACKET_TYPE_ASCII_FRAME || type > PACKET_TYPE_AUDIO_BATCH) {
    SET_ERRNO(ERROR_BUFFER, "Invalid packet type: %u", type);
    return false;
  }

  // Check length matches data_len
  uint32_t length = ntohl(packet->header.length);
  if (length != packet->data_len) {
    SET_ERRNO(ERROR_BUFFER, "Packet length mismatch: header says %u, data_len is %zu", length, packet->data_len);
    return false;
  }

  // Check CRC if there's data
  if (packet->data_len > 0 && packet->data) {
    uint32_t expected_crc = ntohl(packet->header.crc32);
    uint32_t actual_crc = asciichat_crc32(packet->data, packet->data_len);
    if (actual_crc != expected_crc) {
      SET_ERRNO(ERROR_BUFFER, "Packet CRC mismatch: got 0x%x, expected 0x%x", actual_crc, expected_crc);
      return false;
    }
  }

  return true;
}
