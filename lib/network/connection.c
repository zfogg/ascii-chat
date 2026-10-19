#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/websocket/server.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/parallel_connect.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <string.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

#define NETWORK_CONNECTION_LIMIT 512

struct network_server {
  network_server_config_t config;
  tcp_server_t listener;
  websocket_server_t websocket;
  asciichat_thread_t websocket_thread;
  thread_pool_t *peers;
  mutex_t registry_mutex;
  network_connection_t *connections;
  size_t count;
  atomic_t stopping, executing;
  bool listener_initialized, websocket_initialized, websocket_started;
};

struct network_connection {
  network_server_t *server;
  network_connection_callbacks_t callbacks;
  void *callback_context;
  network_server_config_t policy;
  network_connection_t *next;
  acip_transport_t *transport;
  platform_event_stream_t *stream;
  packet_decoder_t decoder;
  acip_security_t security;
  bool security_claimed, security_options;
  atomic_ptr_t context;
  char peer[256];
  uint16_t port;
  atomic_t state, pins;
  bool secure, opened, authenticated_peer, cleanup_failed;
  uint64_t started, last_packet, partial_since;
  const void *input;
  size_t input_length;
  asciichat_error_t result;
};

acip_transport_t *network_connection_transport(network_connection_t *c) {
  return c ? c->transport : NULL;
}
void *network_connection_context(network_connection_t *c) {
  return c ? atomic_ptr_load(&c->context) : NULL;
}
void network_connection_set_context(network_connection_t *c, void *context) {
  atomic_ptr_store(&c->context, context);
}
const char *network_connection_peer(network_connection_t *c) {
  return c->peer;
}
bool network_connection_secure(network_connection_t *c) {
  return c->secure;
}
network_connection_state_t network_connection_state(network_connection_t *c) {
  return atomic_load_int(&c->state);
}
void network_connection_ready(network_connection_t *c) {
  int expected = NETWORK_NEGOTIATING;
  atomic_cas_int(&c->state, &expected, NETWORK_READY);
}
void network_connection_request_stop(network_connection_t *c) {
  if (!c)
    return;
  atomic_store_int(&c->state, NETWORK_CLOSING);
  acip_transport_interrupt(c->transport);
}

crypto_handshake_context_t *network_connection_handshake(network_connection_t *c) {
  c->security_claimed = true;
  return &c->security.handshake;
}
asciichat_error_t network_connection_prepare_security(network_connection_t *c, bool server, const char *password) {
  if (!c || c->security_claimed || network_connection_state(c) != NETWORK_NEGOTIATING)
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection security already configured");
  c->security_claimed = c->security_options = true;
  asciichat_error_t result = acip_security_init_options(&c->security, server, c->secure, password);
  if (result == ASCIICHAT_OK && !server) {
    crypto_handshake_context_t *ctx = &c->security.handshake;
    SAFE_STRNCPY(ctx->server_hostname, c->peer, sizeof(ctx->server_hostname));
    SAFE_STRNCPY(ctx->server_ip, c->peer, sizeof(ctx->server_ip));
    ctx->server_port = c->port;
  }
  return result;
}

static network_connection_t *connection_create(network_server_t *s, acip_transport_t *transport, const char *peer,
                                               bool secure, bool ready) {
  network_connection_t *c = SAFE_CALLOC(1, sizeof(*c), network_connection_t *);
  if (!c)
    return NULL;
  c->server = s;
  c->transport = transport;
  c->secure = secure;
  c->authenticated_peer = ready;
  c->started = c->last_packet = time_get_ns();
  atomic_store_int(&c->state, ready ? NETWORK_READY : NETWORK_NEGOTIATING);
  SAFE_STRNCPY(c->peer, peer ? peer : "", sizeof(c->peer));
  c->policy.handshake_timeout_ms = 30000;
  if (s) {
    c->policy = s->config;
    c->callbacks = s->config.callbacks;
    c->callback_context = s->config.user_data;
    c->decoder.max_payload = s->config.max_payload;
    mutex_lock(&s->registry_mutex);
    if (atomic_load_bool(&s->stopping) || s->count >= s->config.max_connections) {
      mutex_unlock(&s->registry_mutex);
      SAFE_FREE(c);
      SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Connection admission limit reached");
      return NULL;
    }
    c->next = s->connections;
    s->connections = c;
    ++s->count;
    mutex_unlock(&s->registry_mutex);
  }
  return c;
}

