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
#include "../main.h" // Global exit API
#include "crypto.h"
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/common.h>

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/url.h> // For WebSocket URL detection
#include <ascii-chat/common.h>
#include "display.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/video/palette.h>
#include <ascii-chat/buffer_pool.h>

#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <stdatomic.h>

#include <ascii-chat/platform/network.h> // Consolidates platform-specific network headers (includes TCP options)

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
 * @brief ACIP transport for server connection
 *
 * Wraps the socket connection with ACIP protocol abstraction. Created after
 * crypto handshake completes, destroyed when connection closes.
 *
 * @ingroup client_connection
 */
static acip_transport_t *g_client_transport = NULL;

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
static unsigned int get_reconnect_delay(unsigned int reconnect_attempt) {
  // Use integer arithmetic for microsecond calculations
  // Initial delay: 100,000 us (0.1 seconds)
  // Additional delay per attempt: 200,000 us (0.2 seconds)
  unsigned int delay_us = 100 * US_PER_MS_INT + (reconnect_attempt - 1) * 200 * US_PER_MS_INT;
  if (delay_us > MAX_RECONNECT_DELAY)
    delay_us = MAX_RECONNECT_DELAY;
  return delay_us;
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
    platform_sleep_us(50 * US_PER_MS_INT); // 50ms delay

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
  if (mutex_init(&g_send_mutex, "send") != 0) {
    log_error("Failed to initialize send mutex");
    return -1;
  }

  // Initialize connection state
  g_sockfd = INVALID_SOCKET_VALUE;
  g_client_transport = NULL;
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
    unsigned int delay_us = get_reconnect_delay(reconnect_attempt);
    // Reconnection attempt logged only to file
    platform_sleep_us(delay_us);

    // Check if user requested exit during reconnection delay
    if (should_exit()) {
      log_debug("Exit requested during reconnection delay");
      return -1;
    }
  } else {
    // Initial connection logged only to file
  }

  // Check for WebSocket URL - handle separately from TCP
  if (url_is_websocket(address)) {
    // WebSocket connection - bypass TCP socket creation
    // Use the original address (URL already contains port if specified)
    const char *ws_url = address;

    // Parse for debug logging
    url_parts_t url_parts = {0};
    if (url_parse(address, &url_parts) == ASCIICHAT_OK) {
      log_info("Connecting via WebSocket: %s (scheme=%s, host=%s, port=%d)", ws_url, url_parts.scheme, url_parts.host,
               url_parts.port);
    } else {
      log_info("Connecting via WebSocket: %s", ws_url);
    }

    // Initialize crypto if encryption is enabled
    log_debug("CLIENT_CONNECT: Calling client_crypto_init()");
    if (client_crypto_init() != 0) {
      log_error("Failed to initialize crypto (password required or incorrect)");
      log_debug("CLIENT_CONNECT: client_crypto_init() failed");
      url_parts_destroy(&url_parts);
      return CONNECTION_ERROR_AUTH_FAILED;
    }

    // Get crypto context for transport
    const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;

    // Create WebSocket transport (handles connection internally)
    g_client_transport = acip_websocket_client_transport_create("client", ws_url, (crypto_context_t *)crypto_ctx);
    if (!g_client_transport) {
      log_error("Failed to create WebSocket ACIP transport");
      url_parts_destroy(&url_parts);
      return -1;
    }
    log_debug("CLIENT_CONNECT: Created WebSocket ACIP transport with crypto context");

    // Set connection as active
    atomic_store(&g_connection_active, true);
    atomic_store(&g_connection_lost, false);

    // Send initial terminal capabilities to server
    int result = threaded_send_terminal_size_with_auto_detect(GET_OPTION(width), GET_OPTION(height));
    if (result < 0) {
      log_error("Failed to send initial capabilities to server: %s", network_error_string());
      acip_transport_destroy(g_client_transport);
      g_client_transport = NULL;
      url_parts_destroy(&url_parts);
      return -1;
    }

    // Disable terminal logging after initial setup (for non-snapshot mode)
    if (!GET_OPTION(snapshot_mode) && has_ever_connected) {
      log_set_terminal_output(false);
    }

    // Build capabilities flags
    uint32_t my_capabilities = CLIENT_CAP_VIDEO;
    log_debug("GET_OPTION(audio_enabled) = %d (sending CLIENT_JOIN)", GET_OPTION(audio_enabled));
    if (GET_OPTION(audio_enabled)) {
      log_debug("Adding CLIENT_CAP_AUDIO to capabilities");
      my_capabilities |= CLIENT_CAP_AUDIO;
    }
    if (GET_OPTION(color_mode) != COLOR_MODE_NONE) {
      my_capabilities |= CLIENT_CAP_COLOR;
    }
    if (GET_OPTION(stretch)) {
      my_capabilities |= CLIENT_CAP_STRETCH;
    }

    // Generate display name from username + PID
    const char *display_name = platform_get_username();
    char my_display_name[MAX_DISPLAY_NAME_LEN];
    int pid = getpid();
    SAFE_SNPRINTF(my_display_name, sizeof(my_display_name), "%s-%d", display_name, pid);
    if (threaded_send_client_join_packet(my_display_name, my_capabilities) < 0) {
      log_error("Failed to send client join packet: %s", network_error_string());
      acip_transport_destroy(g_client_transport);
      g_client_transport = NULL;
      url_parts_destroy(&url_parts);
      return -1;
    }

    log_info("WebSocket connection established successfully");
    url_parts_destroy(&url_parts);
    return 0;
  }

  // Resolve server address using getaddrinfo() for IPv4/IPv6 support
  // Special handling for localhost: ensure we try both IPv6 (::1) and IPv4 (127.0.0.1)
  // Many systems only map "localhost" to 127.0.0.1 in /etc/hosts
  bool is_localhost = (strcmp(address, "localhost") == 0 || is_localhost_ipv4(address) || is_localhost_ipv6(address));

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
      g_sockfd = socket_create("client_socket_ipv6", res->ai_family, res->ai_socktype, res->ai_protocol);
      if (g_sockfd != INVALID_SOCKET_VALUE) {
        log_debug("Trying IPv6 loopback connection to [::1]:%s...", port_str);
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
      g_sockfd = socket_create("client_socket_ipv4", res->ai_family, res->ai_socktype, res->ai_protocol);
      if (g_sockfd != INVALID_SOCKET_VALUE) {
        log_debug("Trying IPv4 loopback connection to 127.0.0.1:%s...", port_str);
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
      const char *socket_name =
          (addr_iter->ai_family == AF_INET6) ? "client_socket_server_ipv6" : "client_socket_server_ipv4";
      g_sockfd = socket_create(socket_name, addr_iter->ai_family, addr_iter->ai_socktype, addr_iter->ai_protocol);
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
        if (format_ip_address(addr_iter->ai_family, addr_iter->ai_addr, g_server_ip, sizeof(g_server_ip)) ==
            ASCIICHAT_OK) {
          log_debug("Resolved server IP: %s", g_server_ip);
        } else {
          log_warn("Failed to format server IP address");
        }

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
    local_port = NET_TO_HOST_U16(((struct sockaddr_in *)&local_addr)->sin_port);
  } else if (((struct sockaddr *)&local_addr)->sa_family == AF_INET6) {
    local_port = NET_TO_HOST_U16(((struct sockaddr_in6 *)&local_addr)->sin6_port);
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

  // Create ACIP transport for protocol-agnostic packet sending
  // The transport wraps the socket with encryption context from the handshake
  const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;

  // Create TCP transport (WebSocket is handled earlier in the function)
  g_client_transport = acip_tcp_transport_create("client", g_sockfd, (crypto_context_t *)crypto_ctx);
  if (!g_client_transport) {
    log_error("Failed to create TCP ACIP transport");
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }
  log_debug("CLIENT_CONNECT: Created TCP ACIP transport with crypto context");

  // Turn OFF terminal logging when successfully connected to server
  // First connection - we'll disable logging after main.c shows the "Connected successfully" message
  if (!GET_OPTION(snapshot_mode)) {
    log_debug("Connected to server - terminal logging will be disabled after initial setup");
  } else {
    log_debug("Connected to server - terminal logging kept enabled for snapshot mode");
  }

  // Configure socket options for optimal performance
  if (socket_set_keepalive(g_sockfd, true) < 0) {
    log_warn("Failed to set socket keepalive: %s", network_error_string());
  }

  // Configure socket buffers and TCP_NODELAY for optimal performance
  asciichat_error_t sock_config_result = socket_configure_buffers(g_sockfd);
  if (sock_config_result != ASCIICHAT_OK) {
    log_warn("Failed to configure socket: %s", network_error_string());
  }

  // Send initial terminal capabilities to server (this may generate debug logs)
  int result = threaded_send_terminal_size_with_auto_detect(GET_OPTION(width), GET_OPTION(height));
  if (result < 0) {
    log_error("Failed to send initial capabilities to server: %s", network_error_string());
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Now disable terminal logging after capabilities are sent (for reconnections)
  if (!GET_OPTION(snapshot_mode) && has_ever_connected) {
    log_set_terminal_output(false);
    log_debug("Reconnected to server - terminal logging disabled to prevent interference with ASCII display");
  }

  // Send client join packet for multi-user support
  uint32_t my_capabilities = CLIENT_CAP_VIDEO; // Basic video capability
  log_debug("GET_OPTION(audio_enabled) = %d (sending CLIENT_JOIN)", GET_OPTION(audio_enabled));
  if (GET_OPTION(audio_enabled)) {
    log_debug("Adding CLIENT_CAP_AUDIO to capabilities");
    my_capabilities |= CLIENT_CAP_AUDIO;
  }
  if (GET_OPTION(color_mode) != COLOR_MODE_NONE) {
    my_capabilities |= CLIENT_CAP_COLOR;
  }
  if (GET_OPTION(stretch)) {
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
  // For TCP: check socket validity
  // For WebRTC: socket is INVALID_SOCKET_VALUE but transport exists
  return atomic_load(&g_connection_active) && (g_sockfd != INVALID_SOCKET_VALUE || g_client_transport != NULL);
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
 * @brief Get ACIP transport instance
 *
 * @return Transport instance or NULL if not connected
 *
 * @ingroup client_connection
 */
acip_transport_t *server_connection_get_transport(void) {
  return g_client_transport;
}

/**
 * @brief Set ACIP transport instance from connection fallback
 *
 * Used to integrate the transport from the 3-stage connection fallback orchestrator
 * (TCP → STUN → TURN) into the server connection management layer.
 *
 * @param transport Transport instance created by connection_attempt_with_fallback()
 *
 * @ingroup client_connection
 */
void server_connection_set_transport(acip_transport_t *transport) {
  log_debug("[TRANSPORT_LIFECYCLE] server_connection_set_transport() called with transport=%p", (void *)transport);

  // Clean up any existing transport
  if (g_client_transport) {
    log_warn("[TRANSPORT_LIFECYCLE] Replacing existing transport=%p with new transport=%p (potential premature "
             "destruction?)",
             (void *)g_client_transport, (void *)transport);
    log_debug("[TRANSPORT_LIFECYCLE] OLD transport is_connected=%s before destruction",
              acip_transport_is_connected(g_client_transport) ? "true" : "false");
    acip_transport_destroy(g_client_transport);
    log_debug("[TRANSPORT_LIFECYCLE] OLD transport destroyed");
  }

  log_debug("[TRANSPORT_LIFECYCLE] Setting g_client_transport to %p", (void *)transport);
  g_client_transport = transport;

  // Mark connection as active when transport is set
  if (transport) {
    log_debug("[TRANSPORT_LIFECYCLE] Transport is non-NULL, extracting socket...");
    // Extract socket from transport for backward compatibility with socket-based checks
    g_sockfd = acip_transport_get_socket(transport);
    log_debug("[TRANSPORT_LIFECYCLE] Socket extracted: %d from transport=%p", (int)g_sockfd, (void *)transport);

    atomic_store(&g_connection_active, true);
    atomic_store(&g_connection_lost, false); // Reset lost flag for new connection
    log_debug("[TRANSPORT_LIFECYCLE] Server connection transport set and marked active (transport=%p, sockfd=%d, "
              "is_connected=%s)",
              (void *)transport, (int)g_sockfd, acip_transport_is_connected(transport) ? "true" : "false");
  } else {
    g_sockfd = INVALID_SOCKET_VALUE;
    atomic_store(&g_connection_active, false);
    log_debug("[TRANSPORT_LIFECYCLE] Server connection transport cleared and marked inactive");
  }

  log_debug("[TRANSPORT_LIFECYCLE] server_connection_set_transport() completed");
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
 * @brief Set the server IP address
 *
 * Updates the global server IP address. Used by new connection code paths
 * that don't use the legacy server_connect() function.
 *
 * @param ip Server IP address string
 *
 * @ingroup client_connection
 */
void server_connection_set_ip(const char *ip) {
  if (ip) {
    SAFE_STRNCPY(g_server_ip, ip, sizeof(g_server_ip));
    log_debug("Server IP set to: %s", g_server_ip);
  } else {
    g_server_ip[0] = '\0';
    log_debug("Server IP cleared");
  }
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
  log_debug("[TRANSPORT_LIFECYCLE] server_connection_close() called");
  atomic_store(&g_connection_active, false);

  // Destroy ACIP transport before closing socket
  if (g_client_transport) {
    log_debug("[TRANSPORT_LIFECYCLE] Destroying transport=%p (is_connected=%s) before closing socket",
              (void *)g_client_transport, acip_transport_is_connected(g_client_transport) ? "true" : "false");
    acip_transport_destroy(g_client_transport);
    g_client_transport = NULL;
    log_debug("[TRANSPORT_LIFECYCLE] Transport destroyed, g_client_transport set to NULL");
  }

  if (g_sockfd != INVALID_SOCKET_VALUE) {
    log_debug("[TRANSPORT_LIFECYCLE] Closing socket: %d", (int)g_sockfd);
    close_socket(g_sockfd);
    g_sockfd = INVALID_SOCKET_VALUE;
    log_debug("[TRANSPORT_LIFECYCLE] Socket closed");
  }

  g_my_client_id = 0;

  // Cleanup crypto context if encryption was enabled
  if (g_encryption_enabled) {
    log_debug("[TRANSPORT_LIFECYCLE] Cleaning up crypto context");
    crypto_handshake_destroy(&g_crypto_ctx);
    g_encryption_enabled = false;
  }

  // Turn ON terminal logging when connection is closed (unless it was disabled with --quiet)
  if (!GET_OPTION(quiet)) {
    log_set_terminal_output(true);
  }
  log_debug("[TRANSPORT_LIFECYCLE] server_connection_close() completed");
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
  asciichat_error_context_t err_ctx;
  fprintf(stderr, "★ SERVER_CONNECTION_LOST: errno=%d, msg=%s\n", HAS_ERRNO(&err_ctx) ? err_ctx.code : -1,
          HAS_ERRNO(&err_ctx) ? err_ctx.context_message : "no error context");
  fflush(stderr);
  atomic_store(&g_connection_lost, true);
  atomic_store(&g_connection_active, false);

  // Don't re-enable terminal logging here - let the splash screen handle it
  // The reconnection splash will capture and display logs properly
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
  if (!GET_OPTION(quiet)) {
    log_set_terminal_output(true);
  }
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
asciichat_error_t threaded_send_packet(packet_type_t type, const void *data, size_t len) {
  // Lock mutex for entire send operation to prevent concurrent socket writes
  mutex_lock(&g_send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load(&g_connection_active) || !g_client_transport) {
    log_debug("[TRANSPORT_LIFECYCLE] threaded_send_packet() check failed: active=%s, transport=%p",
              atomic_load(&g_connection_active) ? "true" : "false", (void *)g_client_transport);
    mutex_unlock(&g_send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active or transport unavailable");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = g_client_transport;
  log_debug_every(LOG_RATE_SLOW, "[TRANSPORT_LIFECYCLE] threaded_send_packet() using transport=%p, is_connected=%s",
                  (void *)transport, acip_transport_is_connected(transport) ? "true" : "false");

  // Network I/O happens while holding mutex to prevent concurrent socket writes
  asciichat_error_t result = packet_send_via_transport(transport, type, data, len, 0);

  // Unlock after send completes
  mutex_unlock(&g_send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    log_debug("[TRANSPORT_LIFECYCLE] threaded_send_packet() send failed, calling server_connection_lost()");
    server_connection_lost();
    return result;
  }

  return ASCIICHAT_OK;
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
  // Lock mutex for entire send operation to prevent concurrent socket writes
  mutex_lock(&g_send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load(&g_connection_active) || !g_client_transport) {
    mutex_unlock(&g_send_mutex);
    return -1;
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = g_client_transport;

  // Network I/O happens while holding mutex to prevent concurrent socket writes
  asciichat_error_t result = acip_send_audio_batch(transport, samples, (uint32_t)num_samples, (uint32_t)batch_count);

  // Unlock after send completes
  mutex_unlock(&g_send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    server_connection_lost();
    return -1;
  }

  return 0;
}

/**
 * @brief Thread-safe Opus audio frame transmission
 *
 * Sends a single Opus-encoded audio frame to the server with proper
 * synchronization and encryption support.
 *
 * @param opus_data Opus-encoded audio data
 * @param opus_size Size of encoded frame
 * @param sample_rate Sample rate in Hz
 * @param frame_duration Frame duration in milliseconds
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_opus(const uint8_t *opus_data, size_t opus_size, int sample_rate,
                                           int frame_duration) {
  // Get transport reference while holding mutex (brief lock)
  mutex_lock(&g_send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load(&g_connection_active) || !g_client_transport) {
    mutex_unlock(&g_send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active or transport unavailable");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = g_client_transport;
  mutex_unlock(&g_send_mutex);

  // Build Opus packet with header (outside mutex - no blocking I/O yet)
  size_t header_size = 16; // sample_rate (4), frame_duration (4), reserved (8)
  size_t total_size = header_size + opus_size;
  void *packet_data = buffer_pool_alloc(NULL, total_size);
  if (!packet_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for Opus packet: %zu bytes", total_size);
  }

  // Write header in network byte order
  uint8_t *buf = (uint8_t *)packet_data;
  uint32_t sr = HOST_TO_NET_U32((uint32_t)sample_rate);
  uint32_t fd = HOST_TO_NET_U32((uint32_t)frame_duration);
  memcpy(buf, &sr, 4);
  memcpy(buf + 4, &fd, 4);
  memset(buf + 8, 0, 8); // Reserved

  // Copy Opus data
  memcpy(buf + header_size, opus_data, opus_size);

  // Network I/O happens OUTSIDE the mutex to prevent deadlock on TCP buffer full
  asciichat_error_t result =
      packet_send_via_transport(transport, PACKET_TYPE_AUDIO_OPUS_BATCH, packet_data, total_size, 0);

  // Clean up
  buffer_pool_free(NULL, packet_data, total_size);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    server_connection_lost();
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe Opus audio batch packet transmission
 *
 * Sends a batch of Opus-encoded audio frames to the server with proper
 * synchronization and encryption support.
 *
 * @param opus_data Opus-encoded audio data (multiple frames concatenated)
 * @param opus_size Total size of Opus data in bytes
 * @param frame_sizes Array of individual frame sizes (variable-length frames)
 * @param frame_count Number of Opus frames in the batch
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_opus_batch(const uint8_t *opus_data, size_t opus_size,
                                                 const uint16_t *frame_sizes, int frame_count) {
  // Lock mutex for entire send operation to prevent concurrent socket writes
  mutex_lock(&g_send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load(&g_connection_active) || !g_client_transport) {
    mutex_unlock(&g_send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active or transport unavailable");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = g_client_transport;

  // Network I/O happens while holding mutex to prevent concurrent socket writes
  // Opus uses 20ms frames at 48kHz (960 samples = 20ms)
  asciichat_error_t result =
      acip_send_audio_opus_batch(transport, opus_data, opus_size, frame_sizes, frame_count, 48000, 20);

  // Unlock after send completes
  mutex_unlock(&g_send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    server_connection_lost();
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe ping packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_ping_packet(void) {
  // Use threaded_send_packet which handles encryption, mutex locking, and connection state
  return threaded_send_packet(PACKET_TYPE_PING, NULL, 0);
}

/**
 * @brief Thread-safe pong packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_pong_packet(void) {
  // Use threaded_send_packet which handles encryption, mutex locking, and connection state
  return threaded_send_packet(PACKET_TYPE_PONG, NULL, 0);
}

/**
 * @brief Thread-safe stream start packet transmission
 *
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_stream_start_packet(uint32_t stream_type) {
  // Connection and transport availability is checked by threaded_send_packet()

  // Build STREAM_START packet locally
  uint32_t type_data = HOST_TO_NET_U32(stream_type);

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
asciichat_error_t threaded_send_terminal_size_with_auto_detect(unsigned short width, unsigned short height) {
  // Log the dimensions being sent to server (helps debug dimension mismatch issues)
  log_debug("Sending terminal size to server: %ux%u (auto_width=%d, auto_height=%d)", width, height,
            GET_OPTION(auto_width), GET_OPTION(auto_height));

  // Connection and transport availability is checked by threaded_send_packet()

  // Build terminal capabilities packet locally
  // Detect terminal capabilities automatically
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Set wants_padding based on snapshot mode and TTY status
  // Disable padding when:
  // - In snapshot mode (one frame and exit)
  // - When stdout is not a TTY (piped/redirected output)
  // Enable padding for interactive terminal sessions
  bool is_snapshot_mode = GET_OPTION(snapshot_mode);
  bool is_interactive = terminal_is_interactive();
  caps.wants_padding = is_interactive && !is_snapshot_mode;

  log_debug("Client capabilities: wants_padding=%d (snapshot=%d, interactive=%d, stdin_tty=%d, stdout_tty=%d)",
            caps.wants_padding, is_snapshot_mode, is_interactive, terminal_is_stdin_tty(), terminal_is_stdout_tty());

  // Apply user's color mode override
  caps = apply_color_mode_override(caps);

  // Check if detection was reliable, use fallback only for auto-detection
  if (!caps.detection_reliable && GET_OPTION(color_mode) == COLOR_MODE_AUTO) {
    log_warn("Terminal capability detection not reliable, using fallback");
    SAFE_MEMSET(&caps, sizeof(caps), 0, sizeof(caps));
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities = 0;
    SAFE_STRNCPY(caps.term_type, "unknown", sizeof(caps.term_type));
    SAFE_STRNCPY(caps.colorterm, "", sizeof(caps.colorterm));
    caps.detection_reliable = 0;
    // Preserve wants_padding even in fallback mode
    caps.wants_padding = is_interactive && !is_snapshot_mode;
  }

  // Convert to network packet format with proper byte order
  terminal_capabilities_packet_t net_packet;
  net_packet.capabilities = HOST_TO_NET_U32(caps.capabilities);
  net_packet.color_level = HOST_TO_NET_U32(caps.color_level);
  net_packet.color_count = HOST_TO_NET_U32(caps.color_count);
  net_packet.render_mode = HOST_TO_NET_U32(caps.render_mode);
  net_packet.width = HOST_TO_NET_U16(width);
  net_packet.height = HOST_TO_NET_U16(height);
  net_packet.palette_type = HOST_TO_NET_U32(GET_OPTION(palette_type));
  net_packet.utf8_support = HOST_TO_NET_U32(caps.utf8_support ? 1 : 0);

  const options_t *opts = options_get();
  if (GET_OPTION(palette_type) == PALETTE_CUSTOM && GET_OPTION(palette_custom_set)) {
    const char *palette_custom = opts && opts->palette_custom_set ? opts->palette_custom : "";
    SAFE_STRNCPY(net_packet.palette_custom, palette_custom, sizeof(net_packet.palette_custom));
    net_packet.palette_custom[sizeof(net_packet.palette_custom) - 1] = '\0';
  } else {
    SAFE_MEMSET(net_packet.palette_custom, sizeof(net_packet.palette_custom), 0, sizeof(net_packet.palette_custom));
  }

  // Set desired FPS
  int fps = GET_OPTION(fps);
  if (fps > 0) {
    net_packet.desired_fps = (uint8_t)(fps > 144 ? 144 : fps);
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
  // Send UTF-8 support flag: true for AUTO (default) and TRUE settings, false for FALSE setting
  net_packet.utf8_support = (GET_OPTION(force_utf8) != UTF8_SETTING_FALSE) ? 1 : 0;

  // Set wants_padding flag (1=padding enabled, 0=no padding for snapshot/piped modes)
  net_packet.wants_padding = caps.wants_padding ? 1 : 0;

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
  // Connection and transport availability is checked by threaded_send_packet()

  // Build CLIENT_JOIN packet locally
  client_info_packet_t join_packet;
  SAFE_MEMSET(&join_packet, sizeof(join_packet), 0, sizeof(join_packet));
  join_packet.client_id = HOST_TO_NET_U32(0); // Will be assigned by server
  SAFE_SNPRINTF(join_packet.display_name, MAX_DISPLAY_NAME_LEN, "%s", display_name ? display_name : "Unknown");
  join_packet.capabilities = HOST_TO_NET_U32(capabilities);

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
