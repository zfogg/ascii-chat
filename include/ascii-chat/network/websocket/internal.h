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
 * @brief Receive queue element (variable-length message)
 */
typedef struct {
  uint8_t *data; ///< Message data (allocated, caller must free)
  size_t len;    ///< Message length in bytes
} websocket_recv_msg_t;

/**
 * @brief WebSocket transport implementation data
 */
typedef struct {
  struct lws *wsi;             ///< libwebsockets instance (owned)
  struct lws_context *context; ///< libwebsockets context (may be owned or borrowed)
  bool owns_context;           ///< True if transport owns context (client), false if borrowed (server)
  ringbuffer_t *recv_queue;    ///< Receive message queue
  ringbuffer_t *send_queue;    ///< Send message queue (for server-side transports)
  mutex_t queue_mutex;         ///< Protect queue operations
  cond_t queue_cond;           ///< Signal when messages arrive
  bool is_connected;           ///< Connection state
  mutex_t state_mutex;         ///< Protect state changes
  uint8_t *send_buffer;        ///< Send buffer with LWS_PRE padding
  size_t send_buffer_capacity; ///< Current send buffer capacity

  // Fragment assembly for large messages (client-side only)
  uint8_t *fragment_buffer; ///< Buffer for assembling fragmented messages
  size_t fragment_size;     ///< Current size of assembled fragments
  size_t fragment_capacity; ///< Allocated capacity of fragment buffer

  // Service thread for client-side transports
  asciichat_thread_t service_thread; ///< Thread that services libwebsockets context
  volatile bool service_running;     ///< Service thread running flag

  // Backpressure handling: store message when queue is full
  websocket_recv_msg_t pending_msg; ///< Message waiting to be queued due to full queue
  bool has_pending_msg;             ///< True if pending_msg contains valid data
} websocket_transport_data_t;

#endif // ASCIICHAT_NETWORK_WEBSOCKET_INTERNAL_H
