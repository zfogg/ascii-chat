#pragma once

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/connection_endpoint.h>

typedef struct network_connection network_connection_t;
typedef struct network_server network_server_t;

typedef enum {
  NETWORK_NEGOTIATING,
  NETWORK_READY,
  NETWORK_CLOSING,
  NETWORK_CLOSED,
} network_connection_state_t;

/** All hooks run serially for a connection, outside backend event loops.
 * Packet payloads are borrowed until return. closing stops application producers;
 * closed releases application state after the transport and callbacks have drained.
 * An open failure still receives closing/closed, including partial application init.
 */
typedef struct {
  asciichat_error_t (*open)(network_connection_t *, void *);
  asciichat_error_t (*packet)(network_connection_t *, packet_type_t, const void *, size_t, void *);
  asciichat_error_t (*closing)(network_connection_t *, void *);
  void (*closed)(network_connection_t *, void *);
} network_connection_callbacks_t;

typedef struct {
  int port;
  const char *ipv4_address, *ipv6_address;
  bool bind_ipv4, bind_ipv6;
  bool websocket_enabled;
  int websocket_port;
  const char *tls_cert_path, *tls_key_path;
  network_connection_callbacks_t callbacks;
  void *user_data;
  void (*tick)(void *);
  void *tick_context;
  size_t max_connections, max_payload;
  uint64_t handshake_timeout_ms, partial_timeout_ms, idle_timeout_ms;
} network_server_config_t;

asciichat_error_t network_server_create(const network_server_config_t *, network_server_t **);
asciichat_error_t network_server_start(network_server_t *);
asciichat_error_t network_server_run(network_server_t *);
/** Atomic-only request, including from a native signal handler. */
void network_server_request_stop(network_server_t *);
/** Call after run returns (join its owner when running on another thread).
 * Drains backend workers and connections, retaining dependencies on failure. */
asciichat_error_t network_server_stop(network_server_t *);
void network_server_destroy(network_server_t **);
size_t network_server_connection_count(network_server_t *);
bool network_server_websocket_enabled(network_server_t *);
const network_server_config_t *network_server_config(network_server_t *);

/** Adopt an authenticated peer transport on success; the caller retains it on failure. */
asciichat_error_t network_server_adopt(network_server_t *, acip_transport_t *, const char *peer, bool secure);

/** Iteration pins connections and application state, without holding registry
 * locks across callbacks or sends. The callback must not wait for server shutdown.
 */
typedef void (*network_connection_visit_fn)(network_connection_t *, void *);
void network_server_foreach(network_server_t *, network_connection_visit_fn, void *);

acip_transport_t *network_connection_transport(network_connection_t *);
void *network_connection_context(network_connection_t *);
void network_connection_set_context(network_connection_t *, void *);
const char *network_connection_peer(network_connection_t *);
bool network_connection_secure(network_connection_t *);
network_connection_state_t network_connection_state(network_connection_t *);
void network_connection_ready(network_connection_t *);
void network_connection_request_stop(network_connection_t *);

/** Security storage follows connection ownership, including a retained fallback. */
crypto_handshake_context_t *network_connection_handshake(network_connection_t *);
asciichat_error_t network_connection_prepare_security(network_connection_t *, bool server, const char *password);
/** Wrap an already authenticated transport in the same ownership contract. */
asciichat_error_t network_connection_adopt(acip_transport_t *, network_connection_t **);

/** Shared outbound establishment. Native TCP is adopted into the event backend;
 * WebSocket establishes its library-managed transport. Returns one owned object.
 */
typedef struct {
  uint32_t timeout_ms;
  bool (*should_exit_callback)(void *);
  void *callback_data;
} network_connect_config_t;

/** A NULL config uses backend defaults and the application's exit callback.
 * A supplied config preserves its timeout, including zero, and also checks its
 * cancellation callback before dialing and while waiting for establishment. */
asciichat_error_t network_connection_open(const char *name, const char *address, uint16_t default_port,
                                          const network_connect_config_t *, network_connection_t **,
                                          connection_endpoint_t *);
void network_connection_destroy(network_connection_t **);

uint8_t network_connection_crypto_mode(network_connection_t *);

/** Execute an outbound connection on its controlling worker. The owner joins
 * that worker before destroy; this call retains all resources on return.
 */
asciichat_error_t network_connection_run(network_connection_t *, const network_connection_callbacks_t *, void *);
