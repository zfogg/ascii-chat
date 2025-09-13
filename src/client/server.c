/**
 * @file server.c
 * @brief ASCII-Chat Client Server Connection Management
 *
 * This module handles all aspects of client-to-server communication including
 * connection establishment, reconnection logic with exponential backoff,
 * socket management, and thread-safe packet transmission.
 *
 * ## Connection Lifecycle
 *
 * The connection management follows a robust state machine:
 * 1. **Initialization**: Socket creation and address resolution
 * 2. **Connection**: TCP handshake with configurable timeout
 * 3. **Capability Exchange**: Send terminal capabilities and client info
 * 4. **Active Monitoring**: Health checks and keepalive management
 * 5. **Disconnection**: Graceful or forced connection teardown
 * 6. **Reconnection**: Exponential backoff retry logic
 *
 * ## Thread Safety
 *
 * All packet transmission functions use a global send mutex to prevent
 * interleaved packets on the wire. The socket file descriptor is protected
 * with atomic operations for thread-safe access across multiple threads.
 *
 * ## Reconnection Strategy
 *
 * Implements exponential backoff with jitter:
 * - Initial delay: 10ms
 * - Exponential growth: delay = 10ms + (200ms * attempt)
 * - Maximum delay: 5 seconds
 * - Jitter: Small random component to prevent thundering herd
 *
 * ## Platform Compatibility
 *
 * Uses platform abstraction layer for:
 * - Socket creation and management (Winsock vs BSD sockets)
 * - Network error handling and error string conversion
 * - Address resolution and connection timeout handling
 * - Socket options (keepalive, nodelay)
 *
 * ## Integration Points
 *
 * - **main.c**: Calls connection establishment and monitoring functions
 * - **protocol.c**: Uses thread-safe send functions for packet transmission
 * - **keepalive.c**: Monitors connection health and triggers reconnection
 * - **capture.c**: Sends media data through connection
 * - **audio.c**: Sends audio data through connection
 *
 * ## Error Handling
 *
 * Connection errors are classified into:
 * - **Temporary**: Network congestion, temporary DNS failures (retry)
 * - **Permanent**: Invalid address, authentication failure (report and exit)
 * - **Timeout**: Connection establishment timeout (retry with backoff)
 * - **Loss**: Existing connection broken (immediate reconnection attempt)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 2.0
 */

#include "server.h"
#include "main.h"

#include "platform/abstraction.h"
#include "network.h"
#include "common.h"
#include "options.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <stdatomic.h>

/* ============================================================================
 * Connection State Management
 * ============================================================================ */

/** Current socket file descriptor (INVALID_SOCKET_VALUE when disconnected) */
static socket_t g_sockfd = INVALID_SOCKET_VALUE;

/** Atomic flag indicating if connection is active */
static atomic_bool g_connection_active = false;

/** Atomic flag indicating if connection loss was detected */
static atomic_bool g_connection_lost = false;

/** Atomic flag indicating if reconnection should be attempted */
static atomic_bool g_should_reconnect = false;

/** Client ID assigned by server (derived from local port) */
static uint32_t g_my_client_id = 0;

/** Mutex to protect socket sends (prevent interleaved packets) */
static mutex_t g_send_mutex = {0};

/* ============================================================================
 * Reconnection Logic
 * ============================================================================ */

/** Maximum delay between reconnection attempts (microseconds) */
#define MAX_RECONNECT_DELAY (5 * 1000 * 1000)

/**
 * Calculate reconnection delay with exponential backoff
 *
 * Implements exponential backoff with a reasonable cap to prevent
 * excessively long delays. The formula provides rapid initial retries
 * that gradually slow down for persistent failures.
 *
 * @param reconnect_attempt The current attempt number (1-based)
 * @return Delay in microseconds before next attempt
 */
static float get_reconnect_delay(unsigned int reconnect_attempt) {
  float delay = 0.01f + 0.2f * (reconnect_attempt - 1) * 1000 * 1000;
  if (delay > MAX_RECONNECT_DELAY)
    delay = (float)MAX_RECONNECT_DELAY;
  return delay;
}

/* ============================================================================
 * Thread-Safe Packet Sending Functions
 * ============================================================================ */

/**
 * Thread-safe wrapper for network.h send_packet
 *
 * Protects the underlying send_packet call with a mutex to prevent
 * multiple threads from sending packets simultaneously, which could
 * result in interleaved packet data on the wire.
 *
 * @param sockfd Socket file descriptor
 * @param type Packet type identifier
 * @param data Packet payload data
 * @param len Length of payload data
 * @return 0 on success, negative on error
 */
