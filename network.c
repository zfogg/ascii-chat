#include "network.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <pthread.h>
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
  setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
  // Not critical, continue if fail.
#endif

#ifdef TCP_KEEPINTVL
  int keepintvl = KEEPALIVE_INTERVAL;
  setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
  // Not critical, continue if fail.
#endif

#ifdef TCP_KEEPCNT
  int keepcnt = KEEPALIVE_COUNT;
  setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
  // Not critical, continue if fail.
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
    if (errno == EINTR) {
      return false; // Interrupted by signal
    }
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

    // The first argument to select() should be set to the highest-numbered file descriptor in any of the fd_sets,
    // plus 1. This is because select() internally checks all file descriptors from 0 up to (but not including) this
    // value to see if they are ready. An fd_set is a data structure used by select() to represent a set of file
    // descriptors (such as sockets or files) to be monitored for readiness (read, write, or error). In this case,
    // sockfd is the only file descriptor in the write_fds set, so we pass sockfd + 1.
    int result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
    if (result <= 0) {
      // Timeout or error
      if (result == 0) {
        errno = ETIMEDOUT;
      } else if (errno == EINTR) {
        // Interrupted by signal
        return -1;
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
  ssize_t total_received = 0;
  char *data = (char *)buf;

  while (total_received < (ssize_t)len) {
    // Set up select for read timeout
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    int result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    if (result <= 0) {
      // Timeout or error
      if (result == 0) {
        errno = ETIMEDOUT;
      } else if (errno == EINTR) {
        // Interrupted by signal
        return -1;
      }
      return -1;
    }

    // Try to receive data
    ssize_t received = recv(sockfd, data + total_received, len - total_received, 0);
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // Try again
      }
      return -1; // Real error
    } else if (received == 0) {
      // Connection closed
      return total_received;
    }

    total_received += received;
  }

  return total_received;
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
    } else if (errno == EINTR) {
      // Interrupted by signal - this is expected during shutdown
      return -1;
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

int send_audio_data(int sockfd, const float *samples, int num_samples) {
  if (!samples || num_samples <= 0 || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    return -1;
  }

  char header[AUDIO_MESSAGE_MAX_LEN];
  int header_len = snprintf(header, sizeof(header), AUDIO_MESSAGE_FORMAT, num_samples);

  if (header_len >= (int)sizeof(header)) {
    return -1;
  }

  ssize_t sent = send_with_timeout(sockfd, header, header_len, SEND_TIMEOUT);
  if (sent != header_len) {
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);
  sent = send_with_timeout(sockfd, samples, data_size, SEND_TIMEOUT);
  if (sent != (ssize_t)data_size) {
    return -1;
  }

  return 0;
}

int receive_audio_data(int sockfd, float *samples, int max_samples) {
  if (!samples || max_samples <= 0) {
    return -1;
  }

  char header[AUDIO_MESSAGE_MAX_LEN];
  ssize_t received = recv_with_timeout(sockfd, header, sizeof(header) - 1, RECV_TIMEOUT);
  if (received <= 0) {
    return -1;
  }

  header[received] = '\0';

  if (strncmp(header, AUDIO_MESSAGE_PREFIX, strlen(AUDIO_MESSAGE_PREFIX)) != 0) {
    return -1;
  }

  int num_samples;
  int parsed = sscanf(header, AUDIO_MESSAGE_FORMAT, &num_samples);
  if (parsed != 1 || num_samples <= 0 || num_samples > max_samples || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);
  received = recv_with_timeout(sockfd, samples, data_size, RECV_TIMEOUT);
  if (received != (ssize_t)data_size) {
    return -1;
  }

  return num_samples;
}

/* ============================================================================
 * Packet Protocol Implementation
 * ============================================================================
 */

// Simple CRC32 implementation
uint32_t asciichat_crc32(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }

  return ~crc;
}

// Global sequence counter (thread-safe)
static uint32_t g_sequence_counter = 0;
static pthread_mutex_t g_sequence_mutex = PTHREAD_MUTEX_INITIALIZER;

uint32_t get_next_sequence(void) {
  pthread_mutex_lock(&g_sequence_mutex);
  uint32_t seq = ++g_sequence_counter;
  pthread_mutex_unlock(&g_sequence_mutex);
  return seq;
}

