#include <ascii-chat/network/acip/listener.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/util/ip.h>

typedef struct accepted_peer {
  struct accepted_peer *next;
  acip_transport_t *transport;
  char address[64];
  int port;
} accepted_peer_t;

struct acip_listener {
  tcp_server_t server;
  mutex_t mutex;
  cond_t ready;
  asciichat_thread_t thread;
  bool thread_started, started, finished;
  asciichat_error_t result;
  accepted_peer_t *head, *tail;
  size_t pending, capacity;
  uint16_t port;
};

static void listener_accept(platform_event_stream_t *stream, void *context) {
  acip_listener_t *listener = context;
  mutex_lock(&listener->mutex);
  bool full = listener->pending >= listener->capacity;
  mutex_unlock(&listener->mutex);
  if (full) {
    platform_event_stream_close(stream, ERROR_SESSION_FULL);
    return;
  }
  accepted_peer_t *peer = SAFE_CALLOC(1, sizeof(*peer), accepted_peer_t *);
  struct sockaddr_storage address;
  socklen_t size = sizeof(address);
  if (platform_event_stream_peer(stream, &address, &size) == ASCIICHAT_OK) {
    format_ip_address(address.ss_family, (struct sockaddr *)&address, peer->address, sizeof(peer->address));
    peer->port = address.ss_family == AF_INET ? ntohs(((struct sockaddr_in *)&address)->sin_port)
                                              : ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
  }
  peer->transport = acip_async_tcp_accepted_create(stream);
  if (!peer->transport) {
    SAFE_FREE(peer);
    platform_event_stream_close(stream, ERROR_NETWORK);
    return;
  }
  mutex_lock(&listener->mutex);
  if (listener->tail)
    listener->tail->next = peer;
  else
    listener->head = peer;
  listener->tail = peer;
  ++listener->pending;
  mutex_unlock(&listener->mutex);
}

static void listener_ready(void *context) {
  acip_listener_t *listener = context;
  mutex_lock(&listener->mutex);
  listener->started = true;
  cond_broadcast(&listener->ready);
  mutex_unlock(&listener->mutex);
}

static void *listener_run(void *context) {
  acip_listener_t *listener = context;
  asciichat_error_t result = tcp_server_run(&listener->server);
  mutex_lock(&listener->mutex);
  listener->result = result;
  listener->finished = true;
  cond_broadcast(&listener->ready);
  mutex_unlock(&listener->mutex);
  return NULL;
}