void network_server_foreach(network_server_t *s, network_connection_visit_fn visit, void *context) {
  if (!s || !visit)
    return;
  network_connection_t *snapshot[NETWORK_CONNECTION_LIMIT];
  size_t count = 0;
  mutex_lock(&s->registry_mutex);
  for (network_connection_t *c = s->connections; c; c = c->next) {
    if (atomic_load_int(&c->state) < NETWORK_CLOSING) {
      atomic_fetch_add_int(&c->pins, 1);
      snapshot[count++] = c;
    }
  }
  mutex_unlock(&s->registry_mutex);
  for (size_t i = 0; i < count; ++i) {
    visit(snapshot[i], context);
    atomic_fetch_sub_int(&snapshot[i]->pins, 1);
  }
}

static void connection_finish(void *arg) {
  network_connection_t *c = arg;
  network_server_t *s = c->server;
  atomic_store_int(&c->state, NETWORK_CLOSING);
  if (s) {
    mutex_lock(&s->registry_mutex);
    network_connection_t **link = &s->connections;
    while (*link && *link != c)
      link = &(*link)->next;
    if (*link)
      *link = c->next;
    mutex_unlock(&s->registry_mutex);
    while (atomic_load_int(&c->pins))
      platform_sleep_ms(1);
  }
  if (c->callbacks.closing) {
    asciichat_error_t result = c->callbacks.closing(c, c->callback_context);
    if (result != ASCIICHAT_OK) {
      c->result = result;
      c->cleanup_failed = true;
      if (s) {
        mutex_lock(&s->registry_mutex);
        c->next = s->connections;
        s->connections = c;
        mutex_unlock(&s->registry_mutex);
      }
      return;
    }
  }
  c->cleanup_failed = false;
  acip_transport_destroy(c->transport);
  c->transport = NULL;
  if (c->security_options)
    acip_security_cleanup(&c->security);
  else if (c->security_claimed)
    crypto_handshake_destroy(&c->security.handshake);
  packet_decoder_reset(&c->decoder);
  atomic_store_int(&c->state, NETWORK_CLOSED);
  if (c->callbacks.closed)
    c->callbacks.closed(c, c->callback_context);
  if (s) {
    mutex_lock(&s->registry_mutex);
    --s->count;
    mutex_unlock(&s->registry_mutex);
  }
}
static void connection_free(void *arg) {
  network_connection_t *c = arg;
  if (!c->cleanup_failed)
    SAFE_FREE(c);
}

static asciichat_error_t connection_open(network_connection_t *c) {
  if (c->opened)
    return ASCIICHAT_OK;
  c->opened = true;
  return c->callbacks.open ? c->callbacks.open(c, c->callback_context) : ASCIICHAT_OK;
}

static asciichat_error_t connection_packet(const void *data, size_t length, void *arg) {
  network_connection_t *c = arg;
  if (atomic_load_int(&c->state) >= NETWORK_CLOSING)
    return SET_ERRNO(ERROR_NETWORK, "Connection is stopping");
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  packet_envelope_t envelope = {
      .type = NET_TO_HOST_U16(header.type), .data = (uint8_t *)data + sizeof(header), .len = length - sizeof(header)};
  asciichat_error_t result = ASCIICHAT_OK;
  if (!c->authenticated_peer && network_connection_state(c) == NETWORK_READY && c->transport->crypto_ctx &&
      c->transport->crypto_ctx->encrypt_data) {
    bool backend_decrypted = !c->stream && acip_transport_get_type(c->transport) == ACIP_TRANSPORT_TCP;
    if (envelope.type != PACKET_TYPE_ENCRYPTED && !backend_decrypted)
      return SET_ERRNO(ERROR_CRYPTO, "Plaintext application packet on encrypted connection");
    if (envelope.type == PACKET_TYPE_ENCRYPTED)
      result = packet_decrypt_envelope(&envelope, c->transport->crypto_ctx);
    if (result == ASCIICHAT_OK && envelope.allocated_buffer) {
      const packet_header_t *inner = envelope.allocated_buffer;
      if (NET_TO_HOST_U64(inner->magic) != PACKET_MAGIC || envelope.type == PACKET_TYPE_ENCRYPTED ||
          asciichat_crc32(envelope.data, envelope.len) != NET_TO_HOST_U32(inner->crc32))
        result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid encrypted packet");
    }
  }
  c->last_packet = time_get_ns();
  if (result == ASCIICHAT_OK)
    result = c->callbacks.packet(c, envelope.type, envelope.data, envelope.len, c->callback_context);
  buffer_release(envelope.allocated_buffer);
  return result;
}

