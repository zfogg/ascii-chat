/**
 * @file network/websocket/internal.h
 * @brief Internal WebSocket implementation types shared between transport.c and server.c
 * @ingroup network
 */

#ifndef ASCIICHAT_NETWORK_WEBSOCKET_INTERNAL_H
#define ASCIICHAT_NETWORK_WEBSOCKET_INTERNAL_H

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/ringbuffer.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief WebSocket message element (individual frame or fragment)
 *
 * Used for both send and receive queues. Each frame may be a complete message
 * or a fragment of a larger fragmented message. The receiver checks first/final
 * flags to reassemble if needed. Used in both send_queue and recv_queue.
 */
typedef struct {
  uint8_t *data; ///< Frame data (allocated, caller must free)
  size_t len;    ///< Frame length in bytes
  uint8_t first; ///< 1 if first frame of message (or complete message)
  uint8_t final; ///< 1 if final frame of message (or complete message)
} websocket_msg_t;

// Alias for backward compatibility
typedef websocket_msg_t websocket_recv_msg_t;

/**
 * @brief Pending buffer free item for deferred cleanup
 * Used to defer buffer freeing when permessage-deflate compression
 * may still hold references asynchronously
 */
typedef struct {
  uint8_t *ptr;    ///< Pointer to buffer to free
  size_t size;     ///< Size of buffer
} pending_free_item_t;

/**
 * @brief WebSocket transport implementation data
 */
typedef struct {
  struct lws *wsi;             ///< libwebsockets instance (owned)
  struct lws_context *context; ///< libwebsockets context (may be owned or borrowed)
  bool owns_context;           ///< True if transport owns context (client), false if borrowed (server)
  ringbuffer_t *recv_queue;    ///< Receive message queue
  ringbuffer_t *send_queue;    ///< Send message queue (for server-side transports)
  mutex_t recv_mutex;          ///< Protect recv_queue operations only
  cond_t recv_cond;            ///< Signal when recv_queue messages arrive
  mutex_t send_mutex;          ///< Protect send_queue operations only
  bool is_connected;           ///< Connection state
  mutex_t state_mutex;         ///< Protect state changes
  cond_t state_cond;           ///< Signal when is_connected changes (for connection wait)
  uint8_t *send_buffer;        ///< Send buffer with LWS_PRE padding
  size_t send_buffer_capacity; ///< Current send buffer capacity

  // Service thread for client-side transports
  asciichat_thread_t service_thread; ///< Thread that services libwebsockets context
  volatile bool service_running;     ///< Service thread running flag

  // Partial message reassembly state (preserved across recv() calls)
  // Fixes issue where slow fragment delivery caused reassembly timeouts
  // and orphaned fragments in the queue
  uint8_t *partial_buffer;           ///< Partial message buffer being assembled
  size_t partial_size;               ///< Current size of partial buffer
  size_t partial_capacity;           ///< Capacity of partial buffer
  uint64_t reassembly_start_ns;      ///< Start time of current reassembly (for timeout detection)
  int fragment_count;                ///< Fragment count for current reassembly
  bool reassembling;                 ///< True if currently assembling a message

  // Deferred buffer freeing for compression layer compatibility
  // permessage-deflate holds buffer references asynchronously after lws_write()
  // We defer freeing to prevent use-after-free in the compression layer
  ringbuffer_t *pending_free_queue;  ///< Queue of buffers pending delayed free
  mutex_t pending_free_mutex;        ///< Protect pending_free_queue operations

} websocket_transport_data_t;

#endif // ASCIICHAT_NETWORK_WEBSOCKET_INTERNAL_H
