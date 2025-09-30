#include "network.h"
#include "common.h"
#include "buffer_pool.h"
#include "crc32_hw.h"
#include "platform/terminal.h"
#include <stdint.h>
#include <errno.h>
#include "platform/abstraction.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "options.h"

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1
// Check if we're in a test environment
static int is_test_environment(void) {
  return SAFE_GETENV("CRITERION_TEST") != NULL || SAFE_GETENV("TESTING") != NULL;
}

int set_socket_timeout(socket_t sockfd, int timeout_seconds) {
  struct timeval timeout;
  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    return -1;
  }

  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    return -1;
  }

  return 0;
}

int set_socket_keepalive(socket_t sockfd) {
  return socket_set_keepalive_params(sockfd, true, KEEPALIVE_IDLE, KEEPALIVE_INTERVAL, KEEPALIVE_COUNT);
}

int set_socket_nonblocking(socket_t sockfd) {
  return socket_set_nonblocking(sockfd, true);
}

bool connect_with_timeout(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen,
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

  // Check if socket operation would block (non-blocking connect in progress)
  int last_error = socket_get_last_error();
  if (last_error != SOCKET_ERROR_INPROGRESS && last_error != SOCKET_ERROR_WOULDBLOCK) {
    return false;
  }

  // Wait for connection to complete with timeout
  fd_set write_fds;
  struct timeval timeout;

  socket_fd_zero(&write_fds);
  socket_fd_set(sockfd, &write_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  result = socket_select(sockfd, NULL, &write_fds, NULL, &timeout);
  if (result <= 0) {
    if (errno == EINTR) {
      return false; // Interrupted by signal
    }
    return false; // Timeout or error
  }

  // Check if connection was successful
  int error = 0;
  socklen_t len = sizeof(error);
  if (socket_getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
    return false;
  }

  if (error != 0) {
    return false;
  }

  // Connection successful - set socket back to blocking mode
  if (socket_set_nonblocking(sockfd, false) < 0) {
    log_warn("Failed to set socket back to blocking mode after connect");
    // Continue anyway, as the connection succeeded
  }

  return true;
}

ssize_t send_with_timeout(socket_t sockfd, const void *buf, size_t len, int timeout_seconds) {

  fd_set write_fds;
  struct timeval timeout;
  ssize_t total_sent = 0;
  const char *data = (const char *)buf;
  time_t start_time = time(NULL);

  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }

  while (total_sent < (ssize_t)len) {
    // Calculate remaining timeout time
    time_t current_time = time(NULL);
    int elapsed_seconds = (int)(current_time - start_time);
    int remaining_timeout = timeout_seconds - elapsed_seconds;

    if (remaining_timeout <= 0) {
      errno = ETIMEDOUT;
      log_error("send_with_timeout: total timeout exceeded (%d seconds) - elapsed=%d, remaining=%d", timeout_seconds,
                elapsed_seconds, remaining_timeout);
      return -1;
    }

    // Set up select for write timeout
    socket_fd_zero(&write_fds);
    socket_fd_set(sockfd, &write_fds);

    timeout.tv_sec = remaining_timeout;
    timeout.tv_usec = 0;

    // Use platform-abstracted select wrapper that handles Windows/POSIX differences
    int result = socket_select(sockfd, NULL, &write_fds, NULL, &timeout);
    if (result <= 0) {
      // Timeout or error
      if (result == 0) {
        errno = ETIMEDOUT;
        log_error("send_with_timeout: select timeout - socket not writable after %d seconds (sent %zd/%zu bytes)",
                  remaining_timeout, total_sent, len);
      } else if (errno == EINTR) {
        // Interrupted by signal
        log_debug("send_with_timeout: select interrupted");
        return -1;
      } else {
        log_error("send_with_timeout: select failed with errno=%d", errno);
      }
      return -1;
    }

    // Try to send data with reasonable chunking
    // Limit chunk size to 64KB to avoid TCP issues and ensure data actually gets sent
    size_t bytes_to_send = len - total_sent;
    const size_t MAX_CHUNK_SIZE = 65536; // 64KB chunks for reliable TCP transmission
    if (bytes_to_send > MAX_CHUNK_SIZE) {
      bytes_to_send = MAX_CHUNK_SIZE;
    }

#ifdef _WIN32
    // Windows send() expects int for length and const char* for buffer
    if (bytes_to_send > INT_MAX) {
      bytes_to_send = INT_MAX;
    }
    int raw_sent = send(sockfd, (const char *)(data + total_sent), (int)bytes_to_send, 0);
    // CRITICAL FIX: Check for SOCKET_ERROR before casting to avoid corruption
    ssize_t sent;
    if (raw_sent == SOCKET_ERROR) {
      sent = -1;
      // On Windows, use WSAGetLastError() instead of errno
      errno = WSAGetLastError();
      log_debug("Windows send() failed: WSAGetLastError=%d, sockfd=%llu, bytes_to_send=%zu", errno,
                (unsigned long long)sockfd, bytes_to_send);
    } else {
      sent = (ssize_t)raw_sent;
      // CORRUPTION DETECTION: Check Windows send() return value
      if (raw_sent > (int)bytes_to_send) {
        log_error("CRITICAL: Windows send() returned more than requested: raw_sent=%d > bytes_to_send=%zu", raw_sent,
                  bytes_to_send);
      }
    }

#elif defined(MSG_NOSIGNAL)
    ssize_t sent = send(sockfd, data + total_sent, bytes_to_send, MSG_NOSIGNAL);
#else
    // macOS doesn't have MSG_NOSIGNAL, but we ignore SIGPIPE signal instead
    ssize_t sent = send(sockfd, data + total_sent, bytes_to_send, 0);
#endif
    if (sent < 0) {
      int error = errno;
      if (error == EAGAIN || error == EWOULDBLOCK) {
        log_debug("send_with_timeout: would block, continuing");
        continue; // Try again
      }
      if (error == EPIPE) {
        // Connection closed by peer
        log_debug("Connection closed by peer (EPIPE)");
      }
      log_error("send_with_timeout: send failed with errno=%d (%s)", error, SAFE_STRERROR(error));
      return -1; // Real error
    }

    total_sent += sent;
  }

  return total_sent;
}

