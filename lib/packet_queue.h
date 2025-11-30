/**
 * @file packet_queue.h
 * @ingroup packet_queue
 * @brief 📬 Thread-safe packet queue system for per-client send threads
 *
 * This module implements a high-performance thread-safe queue for network packets,
 * enabling producer threads (audio mixer, video broadcast) to enqueue packets while
 * consumer threads (per-client send threads) dequeue and transmit them. This design
 * eliminates shared bottlenecks and enables linear scaling across multiple clients.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Thread-safe packet queuing for producer-consumer architecture
 * 2. Per-client queue isolation (each client has separate audio/video queues)
 * 3. Memory-efficient packet node pooling to reduce malloc/free overhead
 * 4. Queue size limits with automatic packet dropping under load
 * 5. Graceful shutdown handling for clean thread termination
 * 6. Comprehensive statistics collection for performance monitoring
 *
 * ARCHITECTURAL OVERVIEW:
 * =======================
 *
 * PRODUCER-CONSUMER MODEL:
 * - Producer threads (audio mixer, video render): Enqueue packets asynchronously
 * - Consumer threads (per-client send threads): Dequeue packets and transmit
 * - Lock-free operations with atomic retry patterns (no busy-waiting with backoff)
 *
 * QUEUE DESIGN:
 * - Linked-list implementation with head/tail pointers for O(1) enqueue/dequeue
 * - Optional node pool reduces malloc/free overhead for high-frequency operations
 * - Optional buffer pool integration for zero-allocation packet data management
 * - Size limits prevent unbounded memory growth under load
 *
 * MEMORY MANAGEMENT:
 * - Node pool pre-allocates queue nodes to eliminate per-packet allocations
 * - Buffer pool integration allows zero-allocation packet data (when available)
 * - Automatic cleanup when queue is destroyed
 * - Statistics track memory efficiency
 *
 * THREAD SAFETY:
 * ==============
 *
 * SYNCHRONIZATION PRIMITIVES:
 * - Atomic operations for lock-free queue state (head, tail, count, etc.)
 * - Atomic statistics fields for performance monitoring
 * - Atomic shutdown flag for graceful termination
 * - No mutexes or condition variables (lock-free design)
 *
 * LOCK-FREE PATTERN:
 * - All queue operations use atomic operations for thread-safe access
 * - Memory ordering (acquire/release) ensures proper synchronization
 * - Non-blocking dequeue operations (caller should retry if needed)
 * - Lock-free operations eliminate lock contention bottlenecks
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - buffer_pool.h: Optional integration for zero-allocation packet data
 * - network/packet.h: Uses packet_header_t and packet_type_t
 * - Per-client send threads: Consume packets from queues
 * - Audio mixer / Video render: Produce packets into queues
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 * - O(1) enqueue/dequeue operations (amortized)
 * - Zero-allocation operation when pools are configured
 * - Minimal lock contention due to per-client queue isolation
 * - Automatic backpressure via queue size limits
 *
 * @note Each client should have separate queues for audio and video to enable
 *       prioritization and prevent one stream from blocking the other.
 *
 * @note When max_size > 0, full queues drop oldest packets automatically.
 *       This prevents memory exhaustion but may cause frame drops under load.
 *
 * @note Queue shutdown causes dequeue operations to return NULL, allowing
 *       consumer threads to exit gracefully without blocking forever.
 *
 * @warning Always free dequeued packets with packet_queue_free_packet() to
 *          properly release memory (either to pool or free()).
 *
 * @warning Queue destruction does NOT free packets still in the queue.
 *          Clear the queue first or ensure all packets are dequeued.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "buffer_pool.h"
#include "network/packet_types.h"

/**
 * @brief Single packet ready to send (header already in network byte order)
 *
 * Represents a complete packet with header and optional payload data.
 * The packet header should already be in network byte order before enqueueing.
 *
 * @note If owns_data is true, the packet queue will free the data when the
 *       packet is freed (either via packet_queue_free_packet() or queue destruction).
 *
 * @note buffer_pool tracks which pool allocated the data (if any) for proper
 *       return to the pool. NULL means data was allocated with malloc().
 *
 * @ingroup packet_queue
 */
