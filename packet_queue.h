#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "network.h"

/*
 * Packet Queue System for Per-Client Send Threads
 *
 * This implements a thread-safe queue for network packets, allowing
 * producer threads (audio mixer, video broadcast) to queue packets
 * and consumer threads (per-client send threads) to dequeue and send them.
 *
 * Each client has separate queues for audio and video to allow prioritization.
 */

// Forward declaration
typedef struct packet_node packet_node_t;

// A single packet ready to send (header already in network byte order)
typedef struct {
  packet_header_t header; // Complete packet header
  void *data;             // Packet payload (can be NULL)
  size_t data_len;        // Length of payload
  bool owns_data;         // If true, free data when packet is freed
} queued_packet_t;

// Node in the packet queue linked list
struct packet_node {
  queued_packet_t packet;
  packet_node_t *next;
};

// Memory pool for packet nodes to reduce malloc/free overhead
typedef struct node_pool {
  packet_node_t *free_list;   // Stack of free nodes
  packet_node_t *nodes;       // Pre-allocated array of nodes
  size_t pool_size;           // Total number of nodes in pool
  size_t used_count;          // Number of nodes currently in use
  pthread_mutex_t pool_mutex; // Protect free list access
} node_pool_t;

// Thread-safe packet queue
typedef struct {
  packet_node_t *head; // Front of queue (dequeue from here)
  packet_node_t *tail; // Back of queue (enqueue here)
  size_t count;        // Number of packets in queue
  size_t max_size;     // Maximum queue size (0 = unlimited)
  size_t bytes_queued; // Total bytes of data queued

  // Memory pool for nodes
  node_pool_t *node_pool; // Optional memory pool (NULL = use malloc/free)

  // Thread synchronization
  pthread_mutex_t mutex;
  pthread_cond_t not_empty; // Signaled when queue becomes non-empty
  pthread_cond_t not_full;  // Signaled when queue becomes not full

  // Statistics
  uint64_t packets_enqueued;
  uint64_t packets_dequeued;
  uint64_t packets_dropped; // Due to queue full

  bool shutdown; // Signal to shutdown queue
} packet_queue_t;

// Memory pool functions
node_pool_t *node_pool_create(size_t pool_size);
void node_pool_destroy(node_pool_t *pool);
packet_node_t *node_pool_get(node_pool_t *pool);
void node_pool_put(node_pool_t *pool, packet_node_t *node);

// Queue management functions
packet_queue_t *packet_queue_create(size_t max_size);
packet_queue_t *packet_queue_create_with_pool(size_t max_size, size_t pool_size);
void packet_queue_destroy(packet_queue_t *queue);

// Enqueue a packet (returns 0 on success, -1 on error)
// If queue is full and max_size > 0, oldest packet is dropped
int packet_queue_enqueue(packet_queue_t *queue, packet_type_t type, const void *data, size_t data_len,
                         uint32_t client_id, bool copy_data);

// Enqueue a pre-built packet (for special cases like compressed frames)
int packet_queue_enqueue_packet(packet_queue_t *queue, const queued_packet_t *packet);

// Dequeue a packet (blocks if queue is empty unless shutdown)
// Caller must free the returned packet with packet_queue_free_packet()
queued_packet_t *packet_queue_dequeue(packet_queue_t *queue);

// Try to dequeue without blocking (returns NULL if empty)
queued_packet_t *packet_queue_try_dequeue(packet_queue_t *queue);

// Free a dequeued packet
void packet_queue_free_packet(queued_packet_t *packet);

// Queue status
size_t packet_queue_size(packet_queue_t *queue);
bool packet_queue_is_empty(packet_queue_t *queue);
bool packet_queue_is_full(packet_queue_t *queue);

// Signal queue shutdown (causes dequeue to return NULL)
void packet_queue_shutdown(packet_queue_t *queue);

// Clear all packets from queue
void packet_queue_clear(packet_queue_t *queue);

// Get statistics
void packet_queue_get_stats(packet_queue_t *queue, uint64_t *enqueued, uint64_t *dequeued, uint64_t *dropped);

// Validate packet integrity (returns true if valid)
bool packet_queue_validate_packet(const queued_packet_t *packet);

#endif // PACKET_QUEUE_H