ssize_t recv_with_timeout(socket_t sockfd, void *buf, size_t len, int timeout_seconds) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }
  fd_set read_fds;
  struct timeval timeout;
  ssize_t total_received = 0;
  char *data = (char *)buf;

  while (total_received < (ssize_t)len) {
    // Set up select for read timeout
    socket_fd_zero(&read_fds);
    socket_fd_set(sockfd, &read_fds);

    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    int result = socket_select(sockfd, &read_fds, NULL, NULL, &timeout);
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
    }
    if (received == 0) {
      // Connection closed
      return total_received;
    }

    total_received += received;
  }

  return total_received;
}

int accept_with_timeout(socket_t listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds) {
  printf("DEBUG: accept_with_timeout ENTER - listenfd=%d, timeout=%d\n", (int)listenfd, timeout_seconds);
  fflush(stdout);

  fd_set read_fds;
  struct timeval timeout;

  socket_fd_zero(&read_fds);
  socket_fd_set(listenfd, &read_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  printf("DEBUG: Calling socket_select...\n");
  fflush(stdout);
  int result = socket_select(listenfd, &read_fds, NULL, NULL, &timeout);
  printf("DEBUG: socket_select returned %d\n", result);
  fflush(stdout);

  if (result <= 0) {
    // Timeout or error
    if (result == 0) {
      errno = ETIMEDOUT;
      printf("DEBUG: socket_select timed out\n");
      fflush(stdout);
    } else if (errno == EINTR) {
      // Interrupted by signal - this is expected during shutdown
      printf("DEBUG: socket_select interrupted by signal\n");
      fflush(stdout);
      return -1;
    } else {
#ifdef _WIN32
      int wsa_error = WSAGetLastError();
      printf("DEBUG: socket_select error - result=%d, WSAError=%d\n", result, wsa_error);
      fflush(stdout);
#else
      printf("DEBUG: socket_select error - result=%d, errno=%d\n", result, errno);
      fflush(stdout);
#endif
    }
    return -1;
  }

  printf("DEBUG: Calling accept...\n");
  fflush(stdout);
  socket_t accept_result = accept(listenfd, addr, addrlen);
  printf("DEBUG: accept returned %lld\n", (long long)accept_result);
  fflush(stdout);

  // Properly handle INVALID_SOCKET on Windows
  if (accept_result == INVALID_SOCKET_VALUE) {
    return -1;
  }

  // Explicit cast is safe after checking for INVALID_SOCKET
  return (int)accept_result;
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
    return SAFE_STRERROR(error_code);
  }
}

