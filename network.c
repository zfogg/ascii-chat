#include "network.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

int set_socket_timeout(int sockfd, int timeout_seconds) {
  struct timeval timeout;
  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    return -1;
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    return -1;
  }

  return 0;
}

int set_socket_keepalive(int sockfd) {
  int keepalive = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
    return -1;
  }

#ifdef TCP_KEEPIDLE
  int keepidle = KEEPALIVE_IDLE;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
    // Not critical, continue
  }
#endif

#ifdef TCP_KEEPINTVL
  int keepintvl = KEEPALIVE_INTERVAL;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
    // Not critical, continue
  }
#endif

#ifdef TCP_KEEPCNT
  int keepcnt = KEEPALIVE_COUNT;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
    // Not critical, continue
  }
#endif

  return 0;
}

int set_socket_nonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }
  return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

bool connect_with_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout_seconds) {
  // Set socket to non-blocking mode
  if (set_socket_nonblocking(sockfd) < 0) {
    return false;
  }

  // Attempt to connect
  int result = connect(sockfd, addr, addrlen);
  if (result == 0) {
    // Connection succeeded immediately
    return true;
  }

  if (errno != EINPROGRESS) {
    return false;
  }

  // Wait for connection to complete with timeout
  fd_set write_fds;
  struct timeval timeout;

  FD_ZERO(&write_fds);
  FD_SET(sockfd, &write_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
  if (result <= 0) {
    return false; // Timeout or error
  }

  // Check if connection was successful
  int error = 0;
  socklen_t len = sizeof(error);
  if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
    return false;
  }

  return (error == 0);
}

ssize_t send_with_timeout(int sockfd, const void *buf, size_t len, int timeout_seconds) {
  fd_set write_fds;
  struct timeval timeout;
  ssize_t total_sent = 0;
  const char *data = (const char *)buf;

  while (total_sent < (ssize_t)len) {
    // Set up select for write timeout
    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);

    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    int result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
    if (result <= 0) {
      // Timeout or error
      if (result == 0) {
        errno = ETIMEDOUT;
      }
      return -1;
    }

    // Try to send data
    ssize_t sent = send(sockfd, data + total_sent, len - total_sent, 0);
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // Try again
      }
      return -1; // Real error
    }

    total_sent += sent;
  }

  return total_sent;
}

ssize_t recv_with_timeout(int sockfd, void *buf, size_t len, int timeout_seconds) {
  fd_set read_fds;
  struct timeval timeout;

  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  int result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
  if (result <= 0) {
    // Timeout or error
    if (result == 0) {
      errno = ETIMEDOUT;
    }
    return -1;
  }

  return recv(sockfd, buf, len, 0);
}

int accept_with_timeout(int listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds) {
  fd_set read_fds;
  struct timeval timeout;

  FD_ZERO(&read_fds);
  FD_SET(listenfd, &read_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  int result = select(listenfd + 1, &read_fds, NULL, NULL, &timeout);
  if (result <= 0) {
    // Timeout or error
    if (result == 0) {
      errno = ETIMEDOUT;
    }
    return -1;
  }

  return accept(listenfd, addr, addrlen);
}

const char *network_error_string(int error_code) {
  switch (error_code) {
  case ETIMEDOUT:
    return "Connection timed out";
  case ECONNREFUSED:
    return "Connection refused";
  case ENETUNREACH:
    return "Network unreachable";
  case EHOSTUNREACH:
    return "Host unreachable";
  case EAGAIN:
#if EAGAIN != EWOULDBLOCK
  case EWOULDBLOCK:
#endif
    return "Operation would block";
  case EPIPE:
    return "Broken pipe";
  case ECONNRESET:
    return "Connection reset by peer";
  default:
    return strerror(error_code);
  }
}

/* ============================================================================
 * Size Communication Protocol
 * ============================================================================
 */

int send_size_message(int sockfd, unsigned short width, unsigned short height) {
  char message[SIZE_MESSAGE_MAX_LEN];
  int len = snprintf(message, sizeof(message), SIZE_MESSAGE_FORMAT, width, height);

  if (len >= (int)sizeof(message)) {
    return -1; // Message too long
  }

  ssize_t sent = send(sockfd, message, len, 0);
  if (sent != len) {
    return -1;
  }

  return 0;
}

int parse_size_message(const char *message, unsigned short *width, unsigned short *height) {
  if (!message || !width || !height) {
    return -1;
  }

  // Check if message starts with SIZE_MESSAGE_PREFIX
  if (strncmp(message, SIZE_MESSAGE_PREFIX, strlen(SIZE_MESSAGE_PREFIX)) != 0) {
    return -1;
  }

  // Parse the width,height values
  int w, h;
  int parsed = sscanf(message, SIZE_MESSAGE_FORMAT, &w, &h);
  if (parsed != 2 || w <= 0 || h <= 0 || w > 65535 || h > 65535) {
    return -1;
  }

  *width = (unsigned short)w;
  *height = (unsigned short)h;
  return 0;
}