/**
 * @file connection_factory.c
 * @brief Shared connection factory for TCP and WebSocket endpoints
 */

#include <ascii-chat/network/connection_factory.h>

#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/platform/abstraction.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <string.h>

static asciichat_error_t connection_factory_open_tcp(const char *name, const connection_endpoint_t *endpoint,
                                                      crypto_context_t *crypto_ctx, acip_transport_t **transport_out) {
  struct addrinfo hints;
  struct addrinfo *addr_result = NULL;
  struct addrinfo *addr_iter = NULL;
  char port_str[16];
  socket_t sockfd = INVALID_SOCKET_VALUE;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  safe_snprintf(port_str, sizeof(port_str), "%u", endpoint->port);

  int gai_result = getaddrinfo(endpoint->host, port_str, &hints, &addr_result);
  if (gai_result != 0) {
    log_error("Failed to resolve TCP endpoint %s:%u: %s", endpoint->host, endpoint->port, gai_strerror(gai_result));
    return SET_ERRNO(ERROR_NETWORK_CONNECT, "Failed to resolve TCP endpoint");
  }

  for (addr_iter = addr_result; addr_iter != NULL; addr_iter = addr_iter->ai_next) {
    sockfd = socket(addr_iter->ai_family, addr_iter->ai_socktype, addr_iter->ai_protocol);
    if (sockfd == INVALID_SOCKET_VALUE) {
      continue;
    }

    if (connect_with_timeout(sockfd, addr_iter->ai_addr, addr_iter->ai_addrlen, CONNECT_TIMEOUT)) {
      break;
    }

    socket_close(sockfd);
    sockfd = INVALID_SOCKET_VALUE;
  }

  freeaddrinfo(addr_result);

  if (sockfd == INVALID_SOCKET_VALUE) {
    log_error("Failed to connect to TCP endpoint %s:%u", endpoint->host, endpoint->port);
    return SET_ERRNO(ERROR_NETWORK_CONNECT, "Failed to connect to TCP endpoint");
  }

  acip_transport_t *transport = acip_tcp_transport_create(name, sockfd, crypto_ctx);
  if (!transport) {
    log_error("Failed to create TCP transport for %s:%u", endpoint->host, endpoint->port);
    socket_close(sockfd);
    return SET_ERRNO(ERROR_NETWORK, "Failed to create TCP transport");
  }

  *transport_out = transport;
  return ASCIICHAT_OK;
}

asciichat_error_t connection_factory_open(const char *name, const char *input, uint16_t default_port,
                                          crypto_context_t *crypto_ctx, acip_transport_t **transport_out,
                                          connection_endpoint_t *endpoint_out) {
  if (!name || !input || !transport_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid connection factory parameters");
  }

  *transport_out = NULL;

  connection_endpoint_t endpoint = {0};
  asciichat_error_t resolve_result = connection_endpoint_resolve(input, default_port, &endpoint);
  if (resolve_result != ASCIICHAT_OK) {
    return resolve_result;
  }

  acip_transport_t *transport = NULL;

  switch (endpoint.protocol) {
  case CONNECTION_ENDPOINT_WEBSOCKET:
    transport = acip_websocket_client_transport_create(name, endpoint.input, crypto_ctx);
    if (!transport) {
      return SET_ERRNO(ERROR_NETWORK_CONNECT, "Failed to create WebSocket transport");
    }
    break;

  case CONNECTION_ENDPOINT_TCP:
    resolve_result = connection_factory_open_tcp(name, &endpoint, crypto_ctx, &transport);
    if (resolve_result != ASCIICHAT_OK) {
      return resolve_result;
    }
    break;

  default:
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported endpoint protocol");
  }

  if (endpoint_out) {
    *endpoint_out = endpoint;
  }
  *transport_out = transport;
  return ASCIICHAT_OK;
}