/* ============================================================================
 * Size Communication Protocol
 * ============================================================================
 */

int send_size_message(socket_t sockfd, unsigned short width, unsigned short height) {
  char message[SIZE_MESSAGE_MAX_LEN];
  int len = SAFE_SNPRINTF(message, sizeof(message), SIZE_MESSAGE_FORMAT, width, height);

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

  // Parse the width,height values using safe parsing
  unsigned int w;
  unsigned int h;
  if (safe_parse_size_message(message, &w, &h) != 0) {
    return -1;
  }

  *width = (unsigned short)w;
  *height = (unsigned short)h;
  return 0;
}

int send_audio_data(socket_t sockfd, const float *samples, int num_samples) {
  if (!samples || num_samples <= 0 || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    return -1;
  }

  char header[AUDIO_MESSAGE_MAX_LEN];
  int header_len = SAFE_SNPRINTF(header, sizeof(header), AUDIO_MESSAGE_FORMAT, num_samples);

  if (header_len >= (int)sizeof(header)) {
    return -1;
  }

  ssize_t sent = send_with_timeout(sockfd, header, header_len, is_test_environment() ? 1 : SEND_TIMEOUT);
  if (sent != header_len) {
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);
  sent = send_with_timeout(sockfd, samples, data_size, is_test_environment() ? 1 : SEND_TIMEOUT);
  if (sent != (ssize_t)data_size) {
    return -1;
  }

  return 0;
}

int receive_audio_data(socket_t sockfd, float *samples, int max_samples) {
  if (!samples || max_samples <= 0) {
    return -1;
  }

  char header[AUDIO_MESSAGE_MAX_LEN];
  ssize_t received = recv_with_timeout(sockfd, header, sizeof(header) - 1, is_test_environment() ? 1 : RECV_TIMEOUT);
  if (received <= 0) {
    return -1;
  }

  header[received] = '\0';

  if (strncmp(header, AUDIO_MESSAGE_PREFIX, strlen(AUDIO_MESSAGE_PREFIX)) != 0) {
    return -1;
  }

  unsigned int num_samples;
  if (safe_parse_audio_message(header, &num_samples) != 0 || num_samples > (unsigned int)max_samples ||
      num_samples > AUDIO_SAMPLES_PER_PACKET) {
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);
  received = recv_with_timeout(sockfd, samples, data_size, is_test_environment() ? 1 : RECV_TIMEOUT);
  if (received != (ssize_t)data_size) {
    return -1;
  }

  return (int)num_samples;
}

/* ============================================================================
 * Packet Protocol Implementation
 * ============================================================================
 */

// CRC32 implementation moved to crc32_hw.c for hardware acceleration

/**
 * Calculate timeout based on packet size
 * Large packets need more time to transmit reliably
 */
static int calculate_packet_timeout(size_t packet_size) {
  int base_timeout = is_test_environment() ? 1 : SEND_TIMEOUT;

  // For large packets, increase timeout proportionally
  if (packet_size > 100000) { // 100KB threshold
    // Add 0.8 seconds per 1MB above the threshold
    int extra_timeout = (int)((packet_size - 100000) / 1000000.0 * 0.8) + 1;
    int total_timeout = base_timeout + extra_timeout;

    // Ensure client timeout is longer than server's RECV_TIMEOUT (30s) to prevent deadlock
    // Add 10 seconds buffer to account for server processing delays
    int min_timeout = 40; // 30s server timeout + 10s buffer
    if (total_timeout < min_timeout) {
      total_timeout = min_timeout;
    }

    // Cap at 60 seconds maximum
    return (total_timeout > 60) ? 60 : total_timeout;
  }

  return base_timeout;
}

