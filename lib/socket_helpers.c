/**
 * @file socket_helpers.c
 * @ingroup socket_helpers
 * @brief 🔌 Socket configuration helper implementations
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include "socket_helpers.h"
#include "common.h"
#include "logging.h"

#include <netinet/tcp.h>
#include <sys/socket.h>

int socket_configure_buffers(socket_t sockfd) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  int result = 0;

  // Configure send buffer
  const int send_buffer_size = 262144; // 256KB
  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
    log_warn("Failed to set SO_SNDBUF: send buffer may be suboptimal");
    result = -1;
  }

  // Configure receive buffer
  const int recv_buffer_size = 262144; // 256KB
  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
    log_warn("Failed to set SO_RCVBUF: receive buffer may be suboptimal");
    result = -1;
  }

  // Disable Nagle's algorithm for low-latency
  const int nodelay = 1;
  if (socket_setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    log_warn("Failed to set TCP_NODELAY: may have higher latency");
    result = -1;
  }

  return result;
}
