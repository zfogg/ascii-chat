#include <ascii-chat/network/acip/quic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include "transport.h"
#include "worker.h"

#define ACIP_QUIC_ENDPOINT_MAX_TRANSPORTS 64

typedef struct endpoint_transport {
  struct endpoint_transport *next;
  acip_quic_connection_t *connection;
  acip_transport_t *transport;
  bool claimed;
} endpoint_transport_t;

struct acip_quic_endpoint {
  mutex_t lane;
  acip_quic_tls_t *tls;
  acip_quic_udp_endpoint_t *udp;
  acip_quic_worker_t *worker;
  endpoint_transport_t *transports;
  size_t count;
  bool listener, stopping, stopped, prefer_audio_datagrams, prefer_visual_datagrams;
  asciichat_error_t terminal_error;
};

static void wake_endpoint(void *context) {
  acip_quic_endpoint_t *endpoint = context;
  (void)acip_quic_udp_wake(endpoint->udp);
}

static void close_connection(acip_quic_connection_t *connection, void *context) {
  acip_quic_endpoint_t *endpoint = context;
  asciichat_errno_scope_t scope = errno_scope_begin();
  (void)acip_quic_udp_close_peer(endpoint->udp, connection);
  errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
}

/* The transport has already detached its receiver. Destroy may be called by
 * the normal network connection owner after an authenticated transport is adopted. */
static void transport_destroyed(acip_transport_t *transport, void *context) {
  acip_quic_endpoint_t *endpoint = context;
  mutex_lock(&endpoint->lane);
  endpoint_transport_t **link = &endpoint->transports;
  while (*link && (*link)->transport != transport)
    link = &(*link)->next;
  endpoint_transport_t *item = *link;
  if (item) {
    if (item->connection && acip_quic_udp_contains(endpoint->udp, item->connection)) {
      (void)acip_quic_udp_remove(endpoint->udp, item->connection);
    }
    *link = item->next;
    --endpoint->count;
  }
  mutex_unlock(&endpoint->lane);
  SAFE_FREE(item);
}

/* Called on the endpoint lane. */
static asciichat_error_t attach(acip_quic_endpoint_t *endpoint, acip_quic_connection_t *connection, bool claimed,
                                acip_transport_t **out) {
  if (endpoint->stopping || endpoint->count >= ACIP_QUIC_ENDPOINT_MAX_TRANSPORTS)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC endpoint transport limit reached");
  endpoint_transport_t *item = SAFE_CALLOC(1, sizeof(*item), endpoint_transport_t *);
  if (!item)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC endpoint transport");
  asciichat_error_t result = acip_quic_transport_attach_on_lane_with_waker(
      connection, &endpoint->lane, wake_endpoint, endpoint, transport_destroyed, endpoint, &item->transport);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(item);
    return result;
  }
  acip_quic_transport_set_close_on_lane(item->transport, close_connection, endpoint);
  item->transport->prefer_audio_datagrams = endpoint->prefer_audio_datagrams;
  item->transport->prefer_visual_datagrams = endpoint->prefer_visual_datagrams;
  item->connection = connection;
  item->claimed = claimed;
  item->next = endpoint->transports;
  endpoint->transports = item;
  ++endpoint->count;
  if (out)
    *out = item->transport;
  return ASCIICHAT_OK;
}

static asciichat_error_t accept_connection(acip_quic_connection_t *connection, void *context) {
  return attach(context, connection, false, NULL);
}

/* Called by the worker on the lane. Keep failed adapters alive until
 * their owners release them, but detach the connection before removing it. */
static bool connection_failed(acip_quic_connection_t *connection, asciichat_error_t reason, void *context) {
  acip_quic_endpoint_t *endpoint = context;
  if (!connection) {
    endpoint->terminal_error = reason;
    for (endpoint_transport_t *current = endpoint->transports; current; current = current->next) {
      (void)acip_quic_transport_fail_on_lane(current->transport, reason);
      if (current->connection && acip_quic_udp_remove(endpoint->udp, current->connection) == ASCIICHAT_OK)
        current->connection = NULL;
    }
    return false;
  }
  endpoint_transport_t *item = endpoint->transports;
  while (item && item->connection != connection)
    item = item->next;
  bool handled = !item && !acip_quic_udp_contains(endpoint->udp, connection);
  if (item && acip_quic_transport_fail_on_lane(item->transport, reason) == ASCIICHAT_OK &&
      acip_quic_udp_remove(endpoint->udp, connection) == ASCIICHAT_OK) {
    item->connection = NULL;
    if (!item->claimed && acip_quic_transport_destroy_unclaimed_on_lane(&item->transport, reason) == ASCIICHAT_OK) {
      endpoint_transport_t **link = &endpoint->transports;
      while (*link != item)
        link = &(*link)->next;
      *link = item->next;
      --endpoint->count;
      SAFE_FREE(item);
    }
    handled = true;
  }
  return handled;
}