typedef struct {
  /** @brief Complete packet header (already in network byte order) */
  packet_header_t header;
  /** @brief Packet payload data (can be NULL for header-only packets) */
  void *data;
  /** @brief Length of payload data in bytes */
  size_t data_len;
  /** @brief If true, free data when packet is freed */
  bool owns_data;
  /** @brief Pool that allocated the data (NULL if malloc'd) */
  data_buffer_pool_t *buffer_pool;
} queued_packet_t;

/**
 * @brief Forward declaration for packet queue node
 * @ingroup packet_queue
 */
typedef struct packet_node packet_node_t;

/**
 * @brief Node in the packet queue linked list
 *
 * Linked list node containing a queued packet and pointer to next node.
 * Nodes are allocated from a pool to reduce malloc/free overhead.
 *
 * @ingroup packet_queue
 */
struct packet_node {
  /** @brief The queued packet data */
  queued_packet_t packet;
  /** @brief Pointer to next node in linked list (NULL for tail) */
  packet_node_t *next;
};

/**
 * @brief Memory pool for packet nodes to reduce malloc/free overhead
 *
 * Pre-allocates a fixed-size array of packet nodes and maintains a free list.
 * This eliminates per-packet malloc/free calls, significantly improving
 * performance for high-frequency packet queue operations.
 *
 * @note Pool operations are thread-safe via pool_mutex.
 *
 * @note If pool is exhausted, operations fall back to malloc/free.
 *
 * @ingroup packet_queue
 */
typedef struct node_pool {
  /** @brief Stack of free nodes (LIFO for cache locality) */
  packet_node_t *free_list;
  /** @brief Pre-allocated array of all nodes */
  packet_node_t *nodes;
  /** @brief Total number of nodes in pool */
  size_t pool_size;
  /** @brief Number of nodes currently in use */
  size_t used_count;
  /** @brief Mutex protecting free list access */
  mutex_t pool_mutex;
} node_pool_t;

/**
 * @brief Thread-safe packet queue for producer-consumer communication
 *
 * Implements a FIFO queue with blocking operations for efficient thread
 * coordination. Producers (audio mixer, video render) enqueue packets
 * asynchronously, while consumers (per-client send threads) dequeue packets
 * for transmission.
 *
 * QUEUE OPERATIONS:
 * - Enqueue: Add packet to tail of queue (non-blocking, drops oldest if full)
 * - Dequeue: Remove packet from head of queue (non-blocking, returns NULL if empty)
 * - Try-dequeue: Non-blocking dequeue (returns NULL if empty) - same as dequeue
 *
 * MEMORY POOLS:
 * - node_pool: Optional pool for queue nodes (reduces malloc overhead)
 * - buffer_pool: Optional pool for packet data (enables zero-allocation)
 *
 * SYNCHRONIZATION:
 * - Atomic head/tail pointers: Lock-free queue state management
 * - Atomic count: Fast size checks without locking
 * - Atomic statistics: Lock-free performance monitoring
 * - Atomic shutdown flag: Graceful shutdown without blocking
 *
 * @note When max_size is 0, queue is unlimited (may grow without bound).
 *       When max_size > 0, full queues drop oldest packets automatically.
 *
 * @note Statistics track enqueue/dequeue/drop counts for performance monitoring.
 *
 * @warning Queue destruction does NOT free packets still in queue. Clear queue
 *          first or ensure all packets are dequeued before destruction.
 *
 * @ingroup packet_queue
 */