int send_packet(int sockfd, packet_type_t type, const void *data, size_t len) {
  if (len > MAX_PACKET_SIZE) {
    log_error("Packet too large: %zu > %d", len, MAX_PACKET_SIZE);
    return -1;
  }

  packet_header_t header = {.magic = htonl(PACKET_MAGIC),
                            .type = htons((uint16_t)type),
                            .length = htonl((uint32_t)len),
                            .sequence = htonl(get_next_sequence()),
                            .crc32 = htonl(len > 0 ? asciichat_crc32(data, len) : 0)};

  // Send header first
  ssize_t sent = send_with_timeout(sockfd, &header, sizeof(header), SEND_TIMEOUT);
  if (sent != sizeof(header)) {
    log_error("Failed to send packet header: %zd/%zu bytes", sent, sizeof(header));
    return -1;
  }

  // Send payload if present
  if (len > 0 && data) {
    sent = send_with_timeout(sockfd, data, len, SEND_TIMEOUT);
    if (sent != (ssize_t)len) {
      log_error("Failed to send packet payload: %zd/%zu bytes", sent, len);
      return -1;
    }
  }

#ifdef NETWORK_DEBUG
  log_debug("Sent packet type=%d, len=%zu, seq=%u", type, len, ntohl(header.sequence));
#endif
  return 0;
}

int receive_packet(int sockfd, packet_type_t *type, void **data, size_t *len) {
  if (!type || !data || !len) {
    return -1;
  }

  packet_header_t header;

  // Read header
  ssize_t received = recv_with_timeout(sockfd, &header, sizeof(header), RECV_TIMEOUT);
  if (received != sizeof(header)) {
    if (received == 0) {
      log_info("Connection closed while reading packet header");
    } else if (received > 0) {
      log_error("Partial packet header received: %zd/%zu bytes", received, sizeof(header));
    }
    return received == 0 ? 0 : -1;
  }

  // Convert from network byte order
  uint32_t magic = ntohl(header.magic);
  uint16_t pkt_type = ntohs(header.type);
  uint32_t pkt_len = ntohl(header.length);
#ifdef NETWORK_DEBUG
  uint32_t sequence = ntohl(header.sequence);
#endif
  uint32_t expected_crc = ntohl(header.crc32);

  // Validate magic
  if (magic != PACKET_MAGIC) {
    log_error("Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
    return -1;
  }

  // Validate packet size
  if (pkt_len > MAX_PACKET_SIZE) {
    log_error("Packet too large: %u > %d", pkt_len, MAX_PACKET_SIZE);
    return -1;
  }

  *type = (packet_type_t)pkt_type;
  *len = pkt_len;

  // Allocate and read payload
  if (pkt_len > 0) {
    SAFE_MALLOC(*data, pkt_len, void *);
    if (!*data) {
      log_error("Failed to allocate %u bytes for packet payload", pkt_len);
      return -1;
    }

    received = recv_with_timeout(sockfd, *data, pkt_len, RECV_TIMEOUT);
    if (received != (ssize_t)pkt_len) {
      log_error("Failed to receive complete packet payload: %zd/%u bytes", received, pkt_len);
      free(*data);
      *data = NULL;
      return -1;
    }

    // Verify checksum
    uint32_t actual_crc = asciichat_crc32(*data, pkt_len);
    if (actual_crc != expected_crc) {
      log_error("Packet checksum mismatch: 0x%x != 0x%x", actual_crc, expected_crc);
      free(*data);
      *data = NULL;
      return -1;
    }
  } else {
    *data = NULL;
  }

#ifdef NETWORK_DEBUG
  log_debug("Received packet type=%d, len=%zu, seq=%u", *type, *len, sequence);
#endif
  return 1;
}

int send_video_header_packet(int sockfd, const void *header_data, size_t header_len) {
  return send_packet(sockfd, PACKET_TYPE_VIDEO_HEADER, header_data, header_len);
}

int send_video_packet(int sockfd, const void *frame_data, size_t frame_len) {
  return send_packet(sockfd, PACKET_TYPE_VIDEO, frame_data, frame_len);
}

int send_audio_packet(int sockfd, const float *samples, int num_samples) {
  if (!samples || num_samples <= 0 || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);
  return send_packet(sockfd, PACKET_TYPE_AUDIO, samples, data_size);
}

int send_size_packet(int sockfd, unsigned short width, unsigned short height) {
  struct {
    uint16_t width;
    uint16_t height;
  } size_data = {.width = htons(width), .height = htons(height)};

  return send_packet(sockfd, PACKET_TYPE_SIZE, &size_data, sizeof(size_data));
}