int send_packet(socket_t sockfd, packet_type_t type, const void *data, size_t len) {
  if (len > MAX_PACKET_SIZE) {
    log_error("Packet too large: %zu > %d", len, MAX_PACKET_SIZE);
    return -1;
  }

  packet_header_t header = {.magic = htonl(PACKET_MAGIC),
                            .type = htons((uint16_t)type),
                            .length = htonl((uint32_t)len),
                            .crc32 = htonl(len > 0 ? asciichat_crc32(data, len) : 0),
                            .client_id = htonl(0)}; // Always initialize client_id to 0 in network byte order

  // Calculate timeout based on packet size
  int timeout = calculate_packet_timeout(len);
  if (len > 100000) { // Only log for large packets
    log_info("DEBUG_TIMEOUT: Packet size=%zu, calculated timeout=%d seconds", len, timeout);
  }

  // Send header first
  ssize_t sent = send_with_timeout(sockfd, &header, sizeof(header), timeout);
  // Check for error first to avoid signed/unsigned comparison issues
  if (sent < 0) {
    log_error("Failed to send packet header: %zd/%zu bytes, errno=%d (%s)", sent, sizeof(header), errno,
              SAFE_STRERROR(errno));
    return -1;
  }
  if ((size_t)sent != sizeof(header)) {
    log_error("Failed to send packet header: %zd/%zu bytes, errno=%d (%s)", sent, sizeof(header), errno,
              SAFE_STRERROR(errno));
    return -1;
  }

  // Send payload if present
  if (len > 0 && data) {
    sent = send_with_timeout(sockfd, data, len, timeout);
    // Check for error first to avoid signed/unsigned comparison issues
    if (sent < 0) {
      log_error("Failed to send packet payload: %zd/%zu bytes", sent, len);
      return -1;
    }
    if ((size_t)sent != len) {
      log_error("Failed to send packet payload: %zd/%zu bytes", sent, len);
      return -1;
    }
  }

#ifdef DEBUG_NETWORK
  log_debug("Sent packet type=%d, len=%zu", type, len);
#endif
  return 0;
}