asciichat_error_t acip_listener_open(const char *address, uint16_t port, size_t capacity, acip_listener_t **out) {
  if (!out || !capacity)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Listener output and capacity are required");
  *out = NULL;
  acip_listener_t *listener = SAFE_CALLOC(1, sizeof(*listener), acip_listener_t *);
  asciichat_error_t result = mutex_init(&listener->mutex, "listener");
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(listener);
    return result;
  }
  result = cond_init(&listener->ready, "listener_ready");
  if (result != ASCIICHAT_OK) {
    mutex_destroy(&listener->mutex);
    SAFE_FREE(listener);
    return result;
  }
  listener->capacity = capacity;
  bool ipv6 = address && strchr(address, ':');
  tcp_server_config_t config = {.port = port,
                                .ipv4_address = ipv6 ? NULL : address,
                                .ipv6_address = ipv6 ? address : NULL,
                                .bind_ipv4 = !ipv6,
                                .bind_ipv6 = ipv6,
                                .event_handler = listener_accept,
                                .tick_fn = listener_ready,
                                .tick_user_data = listener,
                                .user_data = listener};
  result = tcp_server_init(&listener->server, &config);
  if (result != ASCIICHAT_OK) {
    if (result != ERROR_THREAD)
      tcp_server_destroy(&listener->server);
    cond_destroy(&listener->ready);
    mutex_destroy(&listener->mutex);
    SAFE_FREE(listener);
    return result;
  }
  struct sockaddr_storage bound;
  socklen_t bound_size = sizeof(bound);
  socket_t socket = ipv6 ? listener->server.listen_socket6 : listener->server.listen_socket;
  if (socket_getsockname(socket, (struct sockaddr *)&bound, &bound_size) != 0) {
    acip_listener_destroy(listener);
    return SET_ERRNO_SYS(ERROR_NETWORK_BIND, "Cannot read listener endpoint");
  }
  listener->port =
      ipv6 ? ntohs(((struct sockaddr_in6 *)&bound)->sin6_port) : ntohs(((struct sockaddr_in *)&bound)->sin_port);
  result = asciichat_thread_create(&listener->thread, "transport_listener", listener_run, listener);
  if (result != ASCIICHAT_OK) {
    acip_listener_destroy(listener);
    return SET_ERRNO(ERROR_THREAD, "Cannot start transport listener");
  }
  listener->thread_started = true;
  mutex_lock(&listener->mutex);
  while (!listener->started && !listener->finished)
    cond_wait(&listener->ready, &listener->mutex);
  result = listener->result;
  mutex_unlock(&listener->mutex);
  if (result != ASCIICHAT_OK) {
    acip_listener_destroy(listener);
    return result;
  }
  *out = listener;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_listener_accept(acip_listener_t *listener, acip_transport_t **transport, char *address,
                                       size_t address_size, int *port) {
  if (!listener || !transport)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Listener and transport output are required");
  *transport = NULL;
  mutex_lock(&listener->mutex);
  accepted_peer_t *peer = listener->head;
  if (peer) {
    listener->head = peer->next;
    if (!listener->head)
      listener->tail = NULL;
    --listener->pending;
  }
  asciichat_error_t result = listener->result;
  mutex_unlock(&listener->mutex);
  if (peer) {
    *transport = peer->transport;
    if (address && address_size)
      SAFE_STRNCPY(address, peer->address, address_size);
    if (port)
      *port = peer->port;
    SAFE_FREE(peer);
    return ASCIICHAT_OK;
  }
  return result;
}

uint16_t acip_listener_port(const acip_listener_t *listener) {
  return listener ? listener->port : 0;
}

asciichat_error_t acip_listener_stop(acip_listener_t *listener) {
  if (!listener || !listener->thread_started)
    return ASCIICHAT_OK;
  atomic_store_bool(&listener->server.running, false);
  if (asciichat_thread_join(&listener->thread, NULL) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot join transport listener");
  listener->thread_started = false;
  return ASCIICHAT_OK;
}

void acip_listener_destroy(acip_listener_t *listener) {
  if (!listener || acip_listener_stop(listener) != ASCIICHAT_OK)
    return;
  while (listener->head) {
    accepted_peer_t *peer = listener->head;
    listener->head = peer->next;
    acip_transport_destroy(peer->transport);
    SAFE_FREE(peer);
  }
  tcp_server_destroy(&listener->server);
  cond_destroy(&listener->ready);
  mutex_destroy(&listener->mutex);
  SAFE_FREE(listener);
}
#else
asciichat_error_t acip_listener_open(const char *address, uint16_t port, size_t capacity, acip_listener_t **out) {
  (void)address;
  (void)port;
  (void)capacity;
  if (out)
    *out = NULL;
  return SET_ERRNO(ERROR_INVALID_STATE, "Browser hosts register negotiated transports directly");
}
asciichat_error_t acip_listener_accept(acip_listener_t *listener, acip_transport_t **transport, char *address,
                                       size_t address_size, int *port) {
  (void)listener;
  (void)address;
  (void)address_size;
  (void)port;
  if (transport)
    *transport = NULL;
  return SET_ERRNO(ERROR_INVALID_STATE, "No native listener in a browser");
}
uint16_t acip_listener_port(const acip_listener_t *listener) {
  (void)listener;
  return 0;
}
asciichat_error_t acip_listener_stop(acip_listener_t *listener) {
  (void)listener;
  return ASCIICHAT_OK;
}
void acip_listener_destroy(acip_listener_t *listener) {
  (void)listener;
}
#endif
