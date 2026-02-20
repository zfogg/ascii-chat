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
 * @brief Receive queue element (individual WebSocket frame or fragment)
 *
 * Each RECEIVE callback queues a frame, which may be a complete message or
 * a fragment of a larger fragmented message. The receiver checks first/final
 * flags to reassemble if needed.
 */
typedef struct {
  uint8_t *data; ///< Frame data (allocated, caller must free)
  size_t len;    ///< Frame length in bytes
  uint8_t first; ///< 1 if first frame of message (or complete message)
  uint8_t final; ///< 1 if final frame of message (or complete message)
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

} websocket_transport_data_t;

#endif // ASCIICHAT_NETWORK_WEBSOCKET_INTERNAL_H