asciichat_error_t acip_quic_endpoint_open(const struct sockaddr *address, socklen_t length, bool listener,
                                          const acip_quic_config_t *config, acip_quic_endpoint_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC endpoint output");
  *out = NULL;
  if (!address || !config)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC endpoint configuration");
  acip_quic_endpoint_t *endpoint = SAFE_CALLOC(1, sizeof(*endpoint), acip_quic_endpoint_t *);
  if (!endpoint)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC endpoint");
  if (mutex_init(&endpoint->lane, "quic_endpoint") != 0) {
    SAFE_FREE(endpoint);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize QUIC endpoint lane");
  }
  const char *alpn = config->alpn ? config->alpn : "acip/1";
  asciichat_error_t result =
      listener ? acip_quic_tls_server_create(alpn, config->certificate_file, config->private_key_file, &endpoint->tls)
               : acip_quic_tls_client_create(alpn, config->ca_file, &endpoint->tls);
  if (result == ASCIICHAT_OK) {
    endpoint->tls->enable_datagrams = config->enable_datagrams;
    endpoint->prefer_audio_datagrams = config->prefer_audio_datagrams;
    endpoint->prefer_visual_datagrams = config->prefer_visual_datagrams;
    endpoint->listener = listener;
    result =
        acip_quic_udp_endpoint_create(address, length, listener ? endpoint->tls : NULL,
                                      listener ? accept_connection : NULL, listener ? endpoint : NULL, &endpoint->udp);
  }
  if (result == ASCIICHAT_OK)
    result = acip_quic_worker_start_locked_failure(endpoint->udp, &endpoint->lane, connection_failed, endpoint,
                                                   &endpoint->worker);
  if (result != ASCIICHAT_OK) {
    acip_quic_udp_endpoint_destroy(&endpoint->udp);
    acip_quic_tls_destroy(&endpoint->tls);
    mutex_destroy(&endpoint->lane);
    SAFE_FREE(endpoint);
    return result;
  }
  *out = endpoint;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_endpoint_address(acip_quic_endpoint_t *endpoint, struct sockaddr_storage *address,
                                             socklen_t *length) {
  if (!endpoint)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC endpoint");
  mutex_lock(&endpoint->lane);
  asciichat_error_t result = acip_quic_udp_endpoint_address(endpoint->udp, address, length);
  mutex_unlock(&endpoint->lane);
  return result;
}

asciichat_error_t acip_quic_connect(acip_quic_endpoint_t *endpoint, const struct sockaddr *remote,
                                    socklen_t remote_length, const char *server_name, acip_transport_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC transport output");
  *out = NULL;
  if (!endpoint || endpoint->listener || !remote || !server_name || !*server_name)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC connect configuration");
  mutex_lock(&endpoint->lane);
  acip_quic_connection_t *connection = NULL;
  asciichat_error_t result = endpoint->stopping         ? SET_ERRNO(ERROR_INVALID_STATE, "QUIC endpoint stopped")
                             : endpoint->terminal_error ? SET_ERRNO(endpoint->terminal_error, "QUIC endpoint failed")
                             : endpoint->count >= ACIP_QUIC_ENDPOINT_MAX_TRANSPORTS
                                 ? SET_ERRNO(ERROR_BUFFER_FULL, "QUIC endpoint transport limit reached")
                                 : acip_quic_udp_connect(endpoint->udp, endpoint->tls, server_name, remote,
                                                         remote_length, time_get_ns(), &connection);
  if (result == ASCIICHAT_OK) {
    result = attach(endpoint, connection, true, out);
    if (result != ASCIICHAT_OK)
      (void)acip_quic_udp_remove(endpoint->udp, connection);
  }
  mutex_unlock(&endpoint->lane);
  if (result == ASCIICHAT_OK)
    wake_endpoint(endpoint);
  return result;
}

asciichat_error_t acip_quic_accept(acip_quic_endpoint_t *endpoint, acip_transport_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC accept output");
  *out = NULL;
  if (!endpoint || !endpoint->listener)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC listener");
  mutex_lock(&endpoint->lane);
  endpoint_transport_t *item = endpoint->transports;
  while (item && item->claimed)
    item = item->next;
  asciichat_error_t result = ASCIICHAT_OK;
  if (item) {
    item->claimed = true;
    *out = item->transport;
  } else {
    result = endpoint->terminal_error ? SET_ERRNO(endpoint->terminal_error, "QUIC endpoint failed")
             : endpoint->stopping     ? SET_ERRNO(ERROR_INVALID_STATE, "QUIC endpoint stopped")
                                      : SET_ERRNO(ERROR_NETWORK_TIMEOUT, "No QUIC connection is available");
  }
  mutex_unlock(&endpoint->lane);
  return result;
}

asciichat_error_t acip_quic_release(acip_quic_endpoint_t *endpoint, acip_transport_t **transport) {
  if (!endpoint || !transport || !*transport)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport release");
  mutex_lock(&endpoint->lane);
  endpoint_transport_t **link = &endpoint->transports;
  while (*link && (*link)->transport != *transport)
    link = &(*link)->next;
  if (!*link || !(*link)->claimed) {
    mutex_unlock(&endpoint->lane);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Transport does not belong to QUIC endpoint");
  }
  endpoint_transport_t *item = *link;
  asciichat_error_t result = acip_quic_transport_fail_on_lane(item->transport, ERROR_NETWORK);
  if (result == ASCIICHAT_OK && item->connection)
    result = acip_quic_udp_remove(endpoint->udp, item->connection);
  if (result == ASCIICHAT_OK) {
    *link = item->next;
    --endpoint->count;
  }
  mutex_unlock(&endpoint->lane);
  if (result != ASCIICHAT_OK)
    return result;
  acip_transport_destroy(item->transport);
  SAFE_FREE(item);
  *transport = NULL;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_endpoint_stop(acip_quic_endpoint_t *endpoint) {
  if (!endpoint)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC endpoint");
  if (endpoint->stopped)
    return ASCIICHAT_OK;
  mutex_lock(&endpoint->lane);
  endpoint->stopping = true;
  mutex_unlock(&endpoint->lane);
  asciichat_error_t worker_result = endpoint->worker ? acip_quic_worker_stop(&endpoint->worker) : ASCIICHAT_OK;
  if (endpoint->worker)
    return worker_result;
  asciichat_error_t result = ASCIICHAT_OK;
  for (;;) {
    mutex_lock(&endpoint->lane);
    endpoint_transport_t *item = endpoint->transports;
    while (item && item->claimed)
      item = item->next;
    if (item)
      item->claimed = true;
    mutex_unlock(&endpoint->lane);
    if (!item)
      break;
    acip_transport_t *transport = item->transport;
    result = acip_quic_release(endpoint, &transport);
    if (result != ASCIICHAT_OK)
      return result;
  }
  mutex_lock(&endpoint->lane);
  for (endpoint_transport_t *item = endpoint->transports; item; item = item->next) {
    result = acip_quic_transport_fail_on_lane(item->transport, ERROR_NETWORK);
    if (result != ASCIICHAT_OK)
      break;
    if (item->connection) {
      result = acip_quic_udp_remove(endpoint->udp, item->connection);
      if (result != ASCIICHAT_OK)
        break;
      item->connection = NULL;
    }
  }
  if (result == ASCIICHAT_OK)
    endpoint->stopped = true;
  mutex_unlock(&endpoint->lane);
  return result == ASCIICHAT_OK ? worker_result : result;
}

asciichat_error_t acip_quic_endpoint_destroy(acip_quic_endpoint_t **endpoint) {
  if (!endpoint || !*endpoint)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC endpoint destruction");
  if (!(*endpoint)->stopped || (*endpoint)->count)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop QUIC endpoint and release all transports first");
  acip_quic_udp_endpoint_destroy(&(*endpoint)->udp);
  acip_quic_tls_destroy(&(*endpoint)->tls);
  mutex_destroy(&(*endpoint)->lane);
  SAFE_FREE(*endpoint);
  *endpoint = NULL;
  return ASCIICHAT_OK;
}
