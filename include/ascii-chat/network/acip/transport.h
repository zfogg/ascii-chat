/**
 * @file network/acip/transport.h
 * @brief Transport abstraction layer for ACIP protocol
 *
 * Provides a transport-agnostic interface that allows ACIP protocol handlers
 * to work with any underlying transport (TCP, WebSocket, HTTP, etc.).
 *
 * DESIGN GOALS:
 * =============
 * 1. Protocol code never calls socket()/send()/recv() directly
 * 2. Same ACIP handlers work with TCP, WebSocket, or any future transport
 * 3. Transport implementations handle their own connection state
 * 4. Clean separation between protocol logic and transport logic
 *
 * USAGE PATTERN:
 * ==============
 * ```c
 * // Create TCP transport
 * acip_transport_t *tcp = acip_tcp_transport_create(sockfd, crypto_ctx);
 *
 * // Use with ACIP protocol handlers
 * asciichat_error_t result = acip_send_ascii_frame(tcp, frame_data, frame_size);
 *
 * // Or WebSocket transport (future)
 * acip_transport_t *ws = acip_websocket_transport_create(ws_handle, crypto_ctx);
 * asciichat_error_t result = acip_send_ascii_frame(ws, frame_data, frame_size);
 *
 * // Cleanup
 * acip_transport_destroy(tcp);
 * ```
 *
 * MEMORY OWNERSHIP:
 * =================
 * - Transport owns its connection state
 * - send() does NOT take ownership of data (caller must keep it valid)
 * - recv() allocates buffer, caller must free via out_allocated_buffer
 * - destroy() cleans up all transport resources
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/platform/socket.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Transport type enumeration
 *
 * Identifies which transport implementation is being used.
 * Useful for debugging and conditional logic.
 */
typedef enum {
  ACIP_TRANSPORT_TCP = 1,       ///< Raw TCP socket
  ACIP_TRANSPORT_WEBSOCKET = 2, ///< WebSocket over TCP
  ACIP_TRANSPORT_WEBRTC = 3,    ///< WebRTC DataChannel (P2P)
  ACIP_TRANSPORT_HTTP = 4,      ///< HTTP long-polling (future)
  ACIP_TRANSPORT_QUIC = 5       ///< QUIC/UDP (future)
} acip_transport_type_t;

/**
 * @brief Forward declaration of transport implementation
 *
 * Opaque handle to transport-specific state.
 * Each transport implementation defines its own state structure.
 */
typedef struct acip_transport acip_transport_t;

/**
 * @brief Transport method table (virtual function table)
 *
 * Each transport implementation provides these methods.
 * ACIP protocol code calls these instead of socket functions.
 */
typedef struct {
  /**
   * @brief Send data through this transport
   *
   * @param transport Transport instance
   * @param data Data to send (NOT owned by transport)
   * @param len Data length in bytes
   * @return ASCIICHAT_OK on success, error code on failure
   *
   * @note Does NOT take ownership of data buffer
   * @note May block until send completes
   * @note For TCP: calls socket_send_all()
   * @note For WebSocket: wraps in WebSocket frame
   */
  asciichat_error_t (*send)(acip_transport_t *transport, const void *data, size_t len);

  /**
   * @brief Receive data from this transport
   *
   * @param transport Transport instance
   * @param buffer Output buffer (allocated by transport)
   * @param out_len Received data length
   * @param out_allocated_buffer Buffer to free (may differ from buffer)
   * @return ASCIICHAT_OK on success, error code on failure
   *
   * @note Allocates buffer, caller must free via out_allocated_buffer
   * @note May block until data arrives
   * @note For TCP: calls socket_recv_all()
   * @note For WebSocket: unwraps WebSocket frame
   */
  asciichat_error_t (*recv)(acip_transport_t *transport, void **buffer, size_t *out_len, void **out_allocated_buffer);

  /**
   * @brief Close this transport
   *
   * @param transport Transport instance
   * @return ASCIICHAT_OK on success, error code on failure
   *
   * @note Should be idempotent (safe to call multiple times)
   * @note Does NOT free the transport structure itself
   */
  asciichat_error_t (*close)(acip_transport_t *transport);

  /**
   * @brief Get transport type
   *
   * @param transport Transport instance
   * @return Transport type enum value
   */
  acip_transport_type_t (*get_type)(acip_transport_t *transport);

  /**
   * @brief Get underlying socket (if applicable)
   *
   * @param transport Transport instance
   * @return Socket descriptor or INVALID_SOCKET_VALUE
   *
   * @note Returns INVALID_SOCKET_VALUE for transports without sockets
   * @note Useful for select()/poll() integration
   */
  socket_t (*get_socket)(acip_transport_t *transport);

  /**
   * @brief Check if transport is connected
   *
   * @param transport Transport instance
   * @return true if connected, false otherwise
   */
  bool (*is_connected)(acip_transport_t *transport);

  /**
   * @brief Custom destroy implementation (optional)
   *
   * @param transport Transport instance
   *
   * @note Called by acip_transport_destroy() before freeing impl_data
   * @note Should free transport-specific resources (peer connections, etc.)
   * @note May be NULL if no custom cleanup needed
   */
  void (*destroy_impl)(acip_transport_t *transport);
} acip_transport_methods_t;

