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
#include "crypto/handshake.h"
#include "crypto/known_hosts.h"
#include "crypto.h"
#include "crypto/crypto.h"

#include "platform/abstraction.h"
#include "network.h"
#include "common.h"
#include "options.h"
#include "buffer_pool.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <netinet/tcp.h>
#endif

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

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
 * Crypto State
 * ============================================================================ */

/** Per-connection crypto handshake context */
crypto_handshake_context_t g_crypto_ctx = {0};

/** Whether encryption is enabled for this connection */
static bool g_encryption_enabled = false;

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
int server_connection_establish(const char *address, int port, int reconnect_attempt, bool first_connection,
                                bool has_ever_connected) {
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
    // Reconnection attempt logged only to file
    platform_sleep_usec((unsigned int)delay);
  } else {
    // Initial connection logged only to file
  }

  // Create socket
  g_sockfd = socket_create(AF_INET, SOCK_STREAM, 0);
  if (g_sockfd == INVALID_SOCKET_VALUE) {
    log_error("Could not create socket: %s", network_error_string(errno));
    return -1;
  }

  // Set up server address structure
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  // Convert address string to binary form
  if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
    log_error("Invalid server address: %s", address);
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

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

  // Mark connection as active immediately after successful socket connection
  atomic_store(&g_connection_active, true);
  atomic_store(&g_connection_lost, false);
  atomic_store(&g_should_reconnect, false);

  // Initialize crypto BEFORE starting protocol handshake
  log_debug("CLIENT_CONNECT: Calling client_crypto_init()");
  if (client_crypto_init() != 0) {
    log_error("Failed to initialize crypto");
    log_debug("CLIENT_CONNECT: client_crypto_init() failed");
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }
  log_debug("CLIENT_CONNECT: client_crypto_init() succeeded");

  // Perform crypto handshake if encryption is enabled
  log_debug("CLIENT_CONNECT: Calling client_crypto_handshake()");
  if (client_crypto_handshake(g_sockfd) != 0) {
    log_error("Crypto handshake failed");
    log_debug("CLIENT_CONNECT: client_crypto_handshake() failed");
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }
  log_debug("CLIENT_CONNECT: client_crypto_handshake() succeeded");

  // Turn OFF terminal logging when successfully connected to server
  // First connection - we'll disable logging after main.c shows the "Connected successfully" message
  if (!opt_snapshot_mode) {
    log_info("Connected to server - terminal logging will be disabled after initial setup");
  } else {
    log_info("Connected to server - terminal logging kept enabled for snapshot mode");
  }

  // Configure socket options for optimal performance
  if (set_socket_keepalive(g_sockfd) < 0) {
    log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
  }

  // Set socket buffer sizes for large data transmission
  int send_buffer_size = 1024 * 1024; // 1MB send buffer
  int recv_buffer_size = 1024 * 1024; // 1MB receive buffer

  if (socket_setsockopt(g_sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
    log_warn("Failed to set send buffer size: %s", network_error_string(errno));
  }

  if (socket_setsockopt(g_sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
    log_warn("Failed to set receive buffer size: %s", network_error_string(errno));
  }

  // Enable TCP_NODELAY to reduce latency for large packets
  int nodelay = 1;
  if (socket_setsockopt(g_sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    log_warn("Failed to set TCP_NODELAY: %s", network_error_string(errno));
  }

  // Send initial terminal capabilities to server (this may generate debug logs)
  int result = threaded_send_terminal_size_with_auto_detect(opt_width, opt_height);
  if (result < 0) {
    log_error("Failed to send initial capabilities to server: %s", network_error_string(errno));
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Now disable terminal logging after capabilities are sent (for reconnections)
  if (!opt_snapshot_mode && has_ever_connected) {
    log_set_terminal_output(false);
    log_info("Reconnected to server - terminal logging disabled to prevent interference with ASCII display");
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
  SAFE_SNPRINTF(my_display_name, sizeof(my_display_name), "%s-%d", display_name, pid);

  if (threaded_send_client_join_packet(my_display_name, my_capabilities) < 0) {
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

  // Cleanup crypto context if encryption was enabled
  if (g_encryption_enabled) {
    crypto_handshake_cleanup(&g_crypto_ctx);
    g_encryption_enabled = false;
  }

  // Turn ON terminal logging when connection is closed
  printf("\n");
  log_set_terminal_output(true);
  log_info("Connection closed - terminal logging re-enabled");
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

  // Turn ON terminal logging when connection is shutdown
  printf("\n");
  log_set_terminal_output(true);
  log_info("Connection shutdown - terminal logging re-enabled");
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

  // Turn ON terminal logging when connection is lost
  printf("\n");
  log_set_terminal_output(true);
  log_info("Connection lost - terminal logging re-enabled");
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
 * Thread Safety Interface
 * ============================================================================ */

/**
 * Thread-safe wrapper functions for network operations
 */
int threaded_send_packet(packet_type_t type, const void *data, size_t len) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);

  // Check if crypto handshake is complete and encrypt the packet
  if (crypto_handshake_is_ready(&g_crypto_ctx)) {
    log_debug("Encrypting packet type=%d, len=%zu (crypto_ready=%d, handshake_complete=%d)", type, len,
              crypto_handshake_is_ready(&g_crypto_ctx), g_crypto_ctx.crypto_ctx.handshake_complete);
    // Create the packet header
    packet_header_t header;
    header.magic = htonl(PACKET_MAGIC);
    header.type = htons(type);
    header.length = htonl(len);
    header.crc32 = htonl(asciichat_crc32(data, len));
    header.client_id = htonl(g_my_client_id);

    // Combine header and data
    size_t plaintext_len = sizeof(header) + len;
    uint8_t *plaintext = buffer_pool_alloc(plaintext_len);
    if (!plaintext) {
      mutex_unlock(&g_send_mutex);
      return -1;
    }

    memcpy(plaintext, &header, sizeof(header));
    memcpy(plaintext + sizeof(header), data, len);

    // Encrypt the packet data
    size_t ciphertext_len;
    size_t ciphertext_size = plaintext_len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
    uint8_t *ciphertext = buffer_pool_alloc(ciphertext_size);
    if (!ciphertext) {
      buffer_pool_free(plaintext, plaintext_len);
      mutex_unlock(&g_send_mutex);
      return -1;
    }

    int encrypt_result = crypto_handshake_encrypt_packet(&g_crypto_ctx, plaintext, plaintext_len, ciphertext,
                                                         ciphertext_size, &ciphertext_len);
    if (encrypt_result != 0) {
      log_error("Failed to encrypt packet (result=%d)", encrypt_result);
      buffer_pool_free(plaintext, plaintext_len);
      buffer_pool_free(ciphertext, ciphertext_size);
      mutex_unlock(&g_send_mutex);
      return -1;
    }

    // Send as PACKET_TYPE_ENCRYPTED with the encrypted data
    int result = send_packet(sockfd, PACKET_TYPE_ENCRYPTED, ciphertext, ciphertext_len);
    buffer_pool_free(plaintext, plaintext_len);
    buffer_pool_free(ciphertext, ciphertext_size);
    mutex_unlock(&g_send_mutex);
    return result;
    mutex_unlock(&g_send_mutex);
    return 0;
  } else {
    // No encryption - send normal packet
    log_debug("Sending unencrypted packet type=%d, len=%zu", type, len);
    int result = send_packet(sockfd, type, data, len);
    mutex_unlock(&g_send_mutex);
    return result;
  }
}

int threaded_send_audio_batch_packet(const float *samples, int num_samples, int batch_count) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_audio_batch_packet(sockfd, samples, num_samples, batch_count);
  mutex_unlock(&g_send_mutex);
  return result;
}

int threaded_send_ping_packet(void) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_ping_packet(sockfd);
  mutex_unlock(&g_send_mutex);
  return result;
}

int threaded_send_pong_packet(void) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_pong_packet(sockfd);
  mutex_unlock(&g_send_mutex);
  return result;
}

int threaded_send_stream_start_packet(uint32_t stream_type) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_stream_start_packet(sockfd, stream_type);
  mutex_unlock(&g_send_mutex);
  return result;
}

int threaded_send_terminal_size_with_auto_detect(unsigned short width, unsigned short height) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_terminal_size_with_auto_detect(sockfd, width, height);
  mutex_unlock(&g_send_mutex);
  return result;
}

int threaded_send_client_join_packet(const char *display_name, uint32_t capabilities) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);
  int result = send_client_join_packet(sockfd, display_name, capabilities);
  mutex_unlock(&g_send_mutex);
  return result;
}