static uint64_t connection_deadline(network_connection_t *c) {
  network_server_config_t *cfg = &c->policy;
  uint64_t now = time_get_ns(), end = 0;
  if (network_connection_state(c) == NETWORK_NEGOTIATING)
    end = c->started + cfg->handshake_timeout_ms * NS_PER_MS_INT;
  else if (c->partial_since && cfg->partial_timeout_ms)
    end = c->partial_since + cfg->partial_timeout_ms * NS_PER_MS_INT;
  else if (cfg->idle_timeout_ms)
    end = c->last_packet + cfg->idle_timeout_ms * NS_PER_MS_INT;
  return !end ? 0 : now >= end ? 1 : (end - now) / NS_PER_MS_INT + 1;
}

static void connection_work(void *arg) {
  network_connection_t *c = arg;
  c->result = connection_open(c);
  if (c->result == ASCIICHAT_OK && c->input_length) {
    uint64_t previous_packet = c->last_packet;
    bool partial = packet_decoder_pending(&c->decoder);
    c->result = packet_decoder_feed(&c->decoder, c->input, c->input_length, connection_packet, c);
    if (packet_decoder_pending(&c->decoder) && (!partial || previous_packet != c->last_packet))
      c->partial_since = time_get_ns();
    if (!packet_decoder_pending(&c->decoder))
      c->partial_since = 0;
  }
}
static void stream_done(platform_event_stream_t *stream, void *arg, bool closing) {
  network_connection_t *c = arg;
  if (closing)
    return;
  if (c->result != ASCIICHAT_OK || network_connection_state(c) >= NETWORK_CLOSING) {
    platform_event_stream_close(stream, c->result != ASCIICHAT_OK ? c->result : ERROR_NETWORK);
    return;
  }
  asciichat_error_t result = platform_event_stream_deadline(stream, connection_deadline(c));
  if (result == ASCIICHAT_OK)
    result = platform_event_stream_resume(stream);
  if (result != ASCIICHAT_OK)
    platform_event_stream_close(stream, result);
}
static void stream_read(platform_event_stream_t *stream, const void *data, size_t length, void *arg) {
  network_connection_t *c = arg;
  c->input = data;
  c->input_length = length;
  asciichat_error_t result = platform_event_stream_work(stream, connection_work, stream_done, c);
  if (result != ASCIICHAT_OK)
    platform_event_stream_close(stream, result);
}
static void stream_closed(void *arg, asciichat_error_t reason) {
  network_connection_t *c = arg;
  log_debug("TCP connection %s closed: %s", c->peer, asciichat_error_string(reason));
  c->result = reason;
  acip_async_tcp_transport_detach(c->transport);
  c->stream = NULL;
  if (platform_event_loop_work(c->server->listener.event_loop, connection_finish, connection_free, c) != ASCIICHAT_OK) {
    c->cleanup_failed = true;
    network_server_request_stop(c->server);
  }
}
static void listener_accept(platform_event_stream_t *stream, void *arg) {
  network_server_t *s = arg;
  char peer[INET6_ADDRSTRLEN] = "";
  struct sockaddr_storage address;
  socklen_t length = sizeof(address);
  if (platform_event_stream_peer(stream, &address, &length) == ASCIICHAT_OK)
    format_ip_address(address.ss_family, (struct sockaddr *)&address, peer, sizeof(peer));
  acip_transport_t *transport = acip_async_tcp_transport_create(stream, NULL);
  network_connection_t *c = transport ? connection_create(s, transport, peer, false, false) : NULL;
  if (!c) {
    if (transport) {
      acip_async_tcp_transport_detach(transport);
      acip_transport_destroy(transport);
    }
    platform_event_stream_close(stream, ERROR_RESOURCE_EXHAUSTED);
    return;
  }
  c->stream = stream;
  asciichat_error_t result = platform_event_stream_start(stream, stream_read, stream_closed, c);
  if (result == ASCIICHAT_OK)
    result = platform_event_stream_deadline(stream, connection_deadline(c));
  if (result == ASCIICHAT_OK)
    result = platform_event_stream_work(stream, connection_work, stream_done, c);
  if (result != ASCIICHAT_OK)
    platform_event_stream_close(stream, result);
}