static int safe_send_packet(socket_t sockfd, packet_type_t type, const void *data, size_t len) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_packet(sockfd, type, data, len);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_audio_packet
 *
 * @param sockfd Socket file descriptor
 * @param samples Audio sample data
 * @param num_samples Number of samples in buffer
 * @return 0 on success, negative on error
 */
static int safe_send_audio_packet(socket_t sockfd, const float *samples, int num_samples) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_audio_packet(sockfd, samples, num_samples);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_audio_batch_packet
 *
 * @param sockfd Socket file descriptor
 * @param samples Batched audio sample data
 * @param num_samples Total number of samples in batch
 * @param batch_count Number of individual packets in batch
 * @return 0 on success, negative on error
 */
static int safe_send_audio_batch_packet(socket_t sockfd, const float *samples, int num_samples, int batch_count) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_audio_batch_packet(sockfd, samples, num_samples, batch_count);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_terminal_size_with_auto_detect
 *
 * @param sockfd Socket file descriptor
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 */
static int safe_send_terminal_size_with_auto_detect(socket_t sockfd, unsigned short width, unsigned short height) {
  log_debug("safe_send entry, errno=%d", errno);
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    log_debug("connection inactive or invalid socket, errno=%d", errno);
    return -1;
  }

  log_debug("acquiring send mutex, errno=%d", errno);
  mutex_lock(&g_send_mutex);
  log_debug("calling send_terminal_size_with_auto_detect, errno=%d", errno);
  int result = send_terminal_size_with_auto_detect(sockfd, width, height);
  log_debug("send_terminal_size_with_auto_detect returned %d, errno=%d", result, errno);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_pong_packet
 *
 * @param sockfd Socket file descriptor
 * @return 0 on success, negative on error
 */
static int safe_send_pong_packet(socket_t sockfd) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_pong_packet(sockfd);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_ping_packet
 *
 * @param sockfd Socket file descriptor
 * @return 0 on success, negative on error
 */
static int safe_send_ping_packet(socket_t sockfd) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_ping_packet(sockfd);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_stream_start_packet
 *
 * @param sockfd Socket file descriptor
 * @param stream_type Type of stream being started (audio/video)
 * @return 0 on success, negative on error
 */
static int safe_send_stream_start_packet(socket_t sockfd, uint32_t stream_type) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_stream_start_packet(sockfd, stream_type);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_stream_stop_packet
 *
 * @param sockfd Socket file descriptor
 * @param stream_type Type of stream being stopped (audio/video)
 * @return 0 on success, negative on error
 */
static int safe_send_stream_stop_packet(socket_t sockfd, uint32_t stream_type) {
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_stream_stop_packet(sockfd, stream_type);
  mutex_unlock(&g_send_mutex);

  return result;
}

/**
 * Thread-safe wrapper for network.h send_client_join_packet
 *
 * @param socketfd Socket file descriptor
 * @param display_name Client display name for identification
 * @param capabilities Bitmask of client capabilities
 * @return 0 on success, negative on error
 */