typedef struct {
  /** @brief Front of queue (dequeue from here) - atomic for lock-free access */
  _Atomic(packet_node_t *) head;
  /** @brief Back of queue (enqueue here) - atomic for lock-free access */
  _Atomic(packet_node_t *) tail;
  /** @brief Number of packets currently in queue - atomic for lock-free access */
  _Atomic size_t count;
  /** @brief Maximum queue size (0 = unlimited) */
  size_t max_size;
  /** @brief Total bytes of data queued (for monitoring) - atomic for lock-free access */
  _Atomic size_t bytes_queued;

  /** @brief Optional memory pool for nodes (NULL = use malloc/free) */
  node_pool_t *node_pool;
  /** @brief Optional memory pool for data buffers (NULL = use malloc/free) */
  data_buffer_pool_t *buffer_pool;

  /** @brief Total packets enqueued (statistics) - atomic for lock-free access */
  _Atomic uint64_t packets_enqueued;
  /** @brief Total packets dequeued (statistics) - atomic for lock-free access */
  _Atomic uint64_t packets_dequeued;
  /** @brief Total packets dropped due to queue full (statistics) - atomic for lock-free access */
  _Atomic uint64_t packets_dropped;

  /** @brief Shutdown flag (true = dequeue returns NULL) - atomic for lock-free access */
  _Atomic bool shutdown;
} packet_queue_t;

/**
 * @name Node Pool Functions
 * @{
 * @ingroup packet_queue
 */

/**
 * @brief Create a memory pool for packet queue nodes
 * @param pool_size Number of nodes to pre-allocate
 * @return Pointer to node pool, or NULL on failure
 *
 * Pre-allocates a fixed-size array of packet nodes to eliminate per-packet
 * malloc/free overhead. All nodes are initialized and added to the free list.
 *
 * @note Pool size should be chosen based on expected queue depth. Too small
 *       causes fallback to malloc, too large wastes memory.
 *
 * @ingroup packet_queue
 */
node_pool_t *node_pool_create(size_t pool_size);

/**
 * @brief Destroy a node pool and free all memory
 * @param pool Node pool to destroy (can be NULL)
 *
 * Frees all pre-allocated nodes and the pool structure itself.
 *
 * @warning Pool must not be in use by any queues when destroyed.
 *
 * @ingroup packet_queue
 */
void node_pool_destroy(node_pool_t *pool);

/**
 * @brief Get a free node from the pool
 * @param pool Node pool
 * @return Pointer to free node, or NULL if pool is exhausted
 *
 * Removes a node from the free list. If pool is exhausted, returns NULL
 * (caller should fall back to malloc).
 *
 * @note Thread-safe (protected by pool_mutex).
 *
 * @ingroup packet_queue
 */
packet_node_t *node_pool_get(node_pool_t *pool);

/**
 * @brief Return a node to the pool
 * @param pool Node pool
 * @param node Node to return (must have been allocated from this pool)
 *
 * Returns a node to the free list for reuse. Node should not be in use
 * by any queue when returned.
 *
 * @note Thread-safe (protected by pool_mutex).
 *
 * @warning Do not return nodes that are still in use by queues.
 *
 * @ingroup packet_queue
 */
void node_pool_put(node_pool_t *pool, packet_node_t *node);

/** @} */

/**
 * @name Queue Management Functions
 * @{
 * @ingroup packet_queue
 */

/**
 * @brief Create a new packet queue
 * @param max_size Maximum queue size (0 = unlimited)
 * @return Pointer to new queue, or NULL on failure
 *
 * Creates a packet queue without memory pools. Queue will use malloc/free
 * for all node and data allocations.
 *
 * @note For high-performance scenarios, use packet_queue_create_with_pools()
 *       instead to enable zero-allocation operation.
 *
 * @ingroup packet_queue
 */
packet_queue_t *packet_queue_create(size_t max_size);

/**
 * @brief Create a packet queue with node pool
 * @param max_size Maximum queue size (0 = unlimited)
 * @param pool_size Number of nodes to pre-allocate in pool
 * @return Pointer to new queue, or NULL on failure
 *
 * Creates a packet queue with optional node pool to reduce malloc overhead.
 * Queue will still use malloc/free for packet data allocations.
 *
 * @note For zero-allocation operation, use packet_queue_create_with_pools()
 *       with use_buffer_pool = true.
 *
 * @ingroup packet_queue
 */
packet_queue_t *packet_queue_create_with_pool(size_t max_size, size_t pool_size);

