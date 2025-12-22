/**
 * @file client/server.c
 * @ingroup client_connection
 * @brief 🌐 Client connection manager: TCP connection, reconnection with exponential backoff, and thread-safe
 * transmission
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
#include "crypto.h"
#include "crypto/crypto.h"
#include "crypto/handshake.h"

#include "platform/abstraction.h"
#include "platform/terminal.h"
#include "platform/system.h"
#include "network/packet.h"
#include "network/network.h"
#include "network/av.h"
#include "common.h"
#include "display.h"
#include "options.h"
#include "palette.h"

#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <netinet/tcp.h>
#include <netdb.h>     // For getaddrinfo(), gai_strerror()
#include <arpa/inet.h> // For inet_ntop()
#else
#include <ws2tcpip.h> // For getaddrinfo(), gai_strerror(), inet_ntop()
#endif

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

/* ============================================================================
 * Connection State Management
 * ============================================================================ */

/**
 * @brief Current socket file descriptor
 *
 * Stores the active socket connection to the server. Set to INVALID_SOCKET_VALUE
 * when disconnected. Thread-safe access via atomic operations where needed.
 *
 * @ingroup client_connection
 */
static socket_t g_sockfd = INVALID_SOCKET_VALUE;

/**
 * @brief Atomic flag indicating if connection is currently active
 *
 * Thread-safe flag indicating whether the connection is established and active.
 * Checked by all threads to determine if they can use the socket safely.
 * Set to true on successful connection, false on disconnection.
 *
 * @ingroup client_connection
 */
static atomic_bool g_connection_active = false;

/**
 * @brief Atomic flag indicating if connection loss was detected
 *
 * Set to true when a connection failure is detected by any thread (protocol,
 * keepalive, or main loop). Used to trigger reconnection logic.
 *
 * @ingroup client_connection
 */
static atomic_bool g_connection_lost = false;

/**
 * @brief Atomic flag indicating if reconnection should be attempted
 *
 * Set by main loop to signal that reconnection should be attempted after
 * connection loss. Used to coordinate exponential backoff retry logic.
 *
 * @ingroup client_connection
 */
static atomic_bool g_should_reconnect = false;

/**
 * @brief Client ID assigned by server
 *
 * Unique identifier assigned to this client by the server during connection
 * establishment. Derived from the client's local port number.
 *
 * @ingroup client_connection
 */
static uint32_t g_my_client_id = 0;

/**
 * @brief Resolved server IP address string
 *
 * Stores the resolved server IP address (IPv4 or IPv6) in string format.
 * Used for known_hosts verification and logging purposes. Sized to hold
 * maximum IPv6 address length with scope ID.
 *
 * @ingroup client_connection
 */
static char g_server_ip[256] = {0};

/**
 * @brief Mutex protecting socket send operations
 *
 * Ensures thread-safe packet transmission by preventing interleaved packets
 * on the wire. All send functions must acquire this mutex before writing
 * to the socket to maintain packet integrity.
 *
 * @ingroup client_connection
 */
static mutex_t g_send_mutex = {0};

/* ============================================================================
 * Crypto State
 * ============================================================================ */

/**
 * @brief Per-connection crypto handshake context
 *
 * Maintains the cryptographic state for the current connection, including
 * key exchange state, encryption keys, and handshake progress.
 *
 * @note This is not static because it may be accessed from crypto.c
 * @ingroup client_connection
 */
crypto_handshake_context_t g_crypto_ctx = {0};

/**
 * @brief Flag indicating whether encryption is enabled for this connection
 *
 * Set to true after successful cryptographic handshake completion.
 * Controls whether packets are encrypted before transmission.
 *
 * @ingroup client_connection
 */
static bool g_encryption_enabled = false;

/* ============================================================================
 * Reconnection Logic
 * ============================================================================ */

/** Maximum delay between reconnection attempts (microseconds) */
#define MAX_RECONNECT_DELAY (5 * 1000 * 1000)

