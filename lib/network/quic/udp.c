#include "udp.h"

#include <ascii-chat/common.h>
#include <string.h>

#define QUIC_UDP_CID_LENGTH 16
#define QUIC_UDP_PACKET_CAPACITY 1452
#define QUIC_UDP_MAX_CONNECTIONS 64
#define QUIC_UDP_READ_BATCH 128
#define QUIC_UDP_WRITE_BATCH 16

typedef struct udp_peer {
  struct udp_peer *next;
  acip_quic_connection_t *connection;
  ngtcp2_path_storage pending_path;
  uint8_t pending[QUIC_UDP_PACKET_CAPACITY];
  size_t pending_length;
} udp_peer_t;

struct acip_quic_udp_endpoint {
  socket_t socket;
  struct sockaddr_storage local;
  socklen_t local_length;
  acip_quic_tls_t *server_tls;
  acip_quic_udp_accept_fn accept;
  void *context;
  udp_peer_t *peers;
  size_t count;
};

static bool valid_address(const struct sockaddr *address, socklen_t length) {
  if (!address || (size_t)length < sizeof(struct sockaddr) || (size_t)length > sizeof(struct sockaddr_storage))
    return false;
  if (address->sa_family == AF_INET && (size_t)length >= sizeof(struct sockaddr_in))
    return ((const struct sockaddr_in *)address)->sin_addr.s_addr != 0;
  if (address->sa_family == AF_INET6 && (size_t)length >= sizeof(struct sockaddr_in6)) {
    static const uint8_t zero[16] = {0};
    return memcmp(&((const struct sockaddr_in6 *)address)->sin6_addr, zero, sizeof(zero)) != 0;
  }
  return false;
}

static ngtcp2_path make_path(const acip_quic_udp_endpoint_t *endpoint, const struct sockaddr *remote,
                             socklen_t remote_length) {
  ngtcp2_path path = {0};
  path.local.addr = (struct sockaddr *)&endpoint->local;
  path.local.addrlen = endpoint->local_length;
  path.remote.addr = (struct sockaddr *)remote;
  path.remote.addrlen = remote_length;
  return path;
}