static void *connection_receive(void *arg) {
  network_connection_t *c = arg;
  asciichat_error_t result = connection_open(c);
  c->transport->receive_timeout_ns = 100 * NS_PER_MS_INT;
  while (result == ASCIICHAT_OK && !APP_CALLBACK_BOOL(should_exit) &&
         (!c->server || !atomic_load_bool(&c->server->stopping)) && network_connection_state(c) < NETWORK_CLOSING) {
    if (connection_deadline(c) == 1) {
      result = ERROR_NETWORK_TIMEOUT;
      break;
    }
    void *data = NULL, *allocation = NULL;
    size_t length = 0;
    asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
    result = acip_transport_recv(c->transport, &data, &length, &allocation);
    if (result == ERROR_NETWORK_TIMEOUT || result == ERROR_NETWORK_INCOMPLETE) {
      if (result == ERROR_NETWORK_INCOMPLETE && !c->partial_since)
        c->partial_since = time_get_ns();
      asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
      buffer_release(allocation);
      result = ASCIICHAT_OK;
      continue;
    }
    if (result == ASCIICHAT_OK) {
      c->input = data;
      c->input_length = length;
      connection_work(c);
      result = c->result;
    }
    buffer_release(allocation);
    asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
  }
  log_debug("Connection %s receive stopped: %s", c->peer, asciichat_error_string(result));
  acip_transport_close(c->transport);
  c->result = result;
  if (c->server) {
    connection_finish(c);
    connection_free(c);
  }
  return NULL;
}
static void *websocket_accept(void *arg) {
  websocket_client_context_t *ctx = arg;
  network_connection_t *c = connection_create(ctx->user_data, ctx->transport, ctx->client_ip, ctx->is_secure, false);
  if (!c)
    acip_transport_destroy(ctx->transport);
  SAFE_FREE(ctx);
  return c ? connection_receive(c) : NULL;
}
asciichat_error_t network_server_adopt(network_server_t *s, acip_transport_t *transport, const char *peer,
                                       bool secure) {
  if (!s || !transport)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing adopted connection");
  network_connection_t *c = connection_create(s, transport, peer, secure, true);
  if (!c)
    return SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Cannot admit peer connection");
  asciichat_error_t result = thread_pool_spawn(s->peers, connection_receive, c, 0, "network_peer");
  if (result != ASCIICHAT_OK) {
    c->transport = NULL;
    connection_finish(c);
    connection_free(c);
  }
  return result;
}
static void *websocket_run(void *arg) {
  websocket_server_run(arg);
  return NULL;
}
static void server_tick(void *arg) {
  network_server_t *s = arg;
  platform_event_stream_t *closing[NETWORK_CONNECTION_LIMIT];
  size_t count = 0;
  mutex_lock(&s->registry_mutex);
  for (network_connection_t *c = s->connections; c; c = c->next)
    if (c->stream && network_connection_state(c) >= NETWORK_CLOSING)
      closing[count++] = c->stream;
  mutex_unlock(&s->registry_mutex);
  for (size_t i = 0; i < count; ++i)
    platform_event_stream_close(closing[i], ASCIICHAT_OK);
  if (atomic_load_bool(&s->stopping))
    atomic_store_bool(&s->listener.running, false);
  else if (s->config.tick)
    s->config.tick(s->config.tick_context);
}
asciichat_error_t network_server_create(const network_server_config_t *config, network_server_t **out) {
  if (!config || !out || !config->callbacks.packet)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing network server configuration");
  *out = NULL;
  network_server_t *s = SAFE_CALLOC(1, sizeof(*s), network_server_t *);
  if (!s)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate network server");
  s->config = *config;
  s->config.max_connections =
      config->max_connections ? MIN(config->max_connections, NETWORK_CONNECTION_LIMIT) : NETWORK_CONNECTION_LIMIT;
  if (!s->config.handshake_timeout_ms)
    s->config.handshake_timeout_ms = 30000;
  if (!s->config.partial_timeout_ms)
    s->config.partial_timeout_ms = 5000;
  asciichat_error_t result = mutex_init(&s->registry_mutex, "network_registry");
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(s);
    return result;
  }
  s->peers = thread_pool_create("network_peers");
  if (!s->peers) {
    mutex_destroy(&s->registry_mutex);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_THREAD, "Cannot create peer workers");
  }
  tcp_server_config_t tcp = {.port = config->port,
                             .ipv4_address = config->ipv4_address,
                             .ipv6_address = config->ipv6_address,
                             .bind_ipv4 = config->bind_ipv4,
                             .bind_ipv6 = config->bind_ipv6,
                             .event_handler = listener_accept,
                             .user_data = s,
                             .tick_fn = server_tick,
                             .tick_user_data = s};
  result = tcp_server_init(&s->listener, &tcp);
  s->listener_initialized = result == ASCIICHAT_OK;
  if (result == ASCIICHAT_OK && config->websocket_enabled) {
    websocket_server_config_t ws = {.port = config->websocket_port,
                                    .client_handler = websocket_accept,
                                    .user_data = s,
                                    .tls_cert_path = config->tls_cert_path,
                                    .tls_key_path = config->tls_key_path};
    result = websocket_server_init(&s->websocket, &ws);
    s->websocket_initialized = result == ASCIICHAT_OK;
  }
  if (result != ASCIICHAT_OK) {
    network_server_destroy(&s);
    return result;
  }
  *out = s;
  return ASCIICHAT_OK;
}
asciichat_error_t network_server_start(network_server_t *s) {
  if (!s || atomic_load_bool(&s->stopping))
    return SET_ERRNO(ERROR_INVALID_STATE, "Network server stopped before startup");
  if (s->websocket_initialized && !s->websocket_started) {
    asciichat_error_t result =
        asciichat_thread_create(&s->websocket_thread, "websocket_io", websocket_run, &s->websocket);
    if (result != ASCIICHAT_OK)
      return result;
    s->websocket_started = true;
  }
  return ASCIICHAT_OK;
}
asciichat_error_t network_server_run(network_server_t *s) {
  int expected = false;
  if (!s || !atomic_cas_int(&s->executing, &expected, true))
    return SET_ERRNO(ERROR_INVALID_STATE, "Network server already has an execution owner");
  asciichat_error_t result = tcp_server_run(&s->listener);
  atomic_store_bool(&s->executing, false);
  return result;
}
void network_server_request_stop(network_server_t *s) {
  if (s)
    atomic_store_bool(&s->stopping, true);
}
static void stop_connection(network_connection_t *c, void *unused) {
  (void)unused;
  network_connection_request_stop(c);
}
asciichat_error_t network_server_stop(network_server_t *s) {
  if (!s)
    return ASCIICHAT_OK;
  network_server_request_stop(s);
  if (atomic_load_bool(&s->executing))
    return SET_ERRNO(ERROR_THREAD, "Join the network execution owner before releasing server dependencies");
  network_server_foreach(s, stop_connection, NULL);
  if (s->websocket_started) {
    atomic_store_bool(&s->websocket.running, false);
    websocket_server_cancel_service(&s->websocket);
    asciichat_error_t result = asciichat_thread_join(&s->websocket_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    s->websocket_started = false;
  }
  if (s->websocket_initialized) {
    websocket_server_destroy(&s->websocket);
    s->websocket_initialized = false;
  }
  asciichat_error_t result = thread_pool_stop_all(s->peers);
  if (result != ASCIICHAT_OK)
    return result;
  network_connection_t *c = s->connections;
  while (c) {
    network_connection_t *next = c->next;
    if (c->cleanup_failed) {
      connection_finish(c);
      if (c->cleanup_failed)
        return c->result;
      connection_free(c);
    }
    c = next;
  }
  return s->count ? SET_ERRNO(ERROR_THREAD, "Connections still retain server dependencies") : ASCIICHAT_OK;
}
void network_server_destroy(network_server_t **owner) {
  if (!owner || !*owner)
    return;
  network_server_t *s = *owner;
  if (network_server_stop(s) != ASCIICHAT_OK)
    return;
  if (s->listener_initialized)
    tcp_server_destroy(&s->listener);
  thread_pool_destroy(s->peers);
  mutex_destroy(&s->registry_mutex);
  SAFE_FREE(*owner);
  *owner = NULL;
}
size_t network_server_connection_count(network_server_t *s) {
  if (!s)
    return 0;
  mutex_lock(&s->registry_mutex);
  size_t count = s->count;
  mutex_unlock(&s->registry_mutex);
  return count;
}
bool network_server_websocket_enabled(network_server_t *s) {
  return s && s->config.websocket_enabled;
}
const network_server_config_t *network_server_config(network_server_t *s) {
  return s ? &s->config : NULL;
}

static bool connect_cancelled(void *unused) {
  (void)unused;
  return APP_CALLBACK_BOOL(should_exit);
}
static asciichat_error_t connection_open_tcp(const char *name, const connection_endpoint_t *endpoint,
                                             crypto_context_t *crypto, acip_transport_t **out) {
  (void)name;
  socket_t socket = INVALID_SOCKET_VALUE;
  parallel_connect_config_t config = {.hostname = endpoint->host,
                                      .port = endpoint->port,
                                      .timeout_ms = 3000,
                                      .should_exit_callback = connect_cancelled};
  asciichat_error_t result = parallel_connect(&config, &socket);
  if (result != ASCIICHAT_OK)
    return result;
  *out = acip_async_tcp_client_create(&socket, crypto);
  if (socket != INVALID_SOCKET_VALUE)
    socket_close(socket);
  return *out ? ASCIICHAT_OK : SET_ERRNO(ERROR_NETWORK, "Cannot adopt connected TCP socket");
}

asciichat_error_t network_connection_open(const char *name, const char *address, uint16_t port,
                                          network_connection_t **out, connection_endpoint_t *resolved) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing connection output");
  *out = NULL;
  connection_endpoint_t endpoint;
  asciichat_error_t result = connection_endpoint_resolve(address, port, &endpoint);
  if (result != ASCIICHAT_OK)
    return result;
  acip_transport_t *transport = NULL;
  if (endpoint.protocol == CONNECTION_ENDPOINT_WEBSOCKET) {
    transport = acip_websocket_client_transport_create(name, endpoint.input, NULL);
  } else {
    result = connection_open_tcp(name, &endpoint, NULL, &transport);
  }
  if (!transport)
    return SET_ERRNO(ERROR_NETWORK_CONNECT, "Cannot establish connection to %s", address);
  if (endpoint.protocol == CONNECTION_ENDPOINT_WEBSOCKET) {
    uint64_t deadline = time_get_ns() + 15 * NS_PER_SEC_INT;
    while (!acip_transport_is_connected(transport) && time_get_ns() < deadline && !connect_cancelled(NULL))
      platform_sleep_ms(10);
    if (!acip_transport_is_connected(transport)) {
      acip_transport_destroy(transport);
      return SET_ERRNO(ERROR_NETWORK_CONNECT, "WebSocket establishment failed or cancelled");
    }
  }
  network_connection_t *c =
      connection_create(NULL, transport, endpoint.host, strncmp(endpoint.input, "wss://", 6) == 0, false);
  if (!c) {
    acip_transport_destroy(transport);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate connection");
  }
  if (resolved)
    *resolved = endpoint;
  c->port = endpoint.port;
  *out = c;
  return ASCIICHAT_OK;
}
asciichat_error_t network_connection_adopt(acip_transport_t *transport, network_connection_t **out) {
  if (!transport || !out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing connection adoption parameters");
  *out = connection_create(NULL, transport, "peer", true, true);
  return *out ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEMORY, "Cannot own authenticated transport");
}
void network_connection_destroy(network_connection_t **owner) {
  if (!owner || !*owner)
    return;
  connection_finish(*owner);
  if ((*owner)->cleanup_failed)
    return;
  connection_free(*owner);
  *owner = NULL;
}

uint8_t network_connection_crypto_mode(network_connection_t *c) {
  return c->security.mode;
}

asciichat_error_t network_connection_run(network_connection_t *c, const network_connection_callbacks_t *callbacks,
                                         void *context) {
  if (!c || c->server || !callbacks || !callbacks->packet || c->opened)
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection already has an execution owner");
  c->callbacks = *callbacks;
  c->callback_context = context;
  connection_receive(c);
  return c->result;
}