/**
 * @brief Create a packet queue with both node and buffer pools
 * @param max_size Maximum queue size (0 = unlimited)
 * @param node_pool_size Number of nodes to pre-allocate
 * @param use_buffer_pool If true, use global buffer pool for packet data
 * @return Pointer to new queue, or NULL on failure
 *
 * Creates a high-performance packet queue with both node and buffer pools
 * enabled. This enables zero-allocation operation when pools are configured.
 *
 * @note When use_buffer_pool is true, queue uses global buffer pool singleton.
 *       Buffer pool must be initialized before queue creation.
 *
 * @ingroup packet_queue
 */
packet_queue_t *packet_queue_create_with_pools(size_t max_size, size_t node_pool_size, bool use_buffer_pool);

/**
 * @brief Destroy a packet queue and free all resources
 * @param queue Queue to destroy (can be NULL)
 *
 * Destroys the queue and all associated resources (node pool, etc.).
 *
 * @warning Packets still in the queue are NOT freed. Clear the queue first
 *          or ensure all packets are dequeued before destruction.
 *
 * @ingroup packet_queue
 */
void packet_queue_destroy(packet_queue_t *queue);

/** @} */

/**
 * @name Queue Operations
 * @{
 * @ingroup packet_queue
 */

/**
 * @brief Enqueue a packet into the queue
 * @param queue Packet queue
 * @param type Packet type (from packet_types.h)
 * @param data Packet payload data (can be NULL)
 * @param data_len Length of payload data in bytes
 * @param client_id Client ID for packet header
 * @param copy_data If true, copy data into pool/malloc buffer; if false, use data pointer directly
 * @return 0 on success, -1 on error
 *
 * Enqueues a new packet at the tail of the queue. If copy_data is true,
 * packet data is copied into a buffer (from pool if available, otherwise malloc).
 * If copy_data is false, the data pointer is used directly (caller must ensure
 * data remains valid until packet is dequeued and freed).
 *
 * @note If queue is full (max_size > 0 and count >= max_size), oldest packet
 *       is automatically dropped to make room (packet count does not exceed max_size).
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @warning When copy_data is false, caller must ensure data pointer remains
 *          valid until packet is freed.
 *
 * @ingroup packet_queue
 */
int packet_queue_enqueue(packet_queue_t *queue, packet_type_t type, const void *data, size_t data_len,
                         uint32_t client_id, bool copy_data);

/**
 * @brief Enqueue a pre-built packet (for special cases like compressed frames)
 * @param queue Packet queue
 * @param packet Pre-built packet structure
 * @return 0 on success, -1 on error
 *
 * Enqueues a packet that has already been built (header, data, etc.).
 * Useful for special cases like compressed frames where packet construction
 * is done outside the normal enqueue path.
 *
 * @note Packet is copied into the queue, but data ownership semantics
 *       follow the packet's owns_data flag.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @ingroup packet_queue
 */
int packet_queue_enqueue_packet(packet_queue_t *queue, const queued_packet_t *packet);

/**
 * @brief Dequeue a packet from the queue (non-blocking)
 * @param queue Packet queue
 * @return Pointer to dequeued packet, or NULL if queue is empty or shutdown
 *
 * Removes and returns the packet at the head of the queue. Returns NULL immediately
 * if queue is empty or shutdown (non-blocking operation).
 *
 * @note Caller must free the returned packet with packet_queue_free_packet()
 *       to properly release memory (either to pool or free()).
 *
 * @note Returns NULL when queue is empty or shutdown (allows consumer threads to exit).
 *       Caller should retry periodically if queue is expected to have data.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @warning Always free dequeued packets with packet_queue_free_packet().
 *
 * @ingroup packet_queue
 */
queued_packet_t *packet_queue_dequeue(packet_queue_t *queue);

/**
 * @brief Try to dequeue a packet without blocking
 * @param queue Packet queue
 * @return Pointer to dequeued packet, or NULL if queue is empty or shutdown
 *
 * Non-blocking version of packet_queue_dequeue(). Returns immediately with
 * NULL if queue is empty or shutdown, otherwise returns next packet.
 *
 * @note Caller must free the returned packet with packet_queue_free_packet().
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @ingroup packet_queue
 */