int receive_packet(socket_t sockfd, packet_type_t *type, void **data, size_t *len) {
  if (!type || !data || !len) {
    return -1;
  }

  packet_header_t header;

  // Read header
  ssize_t received = recv_with_timeout(sockfd, &header, sizeof(header), is_test_environment() ? 1 : RECV_TIMEOUT);
  // Check for error first to avoid signed/unsigned comparison issues
  if (received < 0) {
    // recv_with_timeout returned an error
    return -1;
  }
  if ((size_t)received != sizeof(header)) {
    if (received == 0) {
      log_info("Connection closed while reading packet header");
    } else {
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

    received = recv_with_timeout(sockfd, *data, pkt_len, is_test_environment() ? 1 : RECV_TIMEOUT);
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

#ifdef DEBUG_NETWORK
  log_debug("Received packet type=%d, len=%zu", *type, *len);
#endif
  return 1;
}

int send_audio_packet(socket_t sockfd, const float *samples, int num_samples) {
  if (!samples || num_samples <= 0 || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_error("Invalid audio packet: samples=%p, num_samples=%d", samples, num_samples);
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);

// Debug: log CRC of audio data being sent
#ifdef DEBUG_AUDIO
  uint32_t crc = asciichat_crc32(samples, data_size);
  log_debug("Sending audio packet: %d samples, %zu bytes, CRC=0x%x", num_samples, data_size, crc);
#endif

  return send_packet(sockfd, PACKET_TYPE_AUDIO, samples, data_size);
}

int send_audio_batch_packet(socket_t sockfd, const float *samples, int num_samples, int batch_count) {
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
  SAFE_MEMCPY(buffer, total_size, &header, header_size);
  SAFE_MEMCPY(buffer + header_size, samples_size, samples, samples_size);

#ifdef DEBUG_AUDIO
  log_debug("Sending audio batch: %d chunks, %d total samples, %zu bytes", batch_count, num_samples, total_size);
#endif

  int result = send_packet(sockfd, PACKET_TYPE_AUDIO_BATCH, buffer, total_size);
  free(buffer);
  return result;
}

// Multi-user protocol functions implementation

int send_client_join_packet(socket_t sockfd, const char *display_name, uint32_t capabilities) {
  client_info_packet_t join_packet;
  SAFE_MEMSET(&join_packet, sizeof(join_packet), 0, sizeof(join_packet));

  join_packet.client_id = 0; // Will be assigned by server
  SAFE_SNPRINTF(join_packet.display_name, MAX_DISPLAY_NAME_LEN, "%s", display_name ? display_name : "Unknown");
  join_packet.capabilities = capabilities;

  return send_packet(sockfd, PACKET_TYPE_CLIENT_JOIN, &join_packet, sizeof(join_packet));
}

int send_client_leave_packet(socket_t sockfd, uint32_t client_id) {
  uint32_t id_data = htonl(client_id);
  return send_packet(sockfd, PACKET_TYPE_CLIENT_LEAVE, &id_data, sizeof(id_data));
}

int send_stream_start_packet(socket_t sockfd, uint32_t stream_type) {
  uint32_t type_data = htonl(stream_type);
  return send_packet(sockfd, PACKET_TYPE_STREAM_START, &type_data, sizeof(type_data));
}

int send_stream_stop_packet(socket_t sockfd, uint32_t stream_type) {
  uint32_t type_data = htonl(stream_type);
  return send_packet(sockfd, PACKET_TYPE_STREAM_STOP, &type_data, sizeof(type_data));
}

int send_packet_from_client(socket_t sockfd, packet_type_t type, uint32_t client_id, const void *data, size_t len) {
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

  // Calculate timeout based on packet size
  int timeout = calculate_packet_timeout(len);
  if (len > 100000) { // Only log for large packets
    log_info("DEBUG_TIMEOUT: Packet size=%zu, calculated timeout=%d seconds", len, timeout);
  }

  // Send header
  ssize_t sent = send_with_timeout(sockfd, &header, sizeof(header), timeout);
  if (sent != sizeof(header)) {
    log_error("Failed to send packet header with client ID: %zd/%zu bytes", sent, sizeof(header));

    // MEMORY CORRUPTION DETECTION: Print stack trace if sent value is suspiciously large
    if (sent > 1000000) { // 1MB+ is definitely corruption
      log_error("CRITICAL: CLIENT detected memory corruption in send_with_timeout (with client ID)!");
      log_error("Expected sizeof(header)=%zu, got sent=%zd", sizeof(header), sent);
      log_error("Header details: magic=0x%x, type=%u, length=%u, crc32=0x%x, client_id=%u", ntohl(header.magic),
                ntohs(header.type), ntohl(header.length), ntohl(header.crc32), ntohl(header.client_id));
      log_error("Stack trace follows:");
      platform_print_backtrace();
    }

    return -1;
  }

  // Send payload if present
  if (len > 0 && data) {
    sent = send_with_timeout(sockfd, data, len, timeout);
    // Check for error first to avoid signed/unsigned comparison issues
    if (sent < 0) {
      log_error("Failed to send packet payload: %zd/%zu bytes", sent, len);
      return -1;
    }
    if ((size_t)sent != len) {
      log_error("Failed to send packet payload: %zd/%zu bytes", sent, len);
      return -1;
    }
  }

  return 0;
}

int receive_packet_with_client(socket_t sockfd, packet_type_t *type, uint32_t *client_id, void **data, size_t *len) {
  if (!type || !client_id || !data || !len) {
    return -1;
  }

  // Check if socket is invalid (e.g., closed by signal handler)
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }
  packet_header_t header;

  // Read header
  ssize_t received = recv_with_timeout(sockfd, &header, sizeof(header), is_test_environment() ? 1 : RECV_TIMEOUT);
  // Check for error first to avoid signed/unsigned comparison issues
  if (received < 0) {
    // recv_with_timeout returned an error
    return -1;
  }
  if ((size_t)received != sizeof(header)) {
    if (received == 0) {
      log_info("Connection closed while reading packet header");
    } else {
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

    // Use adaptive timeout for large packets
    int recv_timeout = is_test_environment() ? 1 : calculate_packet_timeout(pkt_len);
    received = recv_with_timeout(sockfd, *data, pkt_len, recv_timeout);
    if (received != (ssize_t)pkt_len) {
      log_error("Failed to receive packet payload: %zd/%u bytes, errno=%d (%s)", received, pkt_len, errno,
                SAFE_STRERROR(errno));
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

int send_ping_packet(socket_t sockfd) {
  return send_packet(sockfd, PACKET_TYPE_PING, NULL, 0);
}

int send_pong_packet(socket_t sockfd) {
  return send_packet(sockfd, PACKET_TYPE_PONG, NULL, 0);
}

int send_clear_console_packet(socket_t sockfd) {
  return send_packet(sockfd, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0);
}

int send_server_state_packet(socket_t sockfd, const server_state_packet_t *state) {
  if (!state) {
    return -1;
  }

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = htonl(state->connected_client_count);
  net_state.active_client_count = htonl(state->active_client_count);
  SAFE_MEMSET(net_state.reserved, sizeof(net_state.reserved), 0, sizeof(net_state.reserved));

  return send_packet(sockfd, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state));
}

int send_terminal_capabilities_packet(socket_t sockfd, const terminal_capabilities_packet_t *caps) {
  if (!caps) {
    return -1;
  }

  // Convert to network byte order
  terminal_capabilities_packet_t net_caps;
  net_caps.capabilities = htonl(caps->capabilities);
  net_caps.color_level = htonl(caps->color_level);
  net_caps.color_count = htonl(caps->color_count);
  net_caps.render_mode = htonl(caps->render_mode);
  net_caps.width = htons(caps->width);
  net_caps.height = htons(caps->height);

  // Copy strings directly (no byte order conversion needed)
  SAFE_MEMCPY(net_caps.term_type, sizeof(net_caps.term_type), caps->term_type, sizeof(net_caps.term_type));
  SAFE_MEMCPY(net_caps.colorterm, sizeof(net_caps.colorterm), caps->colorterm, sizeof(net_caps.colorterm));

  net_caps.detection_reliable = caps->detection_reliable;

  // NEW: Include palette information
  net_caps.palette_type = htonl(caps->palette_type);
  net_caps.utf8_support = htonl(caps->utf8_support);
  SAFE_MEMCPY(net_caps.palette_custom, sizeof(net_caps.palette_custom), caps->palette_custom,
              sizeof(net_caps.palette_custom));

  // Add the desired FPS field
  net_caps.desired_fps = caps->desired_fps;
  SAFE_MEMSET(net_caps.reserved, sizeof(net_caps.reserved), 0, sizeof(net_caps.reserved));

  return send_packet(sockfd, PACKET_TYPE_CLIENT_CAPABILITIES, &net_caps, sizeof(net_caps));
}

// Convenience function that sends terminal capabilities with auto-detection
// This provides a drop-in replacement for send_size_packet
int send_terminal_size_with_auto_detect(socket_t sockfd, unsigned short width, unsigned short height) {
  // Detect terminal capabilities automatically
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Apply user's color mode override
  caps = apply_color_mode_override(caps);

  // Check if detection was reliable, use fallback only for auto-detection
  // Don't override user's explicit color mode choices
  if (!caps.detection_reliable && opt_color_mode == COLOR_MODE_AUTO) {
    log_warn("Terminal capability detection not reliable, using fallback");
    // Use minimal fallback capabilities
    SAFE_MEMSET(&caps, sizeof(caps), 0, sizeof(caps));
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities = 0; // No special capabilities
    SAFE_STRNCPY(caps.term_type, "unknown", sizeof(caps.term_type));
    SAFE_STRNCPY(caps.colorterm, "", sizeof(caps.colorterm));
    caps.detection_reliable = 0; // Not reliable
  }

  // Convert to network packet format
  terminal_capabilities_packet_t net_packet;
  net_packet.capabilities = caps.capabilities;
  net_packet.color_level = caps.color_level;
  net_packet.color_count = caps.color_count;
  net_packet.render_mode = caps.render_mode; // CRITICAL: Include render mode!
  net_packet.width = width;
  net_packet.height = height;

  // NEW: Include client's palette preferences
  net_packet.palette_type = opt_palette_type;
  net_packet.utf8_support = caps.utf8_support ? 1 : 0;
  if (opt_palette_type == PALETTE_CUSTOM && opt_palette_custom_set) {
    SAFE_STRNCPY(net_packet.palette_custom, opt_palette_custom, sizeof(net_packet.palette_custom));
    net_packet.palette_custom[sizeof(net_packet.palette_custom) - 1] = '\0';
  } else {
    SAFE_MEMSET(net_packet.palette_custom, sizeof(net_packet.palette_custom), 0, sizeof(net_packet.palette_custom));
  }

  // Set desired FPS from global variable or use the detected/default value
  if (g_max_fps > 0) {
    net_packet.desired_fps = (uint8_t)(g_max_fps > 144 ? 144 : g_max_fps);
  } else {
    net_packet.desired_fps = caps.desired_fps;
  }

  // Fallback: ensure FPS is never 0
  if (net_packet.desired_fps == 0) {
    net_packet.desired_fps = DEFAULT_MAX_FPS;
    log_warn("desired_fps was 0, using fallback DEFAULT_MAX_FPS=%d", DEFAULT_MAX_FPS);
  }

  // log_debug("NETWORK DEBUG: About to send capabilities packet with width=%u, height=%u", width, height);

  SAFE_STRNCPY(net_packet.term_type, caps.term_type, sizeof(net_packet.term_type));
  net_packet.term_type[sizeof(net_packet.term_type) - 1] = '\0';

  SAFE_STRNCPY(net_packet.colorterm, caps.colorterm, sizeof(net_packet.colorterm));
  net_packet.colorterm[sizeof(net_packet.colorterm) - 1] = '\0';

  net_packet.detection_reliable = caps.detection_reliable;

  // NEW: Include client's palette preference
  net_packet.palette_type = opt_palette_type;
  net_packet.utf8_support = opt_force_utf8 ? 1 : 0; // Use forced UTF-8 or detect
  if (opt_palette_type == PALETTE_CUSTOM && opt_palette_custom_set) {
    SAFE_STRNCPY(net_packet.palette_custom, opt_palette_custom, sizeof(net_packet.palette_custom));
    net_packet.palette_custom[sizeof(net_packet.palette_custom) - 1] = '\0';
  } else {
    SAFE_MEMSET(net_packet.palette_custom, sizeof(net_packet.palette_custom), 0, sizeof(net_packet.palette_custom));
  }

  SAFE_MEMSET(net_packet.reserved, sizeof(net_packet.reserved), 0, sizeof(net_packet.reserved));

  return send_terminal_capabilities_packet(sockfd, &net_packet);
}

// ============================================================================
// Frame Sending Functions (moved from compression.c)
// ============================================================================

#include <time.h>
#include <zlib.h>
#include "compression.h"

// Rate-limit compression debug logs to once every 5 seconds
static time_t g_last_compression_log_time = 0;

// Send ASCII frame using the new unified packet structure
int send_ascii_frame_packet(socket_t sockfd, const char *frame_data, size_t frame_size, int width, int height) {
  // Validate frame size
  if (frame_size == 0 || !frame_data) {
    log_error("Invalid frame data: frame_data=%p, frame_size=%zu", frame_data, frame_size);
    return -1;
  }

  // Check for suspicious frame size
  if (frame_size > (size_t)10 * 1024 * 1024) { // 10MB seems unreasonable for ASCII art
    log_error("Suspicious frame_size=%zu, might be corrupted", frame_size);
    return -1;
  }

  // Calculate maximum compressed size
  uLongf compressed_size = compressBound(frame_size);
  char *compressed_data;
  SAFE_MALLOC(compressed_data, compressed_size, char *);

  if (!compressed_data) {
    return -1;
  }

  // Try compression using deflate
  z_stream strm;
  SAFE_MEMSET(&strm, sizeof(strm), 0, sizeof(strm));

  int ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
  if (ret != Z_OK) {
    log_error("deflateInit failed: %d", ret);
    free(compressed_data);
    return -1;
  }

  strm.avail_in = frame_size;
  strm.next_in = (Bytef *)frame_data;
  strm.avail_out = compressed_size;
  strm.next_out = (Bytef *)compressed_data;

  ret = deflate(&strm, Z_FINISH);

  if (ret != Z_STREAM_END) {
    log_error("deflate failed: %d", ret);
    deflateEnd(&strm);
    free(compressed_data);
    return -1;
  }

  compressed_size = strm.total_out;
  deflateEnd(&strm);

  // Check if compression is worthwhile
  float compression_ratio = (float)compressed_size / (float)frame_size;
  bool use_compression = compression_ratio < COMPRESSION_RATIO_THRESHOLD;

  // Build the ASCII frame packet header
  ascii_frame_packet_t frame_header;
  frame_header.width = htonl(width);
  frame_header.height = htonl(height);
  frame_header.original_size = htonl((uint32_t)frame_size);
  frame_header.checksum = htonl(asciichat_crc32(frame_data, frame_size));
  frame_header.flags = 0;

  // Prepare packet data
  void *packet_data;
  size_t packet_size;

  if (use_compression) {
    frame_header.compressed_size = htonl((uint32_t)compressed_size);
    frame_header.flags |= htonl(FRAME_FLAG_IS_COMPRESSED);

    // Allocate buffer for header + compressed data
    packet_size = sizeof(ascii_frame_packet_t) + compressed_size;
    SAFE_MALLOC(packet_data, packet_size, void *);
    if (!packet_data) {
      free(compressed_data);
      return -1;
    }

    // Copy header and compressed data
    SAFE_MEMCPY(packet_data, packet_size, &frame_header, sizeof(ascii_frame_packet_t));
    SAFE_MEMCPY((char *)packet_data + sizeof(ascii_frame_packet_t), compressed_size, compressed_data, compressed_size);

    time_t now = time(NULL);
    if (now - g_last_compression_log_time >= 5) {
      log_debug("Sending compressed ASCII frame: %zu -> %lu bytes (%.1f%%)", frame_size, compressed_size,
                compression_ratio * 100);
      g_last_compression_log_time = now;
    }
  } else {
    frame_header.compressed_size = htonl(0);

    // Allocate buffer for header + uncompressed data
    packet_size = sizeof(ascii_frame_packet_t) + frame_size;
    SAFE_MALLOC(packet_data, packet_size, void *);
    if (!packet_data) {
      free(compressed_data);
      return -1;
    }

    // Copy header and uncompressed data
    SAFE_MEMCPY(packet_data, packet_size, &frame_header, sizeof(ascii_frame_packet_t));
    SAFE_MEMCPY((char *)packet_data + sizeof(ascii_frame_packet_t), frame_size, frame_data, frame_size);

    time_t now = time(NULL);
    if (now - g_last_compression_log_time >= 5) {
      log_debug("Sending uncompressed ASCII frame: %zu bytes", frame_size);
      g_last_compression_log_time = now;
    }
  }

  // Send as a single unified packet
  int result = send_packet(sockfd, PACKET_TYPE_ASCII_FRAME, packet_data, packet_size);

  free(packet_data);
  free(compressed_data);

  if (result < 0) {
    return -1;
  }

  if (use_compression) {
    return (int)compressed_size;
  }
  return (int)frame_size;
}

// Send image frame using the new unified packet structure
int send_image_frame_packet(socket_t sockfd, const void *pixel_data, size_t pixel_size, int width, int height,
                            uint32_t pixel_format) {
  // Validate input
  if (!pixel_data || pixel_size == 0) {
    log_error("Invalid pixel data");
    return -1;
  }

  // Build the image frame packet header
  image_frame_packet_t frame_header;
  frame_header.width = htonl(width);
  frame_header.height = htonl(height);
  frame_header.pixel_format = htonl(pixel_format);
  frame_header.compressed_size = htonl(0); // No compression for now
  frame_header.checksum = htonl(asciichat_crc32(pixel_data, pixel_size));
  frame_header.timestamp = htonl((uint32_t)time(NULL));

  // Allocate buffer for header + pixel data
  size_t packet_size = sizeof(image_frame_packet_t) + pixel_size;
  void *packet_data;
  SAFE_MALLOC(packet_data, packet_size, void *);
  if (!packet_data) {
    return -1;
  }

  // Copy header and pixel data
  SAFE_MEMCPY(packet_data, packet_size, &frame_header, sizeof(image_frame_packet_t));
  SAFE_MEMCPY((char *)packet_data + sizeof(image_frame_packet_t), pixel_size, pixel_data, pixel_size);

  // Send as a single unified packet
  int result = send_packet(sockfd, PACKET_TYPE_IMAGE_FRAME, packet_data, packet_size);

  free(packet_data);
  return result;
}

// Legacy function - now uses the new unified packet system
int send_compressed_frame(socket_t sockfd, const char *frame_data, size_t frame_size) {
  // Use the new unified packet function with global width/height
  return send_ascii_frame_packet(sockfd, frame_data, frame_size, opt_width, opt_height);
}
