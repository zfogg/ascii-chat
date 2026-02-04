
/**
 * @file platform/socket.c
 * @ingroup platform
 * @brief 🌐 Common socket utility functions (cross-platform implementations)
 */

#include <ascii-chat/platform/socket.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>

// Platform-specific TCP header (included by socket.h above)
// Windows: ws2tcpip.h (included by winsock2.h)
// POSIX: netinet/tcp.h (need to include explicitly)
#ifndef _WIN32
#include <netinet/tcp.h>
#endif

// ============================================================================
// Common Socket Functions
// ============================================================================

/**
 * @brief Optimize socket for high-throughput video streaming
 *
 * Consolidates socket configuration for real-time video streaming:
 * - Disables Nagle's algorithm (TCP_NODELAY)
 * - Sets large send/receive buffers with automatic fallbacks
 * - Enables TCP keepalive
 * - Sets send/receive timeouts
 *
 * This common implementation applies to both POSIX and Windows platforms.
 *
 * @param sock Socket to configure
 * @ingroup platform
 */
void socket_optimize_for_streaming(socket_t sock) {
  // 1. Disable Nagle's algorithm - CRITICAL for real-time video
  // TCP_NODELAY ensures data is sent immediately without waiting for ACKs
  int nodelay = 1;
  if (socket_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) != 0) {
    log_warn("Failed to disable Nagle's algorithm (TCP_NODELAY) on socket");
  }

  // 2. Increase send buffer for video streaming (2MB with fallbacks)
  // Large buffers reduce packet drops during bursty frame transmission
  int send_buffer = MAX_FRAME_BUFFER_SIZE; // 2MB
  if (socket_setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
    send_buffer = 512 * 1024; // 512KB fallback
    if (socket_setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
      send_buffer = 128 * 1024; // 128KB fallback
      socket_setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
    }
  }

  // 3. Increase receive buffer (2MB with fallbacks)
  // Allows buffering of multiple incoming frames before processing
  int recv_buffer = MAX_FRAME_BUFFER_SIZE; // 2MB
  if (socket_setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buffer, sizeof(recv_buffer)) != 0) {
    recv_buffer = 512 * 1024; // 512KB fallback
    if (socket_setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buffer, sizeof(recv_buffer)) != 0) {
      recv_buffer = 128 * 1024; // 128KB fallback
      socket_setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buffer, sizeof(recv_buffer));
    }
  }

  // 4. Enable keepalive to detect dead connections
  int keepalive = 1;
  socket_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
}

/**
 * @brief Set socket receive and send timeouts
 * @param sock Socket to configure
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, non-zero on error
 *
 * Cross-platform implementation that sets both SO_RCVTIMEO and SO_SNDTIMEO.
 * Platform-specific socket_setsockopt() handles the differences between
 * Windows (DWORD milliseconds) and POSIX (struct timeval).
 */
int socket_set_timeout(socket_t sock, uint64_t timeout_ns) {
#ifdef _WIN32
  // Windows: SO_RCVTIMEO and SO_SNDTIMEO use DWORD (milliseconds)
  DWORD timeout_val = (DWORD)time_ns_to_ms(timeout_ns);
  if (socket_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout_val, sizeof(timeout_val)) != 0) {
    return -1;
  }
  if (socket_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout_val, sizeof(timeout_val)) != 0) {
    return -1;
  }
#else
  // POSIX: SO_RCVTIMEO and SO_SNDTIMEO use struct timeval (seconds + microseconds)
  struct timeval tv;
  uint64_t us = time_ns_to_us(timeout_ns);
  tv.tv_sec = (time_t)(us / US_PER_SEC_INT);
  tv.tv_usec = (suseconds_t)(us % US_PER_SEC_INT);
  if (socket_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    return -1;
  }
  if (socket_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    return -1;
  }
#endif
  return 0;
}