asciichat_error_t acip_quic_udp_endpoint_create(const struct sockaddr *address, socklen_t length,
                                                acip_quic_tls_t *server_tls, acip_quic_udp_accept_fn accept,
                                                void *context, acip_quic_udp_endpoint_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC UDP endpoint output");
  *out = NULL;
  if (!valid_address(address, length) || (!!server_tls != !!accept) || (server_tls && !server_tls->server))
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC UDP requires a concrete bind address and valid listener");
  acip_quic_udp_endpoint_t *endpoint = SAFE_CALLOC(1, sizeof(*endpoint), acip_quic_udp_endpoint_t *);
  if (!endpoint)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC UDP endpoint");
  endpoint->socket = socket_create("quic_udp", address->sa_family, SOCK_DGRAM, 0);
  if (endpoint->socket == INVALID_SOCKET_VALUE || socket_bind(endpoint->socket, address, length) != 0 ||
      socket_set_nonblocking(endpoint->socket, true) != 0) {
    if (endpoint->socket != INVALID_SOCKET_VALUE)
      socket_close(endpoint->socket);
    SAFE_FREE(endpoint);
    return SET_ERRNO(ERROR_NETWORK, "Cannot bind QUIC UDP endpoint");
  }
  endpoint->local_length = sizeof(endpoint->local);
  if (socket_getsockname(endpoint->socket, (struct sockaddr *)&endpoint->local, &endpoint->local_length) != 0) {
    socket_close(endpoint->socket);
    SAFE_FREE(endpoint);
    return SET_ERRNO(ERROR_NETWORK, "Cannot obtain QUIC UDP endpoint address");
  }
  endpoint->server_tls = server_tls;
  endpoint->accept = accept;
  endpoint->context = context;
  *out = endpoint;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_udp_endpoint_address(const acip_quic_udp_endpoint_t *endpoint,
                                                 struct sockaddr_storage *address, socklen_t *length) {
  if (!endpoint || !address || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC UDP address output");
  *address = endpoint->local;
  *length = endpoint->local_length;
  return ASCIICHAT_OK;
}

static udp_peer_t *new_peer(acip_quic_udp_endpoint_t *endpoint) {
  if (endpoint->count >= QUIC_UDP_MAX_CONNECTIONS)
    return NULL;
  return SAFE_CALLOC(1, sizeof(udp_peer_t), udp_peer_t *);
}

static void add_peer(acip_quic_udp_endpoint_t *endpoint, udp_peer_t *peer) {
  peer->next = endpoint->peers;
  endpoint->peers = peer;
  ++endpoint->count;
}

asciichat_error_t acip_quic_udp_connect(acip_quic_udp_endpoint_t *endpoint, acip_quic_tls_t *client_tls,
                                        const char *server_name, const struct sockaddr *remote, socklen_t remote_length,
                                        uint64_t now_ns, acip_quic_connection_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC UDP connection output");
  *out = NULL;
  if (!endpoint || !client_tls || client_tls->server || !valid_address(remote, remote_length) ||
      remote->sa_family != ((struct sockaddr *)&endpoint->local)->sa_family)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC UDP connection configuration");
  udp_peer_t *peer = new_peer(endpoint);
  if (!peer)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC UDP connection limit reached or allocation failed");
  ngtcp2_path path = make_path(endpoint, remote, remote_length);
  asciichat_error_t result =
      acip_quic_connection_client_create(client_tls, server_name, &path, now_ns, &peer->connection);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(peer);
    return result;
  }
  add_peer(endpoint, peer);
  *out = peer->connection;
  return ASCIICHAT_OK;
}

static asciichat_error_t receive_one(acip_quic_udp_endpoint_t *endpoint, const uint8_t *data, size_t length,
                                     const struct sockaddr *remote, socklen_t remote_length, uint64_t now,
                                     acip_quic_connection_t **failed) {
  ngtcp2_version_cid header;
  if (ngtcp2_pkt_decode_version_cid(&header, data, length, QUIC_UDP_CID_LENGTH) != 0)
    return ASCIICHAT_OK;
  udp_peer_t *peer = endpoint->peers;
  while (peer) {
    bool matches = false;
    asciichat_error_t result =
        acip_quic_connection_matches_cid(peer->connection, header.dcid, header.dcidlen, &matches);
    if (result != ASCIICHAT_OK)
      return result;
    if (matches)
      break;
    peer = peer->next;
  }
  ngtcp2_path path = make_path(endpoint, remote, remote_length);
  if (!peer) {
    if (!endpoint->server_tls)
      return ASCIICHAT_OK;
    ngtcp2_pkt_hd initial;
    if (ngtcp2_accept(&initial, data, length) != 0 || initial.version != NGTCP2_PROTO_VER_V1)
      return ASCIICHAT_OK;
    peer = new_peer(endpoint);
    if (!peer)
      return ASCIICHAT_OK;
    asciichat_error_t result =
        acip_quic_connection_server_create(endpoint->server_tls, &path, data, length, now, &peer->connection);
    if (result == ASCIICHAT_OK)
      result = endpoint->accept(peer->connection, endpoint->context);
    if (result != ASCIICHAT_OK) {
      acip_quic_connection_destroy(&peer->connection);
      SAFE_FREE(peer);
      return result;
    }
    add_peer(endpoint, peer);
  }
  asciichat_error_t result = acip_quic_connection_receive(peer->connection, &path, data, length, now);
  if (result != ASCIICHAT_OK)
    *failed = peer->connection;
  return result;
}

asciichat_error_t acip_quic_udp_pump(acip_quic_udp_endpoint_t *endpoint, uint64_t now,
                                     acip_quic_connection_t **failed) {
  if (!endpoint || !failed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC UDP pump arguments");
  *failed = NULL;
  for (unsigned i = 0; i < QUIC_UDP_READ_BATCH; ++i) {
    uint8_t data[65536];
    struct sockaddr_storage remote;
    socklen_t remote_length = sizeof(remote);
    ssize_t received =
        socket_recvfrom(endpoint->socket, data, sizeof(data), 0, (struct sockaddr *)&remote, &remote_length);
    if (received < 0) {
      if (socket_is_would_block_error(socket_get_last_error()))
        break;
      return SET_ERRNO(ERROR_NETWORK, "QUIC UDP receive failed");
    }
    if (!received || !valid_address((struct sockaddr *)&remote, remote_length))
      continue;
    asciichat_error_t result =
        receive_one(endpoint, data, (size_t)received, (struct sockaddr *)&remote, remote_length, now, failed);
    if (result != ASCIICHAT_OK)
      return result;
  }
  for (udp_peer_t *peer = endpoint->peers; peer; peer = peer->next) {
    if (acip_quic_connection_expiry(peer->connection) <= now) {
      asciichat_error_t result = acip_quic_connection_handle_expiry(peer->connection, now);
      if (result != ASCIICHAT_OK) {
        *failed = peer->connection;
        return result;
      }
    }
    for (unsigned i = 0; i < QUIC_UDP_WRITE_BATCH; ++i) {
      if (!peer->pending_length) {
        asciichat_error_t result = acip_quic_connection_write(peer->connection, &peer->pending_path, peer->pending,
                                                              sizeof(peer->pending), &peer->pending_length, now);
        if (result != ASCIICHAT_OK) {
          *failed = peer->connection;
          return result;
        }
        if (!peer->pending_length)
          break;
      }
      ngtcp2_addr *remote = &peer->pending_path.path.remote;
      ssize_t sent = socket_sendto(endpoint->socket, peer->pending, peer->pending_length, 0, remote->addr,
                                   (socklen_t)remote->addrlen);
      if (sent < 0 && socket_is_would_block_error(socket_get_last_error()))
        break;
      if (sent < 0 || (size_t)sent != peer->pending_length)
        return SET_ERRNO(ERROR_NETWORK, "QUIC UDP send failed");
      peer->pending_length = 0;
      asciichat_error_t result = acip_quic_connection_packet_sent(peer->connection, now);
      if (result != ASCIICHAT_OK) {
        *failed = peer->connection;
        return result;
      }
    }
  }
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_udp_remove(acip_quic_udp_endpoint_t *endpoint, acip_quic_connection_t *connection) {
  if (!endpoint || !connection)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC UDP removal");
  udp_peer_t **next = &endpoint->peers;
  while (*next && (*next)->connection != connection)
    next = &(*next)->next;
  if (!*next)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC UDP connection is not owned by endpoint");
  udp_peer_t *peer = *next;
  *next = peer->next;
  --endpoint->count;
  acip_quic_connection_destroy(&peer->connection);
  SAFE_FREE(peer);
  return ASCIICHAT_OK;
}

void acip_quic_udp_endpoint_destroy(acip_quic_udp_endpoint_t **endpoint) {
  if (!endpoint || !*endpoint)
    return;
  while ((*endpoint)->peers)
    (void)acip_quic_udp_remove(*endpoint, (*endpoint)->peers->connection);
  socket_close((*endpoint)->socket);
  SAFE_FREE(*endpoint);
  *endpoint = NULL;
}
