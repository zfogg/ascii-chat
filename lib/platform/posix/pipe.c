/**
 * @file platform/posix/pipe.c
 * @ingroup platform
 * @brief 🔌 POSIX pipe/agent socket implementation using Unix domain sockets
 */

#ifndef _WIN32

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "common.h"
#include "platform/pipe.h"

pipe_t pipe_connect(const char *path) {
  if (!path) {
    return INVALID_PIPE_VALUE;
  }

  // Create Unix domain socket
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    log_debug("Failed to create Unix domain socket: %s", SAFE_STRERROR(errno));
    return INVALID_PIPE_VALUE;
  }

  // Set up Unix socket address
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  SAFE_STRNCPY(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  // Connect to the socket
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    log_debug("Failed to connect to Unix socket %s: %s", path, SAFE_STRERROR(errno));
    close(sock);
    return INVALID_PIPE_VALUE;
  }

  log_debug("Connected to agent via Unix domain socket: %s", path);
  return sock;
}

int pipe_close(pipe_t pipe) {
  if (pipe == INVALID_PIPE_VALUE) {
    return 0;
  }
  return close(pipe);
}

ssize_t pipe_read(pipe_t pipe, void *buf, size_t len) {
  if (pipe == INVALID_PIPE_VALUE || !buf || len == 0) {
    return -1;
  }

  ssize_t result = read(pipe, buf, len);
  if (result < 0) {
    log_debug("Failed to read from pipe: %s", SAFE_STRERROR(errno));
  }
  return result;
}

ssize_t pipe_write(pipe_t pipe, const void *buf, size_t len) {
  if (pipe == INVALID_PIPE_VALUE || !buf || len == 0) {
    return -1;
  }

  ssize_t result = write(pipe, buf, len);
  if (result < 0) {
    log_debug("Failed to write to pipe: %s", SAFE_STRERROR(errno));
  }
  return result;
}

bool pipe_is_valid(pipe_t pipe) {
  return pipe != INVALID_PIPE_VALUE;
}

#endif // !_WIN32
