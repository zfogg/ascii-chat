#include "network.h"
#include "common.h"
#include "buffer_pool.h"
#include "crc32_hw.h"
#include "terminal_detect.h"
#include <stdint.h>
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
#include "options.h"

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

#ifdef __APPLE__
  /* macOS: TCP_KEEPALIVE sets the idle time (in seconds) before sending probes */
  {
    int keepalive_idle = KEEPALIVE_IDLE;
    (void)setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, &keepalive_idle, sizeof(keepalive_idle));
    /* Interval/count tuning is not available via setsockopt on macOS; client/server PINGs handle liveness. */
  }
#endif

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

bool connect_with_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen,
                          int timeout_seconds) { // NOLINT(bugprone-easily-swappable-parameters)
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
#ifdef MSG_NOSIGNAL
    ssize_t sent = send(sockfd, data + total_sent, len - total_sent, MSG_NOSIGNAL);
#else
    // macOS doesn't have MSG_NOSIGNAL, but we ignore SIGPIPE signal instead
    ssize_t sent = send(sockfd, data + total_sent, len - total_sent, 0);
#endif
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // Try again
      }
      if (errno == EPIPE) {
        // Connection closed by peer
        log_debug("Connection closed by peer (EPIPE)");
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

// CRC32 implementation moved to crc32_hw.c for hardware acceleration

int send_packet(int sockfd, packet_type_t type, const void *data, size_t len) {
  if (len > MAX_PACKET_SIZE) {
    log_error("Packet too large: %zu > %d", len, MAX_PACKET_SIZE);
    return -1;
  }

  packet_header_t header = {.magic = htonl(PACKET_MAGIC),
                            .type = htons((uint16_t)type),
                            .length = htonl((uint32_t)len),
                            .crc32 = htonl(len > 0 ? asciichat_crc32(data, len) : 0),
                            .client_id = htonl(0)}; // Always initialize client_id to 0 in network byte order

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
  log_debug("Sent packet type=%d, len=%zu", type, len);
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

  // First validate packet length BEFORE converting from network byte order
  // This prevents potential integer overflow issues
  uint32_t pkt_len_network = header.length;
  if (pkt_len_network == 0xFFFFFFFF) {
    log_error("Invalid packet length in network byte order: 0xFFFFFFFF");
    return -1;
  }

  // Convert from network byte order
  uint32_t magic = ntohl(header.magic);
  uint16_t pkt_type = ntohs(header.type);
  uint32_t pkt_len = ntohl(pkt_len_network);
  uint32_t expected_crc = ntohl(header.crc32);

  // Validate magic
  if (magic != PACKET_MAGIC) {
    log_error("Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
    return -1;
  }

  // Validate packet size with bounds checking
  if (pkt_len > MAX_PACKET_SIZE) {
    log_error("Packet too large: %u > %d", pkt_len, MAX_PACKET_SIZE);
    return -1;
  }

  // Validate packet type and size constraints
  switch (pkt_type) {
  case PACKET_TYPE_ASCII_FRAME:
    // ASCII frame contains header + frame data
    if (pkt_len < sizeof(ascii_frame_packet_t)) {
      log_error("Invalid ASCII frame packet size: %u, minimum %zu", pkt_len, sizeof(ascii_frame_packet_t));
      return -1;
    }
    break;
  case PACKET_TYPE_IMAGE_FRAME:
    // Image frame contains header + pixel data
    if (pkt_len < sizeof(image_frame_packet_t)) {
      log_error("Invalid image frame packet size: %u, minimum %zu", pkt_len, sizeof(image_frame_packet_t));
      return -1;
    }
    break;
  case PACKET_TYPE_AUDIO:
    if (pkt_len == 0 || pkt_len > AUDIO_SAMPLES_PER_PACKET * sizeof(float) * 2) { // Max stereo samples
      log_error("Invalid audio packet size: %u", pkt_len);
      return -1;
    }
    break;
  case PACKET_TYPE_AUDIO_BATCH:
    // Batch must have at least header + some samples
    if (pkt_len < sizeof(audio_batch_packet_t) + sizeof(float)) {
      log_error("Invalid audio batch packet size: %u", pkt_len);
      return -1;
    }
    break;
  case PACKET_TYPE_PING:
  case PACKET_TYPE_PONG:
    if (pkt_len > 64) { // Ping/pong shouldn't be large
      log_error("Invalid ping/pong packet size: %u", pkt_len);
      return -1;
    }
    break;
  case PACKET_TYPE_CLIENT_JOIN:
    if (pkt_len != sizeof(client_info_packet_t)) {
      log_error("Invalid client join packet size: %u, expected %zu", pkt_len, sizeof(client_info_packet_t));
      return -1;
    }
    break;
  case PACKET_TYPE_CLIENT_LEAVE:
    // Client leave packet can be empty or contain reason
    if (pkt_len > 256) {
      log_error("Invalid client leave packet size: %u", pkt_len);
      return -1;
    }
    break;
  case PACKET_TYPE_STREAM_START:
  case PACKET_TYPE_STREAM_STOP:
    if (pkt_len != sizeof(uint32_t)) {
      log_error("Invalid stream control packet size: %u, expected %zu", pkt_len, sizeof(uint32_t));
      return -1;
    }
    break;
  case PACKET_TYPE_CLEAR_CONSOLE:
    // Clear console packet should be empty
    if (pkt_len != 0) {
      log_error("Invalid clear console packet size: %u, expected 0", pkt_len);
      return -1;
    }
    break;
  case PACKET_TYPE_SERVER_STATE:
    // Server state packet has fixed size
    if (pkt_len != sizeof(server_state_packet_t)) {
      log_error("Invalid server state packet size: %u, expected %zu", pkt_len, sizeof(server_state_packet_t));
      return -1;
    }
    break;
  case PACKET_TYPE_CLIENT_CAPABILITIES:
    // Terminal capabilities packet has fixed size
    if (pkt_len != sizeof(terminal_capabilities_packet_t)) {
      log_error("Invalid terminal capabilities packet size: %u, expected %zu", pkt_len,
                sizeof(terminal_capabilities_packet_t));
      return -1;
    }
    break;
  default:
    log_error("Unknown packet type: %u", pkt_type);
    return -1;
  }

  // Additional safety: don't allow zero-sized allocations for types that should have data
  if (pkt_len == 0 && (pkt_type == PACKET_TYPE_AUDIO || pkt_type == PACKET_TYPE_CLIENT_CAPABILITIES ||
                       pkt_type == PACKET_TYPE_AUDIO_BATCH)) {
    log_error("Zero-sized packet for type that requires data: %u", pkt_type);
    return -1;
  }

  *type = (packet_type_t)pkt_type;
  *len = pkt_len;

  // Allocate and read payload
  if (pkt_len > 0) {
    // Additional safety check before allocation (uint32_t max is safe)
    if (pkt_len > 0xFFFFFFFFU / 2) {
      log_error("Packet size too large for safe allocation: %u", pkt_len);
      return -1;
    }

    // Try to allocate from global buffer pool first
    *data = buffer_pool_alloc(pkt_len);
    if (!*data) {
      log_error("Failed to allocate %u bytes for packet", pkt_len);
      return -1;
    }

    received = recv_with_timeout(sockfd, *data, pkt_len, RECV_TIMEOUT);
    if (received != (ssize_t)pkt_len) {
      log_error("Failed to receive complete packet payload: %zd/%u bytes", received, pkt_len);
      buffer_pool_free(*data, pkt_len);
      *data = NULL;
      return -1;
    }

    // Check if we accidentally received another packet header as payload
    if (pkt_len >= 4) {
      uint32_t first_word = *(uint32_t *)*data;
      if (first_word == 0xEFBEADDE) { // DEADBEEF in little-endian
        log_error("CRITICAL: Received packet header (DEADBEEF) as payload data! Stream is desynchronized!");
        log_error("This packet was supposed to be type %u with %u bytes", pkt_type, pkt_len);
        // Try to recover by treating this as a new packet...
      }
    }

    // Verify checksum
    uint32_t actual_crc = asciichat_crc32(*data, pkt_len);
    if (actual_crc != expected_crc) {
      // Debug: log first few bytes of data
      if (pkt_type == PACKET_TYPE_AUDIO) {
        unsigned char *bytes = (unsigned char *)*data;
        log_debug(
            "Packet type %u first 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
            "%02x %02x",
            pkt_type, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
            bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
      }
      log_error("Packet checksum mismatch for type %u: got 0x%x, expected 0x%x (len=%u)", pkt_type, actual_crc,
                expected_crc, pkt_len);
      buffer_pool_free(*data, pkt_len);
      *data = NULL;
      return -1;
    }
  } else {
    *data = NULL;
  }

#ifdef NETWORK_DEBUG
  log_debug("Received packet type=%d, len=%zu", *type, *len);
#endif
  return 1;
}

int send_audio_packet(int sockfd, const float *samples, int num_samples) {
  if (!samples || num_samples <= 0 || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_error("Invalid audio packet: samples=%p, num_samples=%d", samples, num_samples);
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);

// Debug: log CRC of audio data being sent
#ifdef AUDIO_DEBUG
  uint32_t crc = asciichat_crc32(samples, data_size);
  log_debug("Sending audio packet: %d samples, %zu bytes, CRC=0x%x", num_samples, data_size, crc);
#endif

  return send_packet(sockfd, PACKET_TYPE_AUDIO, samples, data_size);
}

int send_audio_batch_packet(int sockfd, const float *samples, int num_samples, int batch_count) {
  if (!samples || num_samples <= 0 || batch_count <= 0) {
    log_error("Invalid audio batch: samples=%p, num_samples=%d, batch_count=%d", samples, num_samples, batch_count);
    return -1;
  }

  // Build batch header
  audio_batch_packet_t header;
  header.batch_count = htonl(batch_count);
  header.total_samples = htonl(num_samples);
  header.sample_rate = htonl(44100); // Could make this configurable
  header.channels = htonl(1);        // Mono for now

  // Calculate total payload size
  size_t header_size = sizeof(audio_batch_packet_t);
  size_t samples_size = num_samples * sizeof(float);
  size_t total_size = header_size + samples_size;

  // Allocate buffer for header + samples
  uint8_t *buffer;
  SAFE_MALLOC(buffer, total_size, uint8_t *);

  // Copy header and samples to buffer
  memcpy(buffer, &header, header_size);
  memcpy(buffer + header_size, samples, samples_size);

#ifdef AUDIO_DEBUG
  log_debug("Sending audio batch: %d chunks, %d total samples, %zu bytes", batch_count, num_samples, total_size);
#endif

  int result = send_packet(sockfd, PACKET_TYPE_AUDIO_BATCH, buffer, total_size);
  free(buffer);
  return result;
}

// Multi-user protocol functions implementation

int send_client_join_packet(int sockfd, const char *display_name, uint32_t capabilities) {
  client_info_packet_t join_packet;
  memset(&join_packet, 0, sizeof(join_packet));

  join_packet.client_id = 0; // Will be assigned by server
  snprintf(join_packet.display_name, MAX_DISPLAY_NAME_LEN, "%s", display_name ? display_name : "Unknown");
  join_packet.capabilities = capabilities;

  return send_packet(sockfd, PACKET_TYPE_CLIENT_JOIN, &join_packet, sizeof(join_packet));
}

int send_client_leave_packet(int sockfd, uint32_t client_id) {
  uint32_t id_data = htonl(client_id);
  return send_packet(sockfd, PACKET_TYPE_CLIENT_LEAVE, &id_data, sizeof(id_data));
}

int send_stream_start_packet(int sockfd, uint32_t stream_type) {
  uint32_t type_data = htonl(stream_type);
  return send_packet(sockfd, PACKET_TYPE_STREAM_START, &type_data, sizeof(type_data));
}

int send_stream_stop_packet(int sockfd, uint32_t stream_type) {
  uint32_t type_data = htonl(stream_type);
  return send_packet(sockfd, PACKET_TYPE_STREAM_STOP, &type_data, sizeof(type_data));
}

int send_packet_from_client(int sockfd, packet_type_t type, uint32_t client_id, const void *data, size_t len) {
  // Enhanced version of send_packet that includes client_id
  packet_header_t header;

  // Build header
  header.magic = htonl(PACKET_MAGIC);
  header.type = htons((uint16_t)type);
  header.length = htonl((uint32_t)len);
  header.client_id = htonl(client_id);

  // Calculate CRC for payload
  uint32_t crc = 0;
  if (len > 0 && data) {
    crc = asciichat_crc32(data, len);
  }
  header.crc32 = htonl(crc);

  // Send header
  ssize_t sent = send_with_timeout(sockfd, &header, sizeof(header), SEND_TIMEOUT);
  if (sent != sizeof(header)) {
    log_error("Failed to send packet header with client ID: %zd/%zu bytes", sent, sizeof(header));
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

  return 0;
}

int receive_packet_with_client(int sockfd, packet_type_t *type, uint32_t *client_id, void **data, size_t *len) {
  if (!type || !client_id || !data || !len) {
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

  // First validate packet length BEFORE converting from network byte order
  // This prevents potential integer overflow issues
  uint32_t pkt_len_network = header.length;
  if (pkt_len_network == 0xFFFFFFFF) {
    log_error("Invalid packet length in network byte order: 0xFFFFFFFF");
    return -1;
  }

  // Convert from network byte order
  uint32_t magic = ntohl(header.magic);
  uint16_t pkt_type = ntohs(header.type);
  uint32_t pkt_len = ntohl(pkt_len_network);
  uint32_t expected_crc = ntohl(header.crc32);
  uint32_t pkt_client_id = ntohl(header.client_id);

  // Validate magic
  if (magic != PACKET_MAGIC) {
    log_error("Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
    return -1;
  }

  // Validate length with bounds checking
  if (pkt_len > MAX_PACKET_SIZE) {
    log_error("Packet too large: %u bytes (max %d)", pkt_len, MAX_PACKET_SIZE);
    return -1;
  }

  // Read payload if present
  *data = NULL;
  if (pkt_len > 0) {
    // Try to allocate from global buffer pool first
    *data = buffer_pool_alloc(pkt_len);
    if (!*data) {
      log_error("Failed to allocate %u bytes for packet", pkt_len);
      return -1;
    }

    received = recv_with_timeout(sockfd, *data, pkt_len, RECV_TIMEOUT);
    if (received != (ssize_t)pkt_len) {
      log_error("Failed to receive packet payload: %zd/%u bytes", received, pkt_len);
      buffer_pool_free(*data, pkt_len);
      *data = NULL;
      return -1;
    }

    // Verify CRC
    uint32_t actual_crc = asciichat_crc32(*data, pkt_len);
    if (actual_crc != expected_crc) {
      log_error("Packet CRC mismatch for type %u from client %u: got 0x%x, expected 0x%x (len=%u)", pkt_type,
                pkt_client_id, actual_crc, expected_crc, pkt_len);
      buffer_pool_free(*data, pkt_len);
      *data = NULL;
      return -1;
    }
  }

  *type = (packet_type_t)pkt_type;
  *client_id = pkt_client_id;
  *len = pkt_len;

  return 1;
}

int send_ping_packet(int sockfd) {
  return send_packet(sockfd, PACKET_TYPE_PING, NULL, 0);
}

int send_pong_packet(int sockfd) {
  return send_packet(sockfd, PACKET_TYPE_PONG, NULL, 0);
}

int send_clear_console_packet(int sockfd) {
  return send_packet(sockfd, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0);
}

int send_server_state_packet(int sockfd, const server_state_packet_t *state) {
  if (!state) {
    return -1;
  }

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = htonl(state->connected_client_count);
  net_state.active_client_count = htonl(state->active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  return send_packet(sockfd, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state));
}

int send_terminal_capabilities_packet(int sockfd, const terminal_capabilities_packet_t *caps) {
  if (!caps) {
    return -1;
  }

  // Convert to network byte order
  terminal_capabilities_packet_t net_caps;
  net_caps.capabilities = htonl(caps->capabilities);
  net_caps.color_level = htonl(caps->color_level);
  net_caps.color_count = htonl(caps->color_count);
  net_caps.width = htons(caps->width);
  net_caps.height = htons(caps->height);

  // Copy strings directly (no byte order conversion needed)
  memcpy(net_caps.term_type, caps->term_type, sizeof(net_caps.term_type));
  memcpy(net_caps.colorterm, caps->colorterm, sizeof(net_caps.colorterm));

  net_caps.detection_reliable = caps->detection_reliable;
  memset(net_caps.reserved, 0, sizeof(net_caps.reserved));

  return send_packet(sockfd, PACKET_TYPE_CLIENT_CAPABILITIES, &net_caps, sizeof(net_caps));
}

// Convenience function that sends terminal capabilities with auto-detection
// This provides a drop-in replacement for send_size_packet
int send_terminal_size_with_auto_detect(int sockfd, unsigned short width, unsigned short height) {
  // Detect terminal capabilities automatically
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Apply user's color mode override
  caps = apply_color_mode_override(caps);

  // Check if detection was reliable, use fallback if not
  if (!caps.detection_reliable) {
    log_warn("Terminal capability detection not reliable, using fallback");
    // Use minimal fallback capabilities
    memset(&caps, 0, sizeof(caps));
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities = 0; // No special capabilities
    strncpy(caps.term_type, "unknown", sizeof(caps.term_type) - 1);
    strncpy(caps.colorterm, "", sizeof(caps.colorterm) - 1);
    caps.detection_reliable = 0; // Not reliable
  }

  // Convert to network packet format
  terminal_capabilities_packet_t net_packet;
  net_packet.capabilities = caps.capabilities;
  net_packet.color_level = caps.color_level;
  net_packet.color_count = caps.color_count;
  net_packet.width = width;
  net_packet.height = height;

  // log_debug("NETWORK DEBUG: About to send capabilities packet with width=%u, height=%u", width, height);

  strncpy(net_packet.term_type, caps.term_type, sizeof(net_packet.term_type) - 1);
  net_packet.term_type[sizeof(net_packet.term_type) - 1] = '\0';

  strncpy(net_packet.colorterm, caps.colorterm, sizeof(net_packet.colorterm) - 1);
  net_packet.colorterm[sizeof(net_packet.colorterm) - 1] = '\0';

  net_packet.detection_reliable = caps.detection_reliable;
  memset(net_packet.reserved, 0, sizeof(net_packet.reserved));

  return send_terminal_capabilities_packet(sockfd, &net_packet);
}