queued_packet_t *packet_queue_try_dequeue(packet_queue_t *queue);

/**
 * @brief Free a dequeued packet
 * @param packet Packet to free (can be NULL)
 *
 * Properly frees packet memory, returning data to buffer pool if applicable,
 * or calling free() if allocated with malloc. Also frees packet structure itself.
 *
 * @note Safe to call with NULL (no-op).
 *
 * @note Always call this for packets returned by dequeue functions.
 *
 * @ingroup packet_queue
 */
void packet_queue_free_packet(queued_packet_t *packet);

/** @} */

/**
 * @name Queue Status Functions
 * @{
 * @ingroup packet_queue
 */

/**
 * @brief Get current number of packets in queue
 * @param queue Packet queue
 * @return Number of packets currently queued
 *
 * Returns the current queue size (count field). This is a snapshot and may
 * change immediately after return due to concurrent enqueue/dequeue operations.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @ingroup packet_queue
 */
size_t packet_queue_size(packet_queue_t *queue);

/**
 * @brief Check if queue is empty
 * @param queue Packet queue
 * @return true if queue is empty, false otherwise
 *
 * Returns true if queue contains no packets. Snapshot value may change
 * immediately after return due to concurrent operations.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @ingroup packet_queue
 */
bool packet_queue_is_empty(packet_queue_t *queue);

/**
 * @brief Check if queue is full
 * @param queue Packet queue
 * @return true if queue is full (max_size > 0 and count >= max_size), false otherwise
 *
 * Returns true if queue has reached its maximum size. Unlimited queues
 * (max_size == 0) never return true from this function.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @ingroup packet_queue
 */
bool packet_queue_is_full(packet_queue_t *queue);

/** @} */

/**
 * @name Queue Control Functions
 * @{
 * @ingroup packet_queue
 */

/**
 * @brief Signal queue shutdown (causes dequeue to return NULL)
 * @param queue Packet queue
 *
 * Sets the shutdown flag, causing all dequeue operations to return NULL.
 * This allows consumer threads to exit gracefully without blocking forever.
 *
 * @note After shutdown, enqueue operations still succeed, but dequeue returns NULL.
 *
 * @note Thread-safe (wakes all waiting dequeue threads).
 *
 * @ingroup packet_queue
 */
void packet_queue_shutdown(packet_queue_t *queue);

/**
 * @brief Clear all packets from queue
 * @param queue Packet queue
 *
 * Removes and frees all packets currently in the queue. Useful for cleanup
 * before queue destruction or when resetting queue state.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @note This function frees all queued packets, so ensure nothing is
 *       still referencing them.
 *
 * @ingroup packet_queue
 */
void packet_queue_clear(packet_queue_t *queue);

/** @} */

/**
 * @name Statistics Functions
 * @{
 * @ingroup packet_queue
 */

/**
 * @brief Get queue statistics
 * @param queue Packet queue
 * @param enqueued Output: Total packets enqueued
 * @param dequeued Output: Total packets dequeued
 * @param dropped Output: Total packets dropped (due to queue full)
 *
 * Retrieves cumulative statistics for the queue. Useful for performance
 * monitoring and debugging.
 *
 * @note Thread-safe (lock-free using atomic operations).
 *
 * @note All output parameters must be non-NULL.
 *
 * @ingroup packet_queue
 */
void packet_queue_get_stats(packet_queue_t *queue, uint64_t *enqueued, uint64_t *dequeued, uint64_t *dropped);

/**
 * @brief Validate packet integrity
 * @param packet Packet to validate
 * @return true if packet is valid, false otherwise
 *
 * Validates packet structure and header integrity. Useful for debugging
 * and detecting memory corruption.
 *
 * @note This is a basic validation - does not verify payload contents.
 *
 * @ingroup packet_queue
 */
bool packet_queue_validate_packet(const queued_packet_t *packet);

/** @} */