/**
 * @brief Transport instance structure
 *
 * Base structure for all transport implementations.
 * Specific transports extend this with their own state.
 */
struct acip_transport {
  const acip_transport_methods_t *methods; ///< Method table (virtual functions)
  crypto_context_t *crypto_ctx;            ///< Optional encryption context
  void *impl_data;                         ///< Transport-specific state
};

// =============================================================================
// Transport Lifecycle
// =============================================================================

/**
 * @brief Destroy transport and free all resources
 *
 * @param transport Transport to destroy (may be NULL)
 *
 * @note Calls close() first if still connected
 * @note Frees the transport structure itself
 * @note Safe to pass NULL
 */
void acip_transport_destroy(acip_transport_t *transport);

// =============================================================================
// Transport Operations (convenience wrappers)
// =============================================================================

/**
 * @brief Send data through transport
 *
 * Convenience wrapper around transport->methods->send().
 *
 * @param transport Transport instance
 * @param data Data to send
 * @param len Data length
 * @return ASCIICHAT_OK on success, error code on failure
 */
static inline asciichat_error_t acip_transport_send(acip_transport_t *transport, const void *data, size_t len) {
  if (!transport || !transport->methods || !transport->methods->send) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }
  return transport->methods->send(transport, data, len);
}

/**
 * @brief Receive data from transport
 *
 * Convenience wrapper around transport->methods->recv().
 *
 * @param transport Transport instance
 * @param buffer Output buffer (allocated by transport)
 * @param out_len Received data length
 * @param out_allocated_buffer Buffer to free
 * @return ASCIICHAT_OK on success, error code on failure
 */
static inline asciichat_error_t acip_transport_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                                    void **out_allocated_buffer) {
  if (!transport || !transport->methods || !transport->methods->recv) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }
  return transport->methods->recv(transport, buffer, out_len, out_allocated_buffer);
}

/**
 * @brief Close transport
 *
 * Convenience wrapper around transport->methods->close().
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
static inline asciichat_error_t acip_transport_close(acip_transport_t *transport) {
  if (!transport || !transport->methods || !transport->methods->close) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }
  return transport->methods->close(transport);
}

/**
 * @brief Get transport type
 *
 * @param transport Transport instance
 * @return Transport type or 0 on error
 */
static inline acip_transport_type_t acip_transport_get_type(acip_transport_t *transport) {
  if (!transport || !transport->methods || !transport->methods->get_type) {
    return 0;
  }
  return transport->methods->get_type(transport);
}

/**
 * @brief Get underlying socket
 *
 * @param transport Transport instance
 * @return Socket descriptor or INVALID_SOCKET_VALUE
 */
static inline socket_t acip_transport_get_socket(acip_transport_t *transport) {
  if (!transport || !transport->methods || !transport->methods->get_socket) {
    return INVALID_SOCKET_VALUE;
  }
  return transport->methods->get_socket(transport);
}

/**
 * @brief Check if transport is connected
 *
 * @param transport Transport instance
 * @return true if connected, false otherwise
 */
static inline bool acip_transport_is_connected(acip_transport_t *transport) {
  if (!transport || !transport->methods || !transport->methods->is_connected) {
    return false;
  }
  return transport->methods->is_connected(transport);
}

// =============================================================================
// Transport Implementations (defined in separate files)
// =============================================================================

/**
 * @brief Create TCP transport from existing socket
 *
 * @param sockfd Connected socket descriptor
 * @param crypto_ctx Optional crypto context (may be NULL)
 * @return Transport instance or NULL on error
 *
 * @note Caller retains socket ownership (transport doesn't close on destroy)
 */
acip_transport_t *acip_tcp_transport_create(socket_t sockfd, crypto_context_t *crypto_ctx);

/**
 * @brief Create WebSocket transport from existing socket
 *
 * @param sockfd Connected socket descriptor (after WebSocket handshake)
 * @param crypto_ctx Optional crypto context (may be NULL)
 * @return Transport instance or NULL on error
 *
 * @note Socket must have completed WebSocket handshake
 * @note Caller retains socket ownership
 */
acip_transport_t *acip_websocket_transport_create(socket_t sockfd, crypto_context_t *crypto_ctx);

/**
 * @brief Create WebRTC transport from peer connection and data channel
 *
 * @param peer_conn WebRTC peer connection handle
 * @param data_channel WebRTC data channel for ACIP packets
 * @param crypto_ctx Optional crypto context (may be NULL)
 * @return Transport instance or NULL on error
 *
 * @note Transport takes ownership of peer_conn and data_channel
 * @note destroy() will close the peer connection and data channel
 */
struct webrtc_peer_connection;
struct webrtc_data_channel;
acip_transport_t *acip_webrtc_transport_create(struct webrtc_peer_connection *peer_conn,
                                               struct webrtc_data_channel *data_channel, crypto_context_t *crypto_ctx);