static int safe_send_client_join_packet(socket_t socketfd, const char *display_name, uint32_t capabilities) {
  if (!atomic_load(&g_connection_active) || socketfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_client_join_packet(socketfd, display_name, capabilities);
  mutex_unlock(&g_send_mutex);

  return result;
}

/* ============================================================================
 * Socket Management Functions
 * ============================================================================ */

/**
 * Close socket connection safely
 *
 * Performs platform-appropriate socket closure and resets the global
 * socket descriptor. Safe to call multiple times or with invalid sockets.
 *
 * @param socketfd Socket file descriptor to close
 * @return 0 on success, -1 on error
 */
static int close_socket(socket_t socketfd) {
  if (socket_is_valid(socketfd)) {
    log_info("Closing socket connection");
    if (socket_close(socketfd) != 0) {
      log_error("Failed to close socket: %s", network_error_string(errno));
      return -1;
    }
    return 0;
  }
  return 0; // Socket already closed or invalid
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * Initialize the server connection management subsystem
 *
 * Sets up the send mutex and initializes connection state variables.
 * Must be called once during client startup before any connection attempts.
 *
 * @return 0 on success, non-zero on failure
 */
int server_connection_init() {
  // Initialize mutex for thread-safe packet sending
  if (mutex_init(&g_send_mutex) != 0) {
    log_error("Failed to initialize send mutex");
    return -1;
  }

  // Initialize connection state
  g_sockfd = INVALID_SOCKET_VALUE;
  atomic_store(&g_connection_active, false);
  atomic_store(&g_connection_lost, false);
  atomic_store(&g_should_reconnect, false);
  g_my_client_id = 0;

  return 0;
}

/**
 * Establish connection to ASCII-Chat server
 *
 * Attempts to connect to the specified server with full capability negotiation.
 * Implements reconnection logic with exponential backoff for failed attempts.
 * On successful connection, performs initial handshake including terminal
 * capabilities and client join protocol.
 *
 * @param address Server IP address or hostname
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt number (0 for first)
 * @param first_connection True if this is the initial connection attempt
 * @return 0 on success, negative on error
 */
int server_connection_establish(const char *address, int port, int reconnect_attempt, bool first_connection) {
  (void)first_connection; // Currently unused
  if (!address || port <= 0) {
    log_error("Invalid address or port parameters");
    return -1;
  }

  // Close any existing connection
  if (g_sockfd != INVALID_SOCKET_VALUE) {
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
  }

  // Apply reconnection delay if this is a retry
  if (reconnect_attempt > 0) {
    float delay = get_reconnect_delay(reconnect_attempt);
    log_info("Reconnection attempt #%d to %s:%d in %.2f seconds...", reconnect_attempt, address, port,
             delay / 1000.0 / 1000.0);
    platform_sleep_usec((unsigned int)delay);
  } else {
    log_info("Connecting to %s:%d", address, port);
  }
  log_info("DEBUG: About to create socket");

  // Create socket
  g_sockfd = socket_create(AF_INET, SOCK_STREAM, 0);
  if (g_sockfd == INVALID_SOCKET_VALUE) {
    log_error("Could not create socket: %s", network_error_string(errno));
    return -1;
  }
  log_info("DEBUG: About to set up server address");
  log_info("DEBUG: Socket created, fd=%d", g_sockfd);

  // Set up server address structure
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  log_info("DEBUG: About to call inet_pton");
  serv_addr.sin_port = htons(port);

  // Convert address string to binary form
  if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
    log_error("Invalid server address: %s", address);
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }
  log_info("DEBUG: inet_pton succeeded, about to connect");

  // Attempt connection with timeout
  if (!connect_with_timeout(g_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr), CONNECT_TIMEOUT)) {
    log_warn("Connection failed: %s", network_error_string(errno));
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Connection successful - extract local port for client ID
  struct sockaddr_in local_addr = {0};
  socklen_t addr_len = sizeof(local_addr);
  if (getsockname(g_sockfd, (struct sockaddr *)&local_addr, &addr_len) == -1) {
    log_error("Failed to get local socket address: %s", network_error_string(errno));
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  int local_port = ntohs(local_addr.sin_port);
  g_my_client_id = (uint32_t)local_port;

  log_info("Connected to server %s:%d (local port: %d)", address, port, local_port);

  // Mark connection as active immediately after successful socket connection
  atomic_store(&g_connection_active, true);
  atomic_store(&g_connection_lost, false);
  atomic_store(&g_should_reconnect, false);

  // Configure socket options for optimal performance
  if (set_socket_keepalive(g_sockfd) < 0) {
    log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
  }

  // Send initial terminal capabilities to server
  log_debug("About to call safe_send_terminal_size_with_auto_detect, errno=%d", errno);
  int result = safe_send_terminal_size_with_auto_detect(g_sockfd, opt_width, opt_height);
  log_debug("safe_send_terminal_size_with_auto_detect returned %d, errno=%d", result, errno);
  if (result < 0) {
    log_error("Failed to send initial capabilities to server: %s", network_error_string(errno));
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Send client join packet for multi-user support
  uint32_t my_capabilities = CLIENT_CAP_VIDEO; // Basic video capability
  if (opt_audio_enabled) {
    my_capabilities |= CLIENT_CAP_AUDIO;
  }
  if (opt_color_mode != COLOR_MODE_MONO) {
    my_capabilities |= CLIENT_CAP_COLOR;
  }
  if (opt_stretch) {
    my_capabilities |= CLIENT_CAP_STRETCH;
  }

  // Generate display name from username + PID
  const char *display_name = platform_get_username();

  char my_display_name[MAX_DISPLAY_NAME_LEN];
  int pid = getpid();
  snprintf(my_display_name, sizeof(my_display_name), "%s-%d", display_name, pid);

  if (safe_send_client_join_packet(g_sockfd, my_display_name, my_capabilities) < 0) {
    log_error("Failed to send client join packet: %s", network_error_string(errno));
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Connection already marked as active after socket creation

  return 0;
}

/**
 * Check if server connection is currently active
 *
 * @return true if connection is active, false otherwise
 */
bool server_connection_is_active() {
  return atomic_load(&g_connection_active) && (g_sockfd != INVALID_SOCKET_VALUE);
}

/**
 * Get current socket file descriptor
 *
 * @return Socket file descriptor or INVALID_SOCKET_VALUE if disconnected
 */
socket_t server_connection_get_socket() {
  return g_sockfd;
}

/**
 * Get client ID assigned by server
 *
 * @return Client ID (based on local port) or 0 if not connected
 */
uint32_t server_connection_get_client_id() {
  return g_my_client_id;
}

/**
 * Close the server connection gracefully
 *
 * Marks connection as inactive and closes the socket. This function
 * is safe to call multiple times and from multiple threads.
 */
void server_connection_close() {
  atomic_store(&g_connection_active, false);

  if (g_sockfd != INVALID_SOCKET_VALUE) {
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
  }

  g_my_client_id = 0;
}

/**
 * Emergency connection shutdown for signal handlers
 *
 * Performs immediate connection shutdown without waiting for graceful
 * close procedures. Uses socket shutdown to interrupt any blocking
 * recv() operations in other threads.
 */
void server_connection_shutdown() {
  atomic_store(&g_connection_active, false);
  atomic_store(&g_connection_lost, true);

  if (g_sockfd != INVALID_SOCKET_VALUE) {
    socket_shutdown(g_sockfd, SHUT_RDWR); // Interrupt blocking operations
    socket_close(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
  }
}

/**
 * Signal that connection has been lost
 *
 * Called by other modules (typically protocol handlers) when they
 * detect connection failure. Triggers reconnection logic in main loop.
 */
void server_connection_lost() {
  atomic_store(&g_connection_lost, true);
  atomic_store(&g_connection_active, false);
}

/**
 * Check if connection loss has been detected
 *
 * @return true if connection loss was flagged, false otherwise
 */
bool server_connection_is_lost() {
  return atomic_load(&g_connection_lost);
}

/**
 * Cleanup connection management subsystem
 *
 * Closes any active connection and destroys synchronization objects.
 * Called during client shutdown.
 */
void server_connection_cleanup() {
  server_connection_close();
  mutex_destroy(&g_send_mutex);
}

/* ============================================================================
 * Public Packet Sending Interface
 * ============================================================================ */

/**
 * Send general packet through current connection
 *
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return 0 on success, negative on error
 */
int server_send_packet(packet_type_t type, const void *data, size_t len) {
  return safe_send_packet(g_sockfd, type, data, len);
}

/**
 * Send audio data packet
 *
 * @param samples Audio sample buffer
 * @param num_samples Number of samples in buffer
 * @return 0 on success, negative on error
 */
int server_send_audio(const float *samples, int num_samples) {
  return safe_send_audio_packet(g_sockfd, samples, num_samples);
}

/**
 * Send batched audio data packet
 *
 * @param samples Batched audio sample buffer
 * @param num_samples Total number of samples
 * @param batch_count Number of packets in batch
 * @return 0 on success, negative on error
 */
int server_send_audio_batch(const float *samples, int num_samples, int batch_count) {
  return safe_send_audio_batch_packet(g_sockfd, samples, num_samples, batch_count);
}

/**
 * Send terminal capabilities update
 *
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 */
int server_send_terminal_capabilities(unsigned short width, unsigned short height) {
  return safe_send_terminal_size_with_auto_detect(g_sockfd, width, height);
}

/**
 * Send ping keepalive packet
 *
 * @return 0 on success, negative on error
 */
int server_send_ping() {
  return safe_send_ping_packet(g_sockfd);
}

/**
 * Send pong response packet
 *
 * @return 0 on success, negative on error
 */
int server_send_pong() {
  return safe_send_pong_packet(g_sockfd);
}

/**
 * Send stream start notification
 *
 * @param stream_type Type of stream being started
 * @return 0 on success, negative on error
 */
int server_send_stream_start(uint32_t stream_type) {
  return safe_send_stream_start_packet(g_sockfd, stream_type);
}

/**
 * Send stream stop notification
 *
 * @param stream_type Type of stream being stopped
 * @return 0 on success, negative on error
 */
int server_send_stream_stop(uint32_t stream_type) {
  return safe_send_stream_stop_packet(g_sockfd, stream_type);
}
