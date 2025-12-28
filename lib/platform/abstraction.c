
/**
 * @file platform/abstraction.c
 * @ingroup platform
 * @brief 🏗️ Common platform abstraction stubs (OS-specific code in posix/ and windows/ subdirectories)
 */

#include "abstraction.h"
#include "socket.h"
#include "../common.h"
#include <netinet/tcp.h>

// ============================================================================
// Common Platform Functions
// ============================================================================
// This file is reserved for common platform functions that don't need
// OS-specific implementations. Currently all functions are OS-specific.
//
// The OS-specific implementations are in:
// - platform_windows.c (Windows)
// - platform_posix.c (POSIX/Unix/Linux/macOS)
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
  int send_buffer = 2 * 1024 * 1024; // 2MB
  if (socket_setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
    send_buffer = 512 * 1024; // 512KB fallback
    if (socket_setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
      send_buffer = 128 * 1024; // 128KB fallback
      socket_setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
    }
  }

  // 3. Increase receive buffer (2MB with fallbacks)
  // Allows buffering of multiple incoming frames before processing
  int recv_buffer = 2 * 1024 * 1024; // 2MB
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