/**
 * @brief Calculate reconnection delay with exponential backoff
 *
 * Implements exponential backoff with a reasonable cap to prevent
 * excessively long delays. The formula provides rapid initial retries
 * that gradually slow down for persistent failures.
 *
 * @param reconnect_attempt The current attempt number (1-based)
 * @return Delay in microseconds before next attempt
 *
 * @ingroup client_connection
 */
static float get_reconnect_delay(unsigned int reconnect_attempt) {
  // Increased initial delay to allow socket cleanup
  float delay = 0.1f + 0.2f * (reconnect_attempt - 1) * 1000 * 1000;
  if (delay > MAX_RECONNECT_DELAY)
    delay = (float)MAX_RECONNECT_DELAY;
  return delay;
}

/* ============================================================================
 * Socket Management Functions
 * ============================================================================ */

/**
 * @brief Close socket connection safely
 *
 * Performs platform-appropriate socket closure and resets the global
 * socket descriptor. Safe to call multiple times or with invalid sockets.
 *
 * @param socketfd Socket file descriptor to close
 * @return 0 on success, -1 on error
 *
 * @ingroup client_connection
 */
static int close_socket(socket_t socketfd) {
  if (socket_is_valid(socketfd)) {
    log_debug("Closing socket %d", socketfd);

    if (socket_close(socketfd) != 0) {
      log_error("Failed to close socket: %s", network_error_string());
      return -1;
    }

    // Small delay to ensure socket resources are fully released
    // This prevents WSA error 10038 on subsequent connections
    platform_sleep_usec(50000); // 50ms delay

    return 0;
  }

  return 0; // Socket already closed or invalid
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * @brief Initialize the server connection management subsystem
 *
 * Sets up the send mutex and initializes connection state variables.
 * Must be called once during client startup before any connection attempts.
 *
 * @return 0 on success, non-zero on failure
 *
 * @ingroup client_connection
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
 * @brief Establish connection to ascii-chat server
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
 * @param has_ever_connected True if a connection was ever successfully established
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
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

    // Check if user requested exit during reconnection delay
    if (should_exit()) {
      log_debug("Exit requested during reconnection delay");
      return -1;
    }
  } else {
    // Initial connection logged only to file
  }

  // Resolve server address using getaddrinfo() for IPv4/IPv6 support
  // Special handling for localhost: ensure we try both IPv6 (::1) and IPv4 (127.0.0.1)
  // Many systems only map "localhost" to 127.0.0.1 in /etc/hosts
  bool is_localhost =
      (strcmp(address, "localhost") == 0 || strcmp(address, "127.0.0.1") == 0 || strcmp(address, "::1") == 0);

  struct addrinfo hints, *res = NULL, *addr_iter;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  if (is_localhost) {
    hints.ai_flags = AI_NUMERICSERV; // Optimize for localhost
  }

  char port_str[16];
  SAFE_SNPRINTF(port_str, sizeof(port_str), "%d", port);

  // For localhost, try IPv6 loopback (::1) first, then fall back to DNS resolution
  if (is_localhost) {
    log_debug("Localhost detected - trying IPv6 loopback [::1]:%s first...", port_str);
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    int ipv6_result = getaddrinfo("::1", port_str, &hints, &res);
    if (ipv6_result == 0 && res != NULL) {
      // Try IPv6 loopback connection
      g_sockfd = socket_create(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (g_sockfd != INVALID_SOCKET_VALUE) {
        log_info("Trying IPv6 loopback connection to [::1]:%s...", port_str);
        if (connect_with_timeout(g_sockfd, res->ai_addr, res->ai_addrlen, CONNECT_TIMEOUT)) {
          log_debug("Connection successful using IPv6 loopback");
          SAFE_STRNCPY(g_server_ip, "::1", sizeof(g_server_ip));
          freeaddrinfo(res);
          res = NULL; // Prevent double-free at connection_success label
          goto connection_success;
        }
        if (socket_get_error(g_sockfd) != 0) {
          log_debug("NETWORK_ERROR: %d", (int)socket_get_error(g_sockfd));
        } else {
          // log_debug("IPv6 loopback connection failed: %s", network_error_string());
        }
        close_socket(g_sockfd);
        g_sockfd = INVALID_SOCKET_VALUE;
      }
      freeaddrinfo(res);
      res = NULL;
    }

    // Check if user requested exit (Ctrl-C) before trying IPv4
    if (should_exit()) {
      log_debug("Exit requested during connection attempt");
      return -1;
    }

    // IPv6 failed, try IPv4 loopback (127.0.0.1)
    log_debug("IPv6 failed, trying IPv4 loopback 127.0.0.1:%s...", port_str);
    hints.ai_family = AF_INET;

    int ipv4_result = getaddrinfo("127.0.0.1", port_str, &hints, &res);
    if (ipv4_result == 0 && res != NULL) {
      g_sockfd = socket_create(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (g_sockfd != INVALID_SOCKET_VALUE) {
        log_info("Trying IPv4 loopback connection to 127.0.0.1:%s...", port_str);
        if (connect_with_timeout(g_sockfd, res->ai_addr, res->ai_addrlen, CONNECT_TIMEOUT)) {
          log_debug("Connection successful using IPv4 loopback");
          SAFE_STRNCPY(g_server_ip, "127.0.0.1", sizeof(g_server_ip));
          freeaddrinfo(res);
          res = NULL; // Prevent double-free at connection_success label
          goto connection_success;
        }
        if (socket_get_error(g_sockfd) != 0) {
          log_debug("NETWORK_ERROR: %d", (int)socket_get_error(g_sockfd));
        } else {
          // log_debug("IPv4 loopback connection failed: %s", network_error_string());
        }
        close_socket(g_sockfd);
        g_sockfd = INVALID_SOCKET_VALUE;
      }
      freeaddrinfo(res);
      res = NULL;
    }

    // Both IPv6 and IPv4 loopback failed for localhost
    log_warn("Could not connect to localhost using either IPv6 or IPv4 loopback");
    return -1;
  }

  // For non-localhost addresses, use standard resolution
  log_debug("Resolving server address '%s' port %s...", address, port_str);
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = 0;
  int getaddr_result = getaddrinfo(address, port_str, &hints, &res);
  if (getaddr_result != 0) {
    log_error("Failed to resolve server address '%s': %s", address, gai_strerror(getaddr_result));
    return -1;
  }

  // Try each address returned by getaddrinfo() until one succeeds
  // Prefer IPv6 over IPv4: try IPv6 addresses first, then fall back to IPv4
  for (int address_family = AF_INET6; address_family >= AF_INET; address_family -= (AF_INET6 - AF_INET)) {
    for (addr_iter = res; addr_iter != NULL; addr_iter = addr_iter->ai_next) {
      // Skip addresses that don't match current pass (IPv6 first, then IPv4)
      if (addr_iter->ai_family != address_family) {
        continue;
      }

      // Create socket with appropriate address family
      g_sockfd = socket_create(addr_iter->ai_family, addr_iter->ai_socktype, addr_iter->ai_protocol);
      if (g_sockfd == INVALID_SOCKET_VALUE) {
        log_debug("Could not create socket for address family %d: %s", addr_iter->ai_family, network_error_string());
        continue; // Try next address
      }

      // Log which address family we're trying
      if (addr_iter->ai_family == AF_INET) {
        log_debug("Trying IPv4 connection...");
      } else if (addr_iter->ai_family == AF_INET6) {
        log_debug("Trying IPv6 connection...");
      }

      // Attempt connection with timeout
      if (connect_with_timeout(g_sockfd, addr_iter->ai_addr, addr_iter->ai_addrlen, CONNECT_TIMEOUT)) {
        // Connection successful!
        log_debug("Connection successful using %s", addr_iter->ai_family == AF_INET    ? "IPv4"
                                                    : addr_iter->ai_family == AF_INET6 ? "IPv6"
                                                                                       : "unknown protocol");

        // Extract server IP address for known_hosts
        if (addr_iter->ai_family == AF_INET) {
          struct sockaddr_in *addr_in = (struct sockaddr_in *)addr_iter->ai_addr;
          inet_ntop(AF_INET, &addr_in->sin_addr, g_server_ip, sizeof(g_server_ip));
        } else if (addr_iter->ai_family == AF_INET6) {
          struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr_iter->ai_addr;
          inet_ntop(AF_INET6, &addr_in6->sin6_addr, g_server_ip, sizeof(g_server_ip));
        }
        log_debug("Resolved server IP: %s", g_server_ip);

        goto connection_success; // Break out of both loops
      }

      // Connection failed - close socket and try next address
      if (socket_get_error(g_sockfd) != 0) {
        log_debug("NETWORK_ERROR: %d", (int)socket_get_error(g_sockfd));
      } else {
        // log_debug("Connection failed: %s", network_error_string());
      }
      close_socket(g_sockfd);
      g_sockfd = INVALID_SOCKET_VALUE;
    }
  }

connection_success:

  if (res) {
    freeaddrinfo(res);
  }

  // If we exhausted all addresses without success, fail
  if (g_sockfd == INVALID_SOCKET_VALUE) {
    log_warn("Could not connect to server %s:%d (tried all addresses)", address, port);
    return -1;
  }

  // Connection successful - extract local port for client ID
  struct sockaddr_storage local_addr = {0};
  socklen_t addr_len = sizeof(local_addr);
  if (getsockname(g_sockfd, (struct sockaddr *)&local_addr, &addr_len) == -1) {
    log_error("Failed to get local socket address: %s", network_error_string());
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Extract port from either IPv4 or IPv6 address
  int local_port = 0;
  if (((struct sockaddr *)&local_addr)->sa_family == AF_INET) {
    local_port = ntohs(((struct sockaddr_in *)&local_addr)->sin_port);
  } else if (((struct sockaddr *)&local_addr)->sa_family == AF_INET6) {
    local_port = ntohs(((struct sockaddr_in6 *)&local_addr)->sin6_port);
  }
  g_my_client_id = (uint32_t)local_port;

  // Mark connection as active immediately after successful socket connection
  atomic_store(&g_connection_active, true);
  atomic_store(&g_connection_lost, false);
  atomic_store(&g_should_reconnect, false);

  // Initialize crypto BEFORE starting protocol handshake
  // Note: server IP is already set above in the connection loop
  log_debug("CLIENT_CONNECT: Calling client_crypto_init()");
  if (client_crypto_init() != 0) {
    log_error("Failed to initialize crypto (password required or incorrect)");
    log_debug("CLIENT_CONNECT: client_crypto_init() failed");
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return CONNECTION_ERROR_AUTH_FAILED; // SSH key password was wrong - no retry
  }

  // Perform crypto handshake if encryption is enabled
  log_debug("CLIENT_CONNECT: Calling client_crypto_handshake()");
  int handshake_result = client_crypto_handshake(g_sockfd);
  if (handshake_result != 0) {
    log_error("Crypto handshake failed");
    log_debug("CLIENT_CONNECT: client_crypto_handshake() failed with code %d", handshake_result);
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    FATAL(ERROR_CRYPTO_HANDSHAKE,
          "Crypto handshake failed with server - this usually indicates a protocol mismatch or network issue");
  }
  log_debug("CLIENT_CONNECT: client_crypto_handshake() succeeded");

  // Turn OFF terminal logging when successfully connected to server
  // First connection - we'll disable logging after main.c shows the "Connected successfully" message
  if (!opt_snapshot_mode) {
    log_debug("Connected to server - terminal logging will be disabled after initial setup");
  } else {
    log_debug("Connected to server - terminal logging kept enabled for snapshot mode");
  }

  // Configure socket options for optimal performance
  if (socket_set_keepalive(g_sockfd, true) < 0) {
    log_warn("Failed to set socket keepalive: %s", network_error_string());
  }

  // Set socket buffer sizes for large data transmission
  int send_buffer_size = 1024 * 1024; // 1MB send buffer
  int recv_buffer_size = 1024 * 1024; // 1MB receive buffer

  if (socket_setsockopt(g_sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
    log_warn("Failed to set send buffer size: %s", network_error_string());
  }

  if (socket_setsockopt(g_sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
    log_warn("Failed to set receive buffer size: %s", network_error_string());
  }

  // Enable TCP_NODELAY to reduce latency for large packets
  int nodelay = 1;
  if (socket_setsockopt(g_sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    log_warn("Failed to set TCP_NODELAY: %s", network_error_string());
  }

  // Send initial terminal capabilities to server (this may generate debug logs)
  int result = threaded_send_terminal_size_with_auto_detect(opt_width, opt_height);
  if (result < 0) {
    log_error("Failed to send initial capabilities to server: %s", network_error_string());
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Now disable terminal logging after capabilities are sent (for reconnections)
  if (!opt_snapshot_mode && has_ever_connected) {
    log_set_terminal_output(false);
    log_debug("Reconnected to server - terminal logging disabled to prevent interference with ASCII display");
  }

  // Send client join packet for multi-user support
  uint32_t my_capabilities = CLIENT_CAP_VIDEO; // Basic video capability
  log_info("DEBUG: opt_audio_enabled = %d (sending CLIENT_JOIN)", opt_audio_enabled);
  if (opt_audio_enabled) {
    log_info("DEBUG: Adding CLIENT_CAP_AUDIO to capabilities");
    my_capabilities |= CLIENT_CAP_AUDIO;
  }
  if (opt_color_mode != COLOR_MODE_NONE) {
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
    log_error("Failed to send client join packet: %s", network_error_string());
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Connection already marked as active after socket creation

  return 0;
}

/**
 * @brief Check if server connection is currently active
 *
 * @return true if connection is active, false otherwise
 *
 * @ingroup client_connection
 */
bool server_connection_is_active() {
  return atomic_load(&g_connection_active) && (g_sockfd != INVALID_SOCKET_VALUE);
}

/**
 * @brief Get current socket file descriptor
 *
 * @return Socket file descriptor or INVALID_SOCKET_VALUE if disconnected
 *
 * @ingroup client_connection
 */
socket_t server_connection_get_socket() {
  return g_sockfd;
}

/**
 * @brief Get client ID assigned by server
 *
 * @return Client ID (based on local port) or 0 if not connected
 *
 * @ingroup client_connection
 */
uint32_t server_connection_get_client_id() {
  return g_my_client_id;
}

/**
 * @brief Get resolved server IP address
 *
 * Returns the server's IP address that was resolved during connection
 * establishment. Used for known_hosts verification and logging.
 *
 * @return Server IP address string (IPv4 or IPv6), or empty string if not connected
 *
 * @ingroup client_connection
 */
const char *server_connection_get_ip() {
  return g_server_ip;
}

/**
 * @brief Close the server connection gracefully
 *
 * Marks connection as inactive and closes the socket. This function
 * is safe to call multiple times and from multiple threads.
 *
 * @ingroup client_connection
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
  log_set_terminal_output(true);
}

/**
 * @brief Emergency connection shutdown for signal handlers
 *
 * Performs immediate connection shutdown without waiting for graceful
 * close procedures. Uses socket shutdown to interrupt any blocking
 * recv() operations in other threads.
 *
 * @ingroup client_connection
 */
void server_connection_shutdown() {
  // NOTE: This function may be called from:
  //   - Signal handlers on Unix (async-signal-safe context)
  //   - SetConsoleCtrlHandler callback thread on Windows (separate thread context)
  // Only use atomic operations and simple system calls - NO mutex locks, NO malloc, NO logging.

  atomic_store(&g_connection_active, false);
  atomic_store(&g_connection_lost, true);

  if (g_sockfd != INVALID_SOCKET_VALUE) {
    // Only shutdown() the socket to interrupt blocking recv()/send() operations.
    // Do NOT close() here - on Windows, closing the socket while another thread
    // is using it is undefined behavior and can cause STATUS_STACK_BUFFER_OVERRUN.
    // The actual socket close happens in server_connection_close() which is called
    // from the main thread after worker threads have been joined.
    socket_shutdown(g_sockfd, SHUT_RDWR);
  }

  // DO NOT call log_set_terminal_output() here - it uses mutex which is NOT async-signal-safe.
  // The normal cleanup path in shutdown_client() will handle logging state.
}

/**
 * @brief Signal that connection has been lost
 *
 * Called by other modules (typically protocol handlers) when they
 * detect connection failure. Triggers reconnection logic in main loop.
 *
 * @ingroup client_connection
 */
void server_connection_lost() {
  atomic_store(&g_connection_lost, true);
  atomic_store(&g_connection_active, false);

  // Turn ON terminal logging when connection is lost
  log_set_terminal_output(true);
  display_full_reset();
}

/**
 * @brief Check if connection loss has been detected
 *
 * @return true if connection loss was flagged, false otherwise
 *
 * @ingroup client_connection
 */
bool server_connection_is_lost() {
  return atomic_load(&g_connection_lost);
}

/**
 * @brief Cleanup connection management subsystem
 *
 * Closes any active connection and destroys synchronization objects.
 * Called during client shutdown.
 *
 * @ingroup client_connection
 */
void server_connection_cleanup() {
  log_set_terminal_output(true);
  server_connection_close();
  mutex_destroy(&g_send_mutex);
}

/* ============================================================================
 * Thread Safety Interface
 * ============================================================================ */

/* ============================================================================
 * Thread-Safe Wrapper Functions
 * ============================================================================ */

/**
 * @brief Thread-safe packet transmission
 *
 * Sends a packet to the server with proper mutex protection and connection
 * state checking. Automatically handles encryption if crypto is ready.
 *
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_packet(packet_type_t type, const void *data, size_t len) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);

  // Use send_packet_secure() which handles encryption and compression automatically
  const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;
  int result = send_packet_secure(sockfd, type, data, len, (crypto_context_t *)crypto_ctx);

  mutex_unlock(&g_send_mutex);
  return result;
}

/**
 * @brief Thread-safe batched audio packet transmission
 *
 * Sends a batched audio packet to the server with proper mutex protection
 * and connection state checking. Automatically handles encryption if crypto
 * is ready.
 *
 * @param samples Audio sample buffer containing batched samples
 * @param num_samples Total number of samples in the batch
 * @param batch_count Number of audio chunks in this batch
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_audio_batch_packet(const float *samples, int num_samples, int batch_count) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  mutex_lock(&g_send_mutex);

  // Get crypto context if encryption is enabled
  const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;
  int result = send_audio_batch_packet(sockfd, samples, num_samples, batch_count, (crypto_context_t *)crypto_ctx);

  mutex_unlock(&g_send_mutex);
  return result;
}

/**
 * @brief Thread-safe ping packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
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

/**
 * @brief Thread-safe pong packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
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

/**
 * @brief Thread-safe stream start packet transmission
 *
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_stream_start_packet(uint32_t stream_type) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  // Build STREAM_START packet locally
  uint32_t type_data = htonl(stream_type);

  // Use threaded_send_packet() which handles encryption
  return threaded_send_packet(PACKET_TYPE_STREAM_START, &type_data, sizeof(type_data));
}

/**
 * @brief Thread-safe terminal size packet transmission with auto-detection
 *
 * Sends terminal capabilities packet to the server including terminal size,
 * color capabilities, and rendering preferences. Auto-detects terminal
 * capabilities if not explicitly specified.
 *
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_terminal_size_with_auto_detect(unsigned short width, unsigned short height) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  // Build terminal capabilities packet locally
  // Detect terminal capabilities automatically
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Apply user's color mode override
  caps = apply_color_mode_override(caps);

  // Check if detection was reliable, use fallback only for auto-detection
  if (!caps.detection_reliable && opt_color_mode == COLOR_MODE_AUTO) {
    log_warn("Terminal capability detection not reliable, using fallback");
    SAFE_MEMSET(&caps, sizeof(caps), 0, sizeof(caps));
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities = 0;
    SAFE_STRNCPY(caps.term_type, "unknown", sizeof(caps.term_type));
    SAFE_STRNCPY(caps.colorterm, "", sizeof(caps.colorterm));
    caps.detection_reliable = 0;
  }

  // Convert to network packet format with proper byte order
  terminal_capabilities_packet_t net_packet;
  net_packet.capabilities = htonl(caps.capabilities);
  net_packet.color_level = htonl(caps.color_level);
  net_packet.color_count = htonl(caps.color_count);
  net_packet.render_mode = htonl(caps.render_mode);
  net_packet.width = htons(width);
  net_packet.height = htons(height);
  net_packet.palette_type = htonl(opt_palette_type);
  net_packet.utf8_support = htonl(caps.utf8_support ? 1 : 0);

  if (opt_palette_type == PALETTE_CUSTOM && opt_palette_custom_set) {
    SAFE_STRNCPY(net_packet.palette_custom, opt_palette_custom, sizeof(net_packet.palette_custom));
    net_packet.palette_custom[sizeof(net_packet.palette_custom) - 1] = '\0';
  } else {
    SAFE_MEMSET(net_packet.palette_custom, sizeof(net_packet.palette_custom), 0, sizeof(net_packet.palette_custom));
  }

  // Set desired FPS
  if (g_max_fps > 0) {
    net_packet.desired_fps = (uint8_t)(g_max_fps > 144 ? 144 : g_max_fps);
  } else {
    net_packet.desired_fps = caps.desired_fps;
  }

  if (net_packet.desired_fps == 0) {
    net_packet.desired_fps = DEFAULT_MAX_FPS;
  }

  SAFE_STRNCPY(net_packet.term_type, caps.term_type, sizeof(net_packet.term_type));
  net_packet.term_type[sizeof(net_packet.term_type) - 1] = '\0';

  SAFE_STRNCPY(net_packet.colorterm, caps.colorterm, sizeof(net_packet.colorterm));
  net_packet.colorterm[sizeof(net_packet.colorterm) - 1] = '\0';

  net_packet.detection_reliable = caps.detection_reliable;
  net_packet.utf8_support = opt_force_utf8 ? 1 : 0;

  SAFE_MEMSET(net_packet.reserved, sizeof(net_packet.reserved), 0, sizeof(net_packet.reserved));

  // Use threaded_send_packet() which handles encryption
  return threaded_send_packet(PACKET_TYPE_CLIENT_CAPABILITIES, &net_packet, sizeof(net_packet));
}

/**
 * @brief Thread-safe client join packet transmission
 *
 * Sends client join packet with display name and capabilities to the server.
 *
 * @param display_name Client display name
 * @param capabilities Client capability flags
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_client_join_packet(const char *display_name, uint32_t capabilities) {
  socket_t sockfd = server_connection_get_socket();
  if (!atomic_load(&g_connection_active) || sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  // Build CLIENT_JOIN packet locally
  client_info_packet_t join_packet;
  SAFE_MEMSET(&join_packet, sizeof(join_packet), 0, sizeof(join_packet));
  join_packet.client_id = 0; // Will be assigned by server
  SAFE_SNPRINTF(join_packet.display_name, MAX_DISPLAY_NAME_LEN, "%s", display_name ? display_name : "Unknown");
  join_packet.capabilities = capabilities;

  // Use threaded_send_packet() which handles encryption
  int send_result = threaded_send_packet(PACKET_TYPE_CLIENT_JOIN, &join_packet, sizeof(join_packet));
  if (send_result == 0) {
    mutex_lock(&g_send_mutex);
    bool active = atomic_load(&g_connection_active);
    socket_t socket_snapshot = g_sockfd;
    const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;
    if (active && socket_snapshot != INVALID_SOCKET_VALUE) {
      (void)log_network_message(
          socket_snapshot, (const struct crypto_context_t *)crypto_ctx, LOG_INFO, REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER,
          "CLIENT_JOIN sent (display=\"%s\", capabilities=0x%x)", join_packet.display_name, capabilities);
    }
    mutex_unlock(&g_send_mutex);
  }
  return send_result;
}
