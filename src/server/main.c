/**
 * @file server/main.c
 * @ingroup server_main
 * @brief 🖥️ Server main entry point: multi-client connection manager with per-client rendering threads (60fps video +
 * 172fps audio)
 *
 * MODULAR COMPONENTS:
 * ===================
 * - main.c (this file): Server initialization, signal handling, connection management
 * - client.c:           Per-client lifecycle, threading, and state management
 * - protocol.c:         Network packet processing and protocol implementation
 * - stream.c:           Video mixing, ASCII frame generation, and caching
 * - render.c:           Per-client rendering threads with rate limiting
 * - stats.c:            Performance monitoring and resource tracking
 *
 * CONCURRENCY MODEL:
 * ==================
 * The server creates multiple thread types per client:
 * 1. Receive thread: Handles incoming packets from client (protocol.c functions)
 * 2. Send thread: Manages outgoing packet delivery (client.c)
 * 3. Video render thread: Generates ASCII frames at 60fps (render.c)
 * 4. Audio render thread: Mixes audio streams at 172fps (render.c)
 * 5. Stats logger thread: Periodic performance reporting (stats.c)
 *
 * CRITICAL THREAD SAFETY:
 * ========================
 * - Lock ordering: Always acquire g_client_manager_rwlock BEFORE per-client mutexes
 * - Snapshot pattern: Copy client state under mutex, then process without locks
 * - Signal-safe shutdown: SIGINT handler only sets flags and closes sockets
 * - Deterministic cleanup: Main thread waits for all worker threads before exit
 *
 * WHY THE REFACTORING:
 * ====================
 * The original server.c became unmaintainable at 2408+ lines, making it:
 * - Too large for LLM context windows (limited AI-assisted development)
 * - Difficult for humans to navigate and understand
 * - Slow to compile and modify
 * - Hard to isolate bugs and add new features
 *
 * The modular design enables:
 * - Faster development cycles (smaller compilation units)
 * - Better IDE support (jump-to-definition, IntelliSense)
 * - Easier testing and debugging (isolated components)
 * - Future extensibility (new protocols, renderers, optimizations)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 */

#ifdef _WIN32
#include <io.h>
#include <ws2tcpip.h> // For getaddrinfo(), gai_strerror(), inet_ntop()
#include <winsock2.h>
#include <mmsystem.h> // For timeEndPeriod()
#else
#include <unistd.h>    // For write() and STDOUT_FILENO (signal-safe I/O)
#include <netdb.h>     // For getaddrinfo(), gai_strerror()
#include <arpa/inet.h> // For inet_ntop()
#endif

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>

#include "main.h"
#include "common.h"
#include "util/uthash.h"
#include "platform/abstraction.h"
#include "platform/socket.h"
#include "platform/init.h"
#include "image2ascii/image.h"
#include "image2ascii/simd/ascii_simd.h"
#include "image2ascii/simd/common.h"
#include "asciichat_errno.h"
#include "network/network.h"
#include "options.h"
#include "buffer_pool.h"
#include "audio/mixer.h"
#include "audio/audio.h"
#include "client.h"
#include "stream.h"
#include "stats.h"
#include "platform/string.h"
#include "platform/symbols.h"
#include "platform/system.h"
#include "crypto/keys/keys.h"

/* ============================================================================
 * Global State
 * ============================================================================
 */

/**
 * @brief Global atomic shutdown flag shared across all threads
 *
 * This flag is the primary coordination mechanism for clean server shutdown.
 * It's atomic to ensure thread-safe access without mutexes, as it's checked
 * frequently in tight loops across all worker threads.
 *
 * USAGE PATTERN:
 * - Set to true by signal handlers (SIGINT/SIGTERM) or main loop on error
 * - Checked by all worker threads to know when to exit gracefully
 * - Must be atomic to prevent race conditions during shutdown cascade
 */
atomic_bool g_server_should_exit = false;

/**
 * @brief Shutdown check callback for library code
 *
 * Provides clean separation between application state and library code.
 * Registered with shutdown_register_callback() so library code can check
 * shutdown status without directly accessing g_server_should_exit.
 */
static bool check_shutdown(void) {
  return atomic_load(&g_server_should_exit);
}

/**
 * @brief Global audio mixer instance for multi-client audio processing
 *
 * The mixer combines audio streams from multiple clients, excluding each client's
 * own audio from their outbound stream (preventing echo). Created once during
 * server initialization and shared by all audio render threads.
 *
 * THREAD SAFETY: The mixer itself is thread-safe and can be used concurrently
 * by multiple render.c audio threads without external synchronization.
 */
mixer_t *g_audio_mixer = NULL;

/**
 * @brief Global shutdown condition variable for waking blocked threads
 *
 * Used to wake up threads that might be blocked on condition variables
 * (like packet queues) during shutdown. This ensures responsive shutdown
 * even when threads are waiting on blocking operations.
 */
static_cond_t g_shutdown_cond = STATIC_COND_INIT;

/**
 * @brief Main listening socket for accepting client connections
 *
 * This socket is bound to the configured port and listens for incoming
 * client connections. Closed by signal handlers to interrupt accept()
 * calls during shutdown, ensuring the main loop exits promptly.
 *
 * PLATFORM NOTE: Uses platform-abstracted socket_t type (SOCKET on Windows,
 * int on POSIX) with INVALID_SOCKET_VALUE for proper cross-platform handling.
 */
/**
 * @brief IPv4 listening socket file descriptor
 *
 * Socket used to accept incoming IPv4 connections. Set to INVALID_SOCKET_VALUE
 * when not listening. Atomic to allow safe access from signal handlers.
 *
 * @ingroup server_main
 */
static _Atomic socket_t listenfd = INVALID_SOCKET_VALUE;

/**
 * @brief IPv6 listening socket file descriptor
 *
 * Socket used to accept incoming IPv6 connections. Set to INVALID_SOCKET_VALUE
 * when not listening. Atomic to allow safe access from signal handlers.
 *
 * @note May be INVALID_SOCKET_VALUE if IPv6 is disabled or unavailable
 * @ingroup server_main
 */
static _Atomic socket_t listenfd6 = INVALID_SOCKET_VALUE;

/**
 * @brief Global client manager for signal handler access
 *
 * Made global so signal handler can close client sockets immediately
 * during shutdown to interrupt blocking recv() calls.
 */
extern client_manager_t g_client_manager;
extern rwlock_t g_client_manager_rwlock;

/**
 * @brief Background thread handle for periodic statistics logging
 *
 * Runs stats_logger_thread_func() from stats.c to provide periodic reports
 * on server performance, client counts, buffer usage, and resource metrics.
 * Essential for monitoring server health in production deployments.
 */
static asciithread_t g_stats_logger_thread;

/**
 * @brief Flag tracking whether stats logger thread was successfully created
 *
 * Used to determine whether we need to wait for the stats thread during
 * shutdown cleanup. Prevents attempting to join a thread that was never
 * started due to initialization failures.
 */
static bool g_stats_logger_thread_created = false;

/* ============================================================================
 * Server Crypto State
 * ============================================================================
 */

/**
 * @brief Global flag indicating if server encryption is enabled
 *
 * Set to true when the server is configured to use encryption and has
 * successfully loaded a private key. Controls whether the server performs
 * cryptographic handshakes with clients.
 *
 * @note Accessed from crypto.c for server-side crypto operations
 * @ingroup server_main
 */
bool g_server_encryption_enabled = false;

/**
 * @brief Global server private key
 *
 * Stores the server's private key loaded from the key file. Used for
 * cryptographic handshakes and packet encryption/decryption. Initialized
 * during server startup from the configured key file path.
 *
 * @note Accessed from crypto.c for server-side crypto operations
 * @ingroup server_main
 */
private_key_t g_server_private_key = {0};

/**
 * @brief Global client public key whitelist
 *
 * Array of public keys for clients that are authorized to connect to the
 * server. Used for client authentication when whitelist mode is enabled.
 * Sized to hold up to MAX_CLIENTS entries.
 *
 * @note Only used when client authentication is enabled
 * @note Accessed from crypto.c for client authentication
 * @ingroup server_main
 */
public_key_t g_client_whitelist[MAX_CLIENTS] = {0};

/**
 * @brief Number of whitelisted clients
 *
 * Tracks the current number of entries in g_client_whitelist that are
 * valid and active. Used to iterate the whitelist and check authorization.
 *
 * @ingroup server_main
 */
size_t g_num_whitelisted_clients = 0;

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

/**
 * @brief Critical signal handler for SIGINT (Ctrl+C) - initiates server shutdown
 *
 * This handler is the primary entry point for graceful server shutdown. It's designed
 * to be signal-safe and perform minimal work to avoid deadlocks and undefined behavior
 * common in complex signal handlers.
 *
 * SIGNAL SAFETY STRATEGY:
 * =======================
 * Signal handlers are severely restricted in what they can safely do:
 * - Only async-signal-safe functions are allowed
 * - No mutex operations (can deadlock if main thread holds mutex)
 * - No malloc/SAFE_FREE(heap corruption if interrupted during allocation)
 * - No non-reentrant library calls (logging, printf, etc. are dangerous)
 *
 * SHUTDOWN PROCESS:
 * =================
 * 1. Set atomic g_server_should_exit flag (signal-safe, checked by all threads)
 * 2. Use raw write() for immediate user feedback (async-signal-safe)
 * 3. Broadcast shutdown condition to wake sleeping threads
 * 4. Close all sockets to interrupt blocking I/O operations
 * 5. Return quickly - let main thread handle complex cleanup
 *
 * SOCKET CLOSING RATIONALE:
 * =========================
 * Without socket closure, threads would remain blocked in:
 * - accept() in main loop (waiting for new connections)
 * - recv() in client receive threads (waiting for packets)
 * - send() in client send threads (if network is slow)
 *
 * Closing sockets causes these functions to return with error codes,
 * allowing threads to check g_server_should_exit and exit gracefully.
 *
 * PLATFORM CONSIDERATIONS:
 * ========================
 * - Windows: socket_shutdown() required to interrupt blocked recv()
 * - POSIX: socket_close() alone typically suffices
 * - Both: Avoid mutex operations (signal may interrupt mutex holder)
 *
 * @param sigint The signal number (unused, required by signal handler signature)
 */
static void sigint_handler(int sigint) {
  (void)(sigint);
  static _Atomic int sigint_count = 0;
  int count = atomic_fetch_add(&sigint_count, 1) + 1;
  if (count > 1) {
    _exit(1);
  }

  // STEP 1: Set atomic shutdown flag (checked by all worker threads)
  atomic_store(&g_server_should_exit, true);

  // STEP 2: Use write() for output (async-signal-safe)
  // NOTE: printf() and fflush() are NOT async-signal-safe and can cause deadlocks
  // Use write() which is guaranteed to be safe in signal handlers
  const char *msg = "SIGINT received - shutting down server...\n";
  ssize_t unused = write(STDOUT_FILENO, msg, strlen(msg));
  (void)unused; // Suppress unused variable warning

  // STEP 3: Close listening socket to interrupt accept() in main loop
  // This is signal-safe on Windows and necessary to wake up blocked accept()
  socket_t current_listenfd = atomic_load(&listenfd);
  if (current_listenfd != INVALID_SOCKET_VALUE) {
    socket_close(current_listenfd);
    atomic_store(&listenfd, INVALID_SOCKET_VALUE);
  }
  socket_t current_listenfd6 = atomic_load(&listenfd6);
  if (current_listenfd6 != INVALID_SOCKET_VALUE) {
    socket_close(current_listenfd6);
    atomic_store(&listenfd6, INVALID_SOCKET_VALUE);
  }

  // STEP 4: DO NOT access client data structures in signal handler
  // Signal handlers CANNOT safely use mutexes, rwlocks, or access complex data structures
  // This causes deadlocks and memory access violations because:
  // 1. Signal may interrupt a thread that already holds these locks
  // 2. Attempting to acquire locks in signal handler = instant deadlock
  // 3. Client array might be in an inconsistent state during modification
  //
  // SOLUTION: The listening socket closure above is sufficient to unblock accept_with_timeout()
  // The main thread will detect g_server_should_exit and properly close client sockets with timeouts

  // NOTE: Do NOT call log_destroy() here - it's not async-signal-safe
  // The main thread will handle cleanup when it detects g_server_should_exit
}

/**
 * @brief Helper function to create, bind, and listen on a socket
 * @param address Address to bind to (e.g., "127.0.0.1" or "::1")
 * @param family Address family (AF_INET or AF_INET6)
 * @param port Port number to bind to
 * @return Socket descriptor on success, INVALID_SOCKET_VALUE on failure (calls FATAL on error)
 *
 * This function handles the complete socket setup sequence:
 * 1. Resolve the address using getaddrinfo
 * 2. Create a socket with the specified family
 * 3. Set socket options (SO_REUSEADDR, IPV6_V6ONLY for IPv6, keep-alive)
 * 4. Bind to the address and port
 * 5. Listen for connections
 *
 * For IPv6 sockets, IPV6_V6ONLY is set to 1 to ensure separate binding from IPv4.
 */
static socket_t bind_and_listen(const char *address, int family, int port) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

  char port_str[16];
  SAFE_SNPRINTF(port_str, sizeof(port_str), "%d", port);

  log_debug("Resolving bind address '%s' port %s...", address, port_str);
  int getaddr_result = getaddrinfo(address, port_str, &hints, &res);
  if (getaddr_result != 0) {
    FATAL(ERROR_NETWORK, "Failed to resolve bind address '%s': %s", address, gai_strerror(getaddr_result));
  }

  struct sockaddr_storage serv_addr;
  if (res->ai_addrlen > sizeof(serv_addr)) {
    freeaddrinfo(res);
    FATAL(ERROR_NETWORK, "Address size too large: %zu bytes", (size_t)res->ai_addrlen);
  }
  memcpy(&serv_addr, res->ai_addr, res->ai_addrlen);
  int address_family = res->ai_family;
  socklen_t addrlen = res->ai_addrlen;
  freeaddrinfo(res);

  socket_t server_socket = socket_create(address_family, SOCK_STREAM, 0);
  if (server_socket == INVALID_SOCKET_VALUE) {
    FATAL(ERROR_NETWORK, "Failed to create socket: %s", SAFE_STRERROR(errno));
  }

  // Set socket options
  int yes = 1;
  if (socket_setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    FATAL(ERROR_NETWORK, "setsockopt SO_REUSEADDR failed: %s", SAFE_STRERROR(errno));
  }

  // For IPv6 sockets, set IPV6_V6ONLY=1 to bind separately from IPv4
  if (address_family == AF_INET6) {
    int ipv6only = 1; // 1 = IPv6 only (don't accept IPv4-mapped)
    if (socket_setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6only, sizeof(ipv6only)) == -1) {
      log_warn("Failed to set IPV6_V6ONLY=1: %s", SAFE_STRERROR(errno));
    } else {
      log_debug("IPv6-only mode enabled (separate from IPv4)");
    }
  }

  if (set_socket_keepalive(server_socket) < 0) {
    log_warn("Failed to set keep-alive on listener: %s", SAFE_STRERROR(errno));
  }

  log_info("Server binding to %s:%d", address, port);
  if (socket_bind(server_socket, (struct sockaddr *)&serv_addr, addrlen) < 0) {
    FATAL(ERROR_NETWORK_BIND, "Socket bind failed: %s", SAFE_STRERROR(errno));
  }

  if (socket_listen(server_socket, 10) < 0) {
    FATAL(ERROR_NETWORK, "Connection listen failed: %s", SAFE_STRERROR(errno));
  }

  return server_socket;
}

/**
 * @brief Handler for SIGTERM (termination request) signals
 *
 * SIGTERM is the standard "please terminate gracefully" signal sent by process
 * managers, systemd, Docker, etc. Unlike SIGINT (user Ctrl+C), SIGTERM indicates
 * a system-initiated shutdown request that should be honored promptly.
 *
 * IMPLEMENTATION STRATEGY:
 * This handler is intentionally more conservative than sigint_handler():
 * - Uses logging system (has built-in signal safety mechanisms)
 * - Does NOT close client sockets (avoids potential race conditions)
 * - Relies on main thread cleanup for client socket management
 * - Focuses on minimal flag setting and thread wakeup
 *
 * RATIONALE FOR CONSERVATIVE APPROACH:
 * SIGTERM often comes from automated systems that expect clean shutdown.
 * By being more careful and letting the main thread handle complex cleanup,
 * we reduce the risk of partial states or resource leaks that could affect
 * process monitoring systems.
 *
 * @param sigterm The signal number (unused, required by signal handler signature)
 */
static void sigterm_handler(int sigterm) {
  (void)(sigterm);
  atomic_store(&g_server_should_exit, true);

  printf("SIGTERM received - shutting down server...\n");
  (void)fflush(stdout);
  // Return immediately - signal handlers must be minimal
  // Main thread will detect g_server_should_exit and perform complete shutdown
}

/**
 * @brief Handler for SIGUSR1 - triggers lock debugging output
 *
 * This signal handler allows external triggering of lock debugging output
 * by sending SIGUSR1 to the server process. This is useful for debugging
 * deadlocks without modifying the running server.
 *
 * @param sigusr1 The signal number (unused, required by signal handler signature)
 */
static void sigusr1_handler(int sigusr1) {
  (void)(sigusr1);

#ifndef NDEBUG
  // Trigger lock debugging output (signal-safe)
  lock_debug_trigger_print();
#endif
}

/* ============================================================================
 * Main Function
 * ============================================================================
 */

/**
 * @brief ascii-chat Server main entry point - orchestrates the entire server architecture
 *
 * This function serves as the conductor of the ascii-chat server's modular architecture.
 * It replaces the original monolithic server design with a clean initialization sequence
 * followed by a robust multi-client connection management loop.
 *
 * ARCHITECTURAL OVERVIEW:
 * =======================
 * The main function coordinates several major subsystems:
 * 1. Platform initialization (Windows/POSIX compatibility)
 * 2. Logging and configuration setup
 * 3. Network socket creation and binding
 * 4. Global resource initialization (audio mixer, buffer pools, etc.)
 * 5. Background thread management (statistics logging)
 * 6. Main connection accept loop with client lifecycle management
 * 7. Graceful shutdown with proper resource cleanup
 *
 * MODULAR COMPONENT INTEGRATION:
 * ==============================
 * This main function ties together the modular components:
 * - client.c: add_client(), remove_client() for client lifecycle
 * - protocol.c: Not directly called (used by client receive threads)
 * - stream.c: Not directly called (used by render threads)
 * - render.c: create_client_render_threads() called via client.c
 * - stats.c: stats_logger_thread_func() runs in background
 *
 * CONCURRENCY STRATEGY:
 * =====================
 * Main thread responsibilities:
 * - Accept new connections (blocking with timeout)
 * - Manage client lifecycle (add/remove)
 * - Handle disconnection cleanup
 * - Coordinate graceful shutdown
 *
 * Background thread responsibilities:
 * - Per-client receive: Handle incoming packets (client.c)
 * - Per-client send: Manage outgoing packets (client.c)
 * - Per-client video render: Generate ASCII frames (render.c)
 * - Per-client audio render: Mix audio streams (render.c)
 * - Stats logger: Monitor server performance (stats.c)
 *
 * CLEANUP GUARANTEES:
 * ===================
 * The shutdown sequence ensures:
 * 1. Signal handlers set g_server_should_exit atomically
 * 2. All worker threads check flag and exit gracefully
 * 3. Main thread waits for all threads to finish
 * 4. Resources cleaned up in reverse dependency order
 * 5. No memory leaks or hanging processes
 *
 * @param argc Command line argument count
 * @param argv Command line argument vector
 * @return Exit code: 0 for success, non-zero for failure
 */

/**
 * @brief Initialize crypto for server
 * @return 0 on success, -1 on error
 * @ingroup server_main
 */
static int init_server_crypto(void) {
  // Check if encryption is disabled
  if (opt_no_encrypt) {
    log_info("Encryption: DISABLED (--no-encrypt)");
    g_server_encryption_enabled = false;
    return 0;
  }

  // Load server private key if provided via --key
  if (strlen(opt_encrypt_key) > 0) {
    // --key requires signing capabilities (SSH key files or GPG keys with gpg-agent)

    // Validate SSH key file (skip validation for special prefixes - they have their own validation)
    bool is_special_key = (strncmp(opt_encrypt_key, "gpg:", 4) == 0 || strncmp(opt_encrypt_key, "github:", 7) == 0 ||
                           strncmp(opt_encrypt_key, "gitlab:", 7) == 0);

    if (!is_special_key) {
      if (validate_ssh_key_file(opt_encrypt_key) != 0) {
        SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid SSH key file: %s", opt_encrypt_key);
        return -1;
      }
    }

    // Parse key (handles SSH files and gpg: prefix, rejects github:/gitlab:)
    log_info("Loading key for authentication: %s", opt_encrypt_key);
    if (parse_private_key(opt_encrypt_key, &g_server_private_key) == ASCIICHAT_OK) {
      log_info("Successfully loaded server key: %s", opt_encrypt_key);
    } else {
      log_error("Failed to parse key: %s\n"
                "This may be due to:\n"
                "  - Wrong password for encrypted key\n"
                "  - Unsupported key type (only Ed25519 is currently supported)\n"
                "  - Corrupted key file\n"
                "\n"
                "Note: RSA and ECDSA keys are not yet supported\n"
                "To generate an Ed25519 key: ssh-keygen -t ed25519\n",
                opt_encrypt_key);
      SET_ERRNO(ERROR_CRYPTO_KEY, "Key parsing failed: %s", opt_encrypt_key);
      return -1;
    }
  } else if (strlen(opt_password) == 0) {
    // No identity key provided - server will run in simple mode
    // The server will still generate ephemeral keys for encryption, but no identity key
    g_server_private_key.type = KEY_TYPE_UNKNOWN;
    log_info("Server running without identity key (simple mode)");
  }

  // Load client whitelist if provided
  if (strlen(opt_client_keys) > 0) {
    if (parse_public_keys(opt_client_keys, g_client_whitelist, &g_num_whitelisted_clients, MAX_CLIENTS) != 0) {
      SET_ERRNO(ERROR_CRYPTO_KEY, "Client key parsing failed: %s", opt_client_keys);
      return -1;
    }
    log_debug("Loaded %zu whitelisted clients", g_num_whitelisted_clients);
    log_info("Server will only accept %zu whitelisted clients", g_num_whitelisted_clients);
  }

  g_server_encryption_enabled = true;
  return 0;
}

int server_main(void) {
  // Common initialization (options, logging, lock debugging) now happens in main.c before dispatch
  // This function focuses on server-specific initialization

  // Register shutdown check callback for library code
  shutdown_register_callback(check_shutdown);

  // Initialize crypto after logging is ready
  log_info("Initializing crypto...");
  if (init_server_crypto() != 0) {
    // Print detailed error context if available
    LOG_ERRNO_IF_SET("Crypto initialization failed");
    FATAL(ERROR_CRYPTO, "Crypto initialization failed");
  }
  log_info("Crypto initialized successfully");

  // Handle quiet mode - disable terminal output when opt_quiet is enabled
  log_set_terminal_output(!opt_quiet);

  log_info("ASCII Chat server starting...");

  // log_info("SERVER: Options initialized, using log file: %s", log_filename);
  int port = strtoint_safe(opt_port);
  if (port == INT_MIN) {
    log_error("Invalid port configuration: %s", opt_port);
    FATAL(ERROR_CONFIG, "Invalid port configuration: %s", opt_port);
  }

  ascii_simd_init();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);

  // Simple signal handling (temporarily disable complex threading signal handling)
  log_debug("Setting up simple signal handlers...");

  // Handle Ctrl+C for cleanup
  platform_signal(SIGINT, sigint_handler);
  // Handle termination signal (SIGTERM is defined with limited support on Windows)
  platform_signal(SIGTERM, sigterm_handler);
  // Handle lock debugging trigger signal
#ifndef _WIN32
  platform_signal(SIGUSR1, sigusr1_handler);
#else
  UNUSED(sigusr1_handler);
#endif
#ifndef _WIN32
  // SIGPIPE not supported on Windows
  platform_signal(SIGPIPE, SIG_IGN);
#endif

#ifndef NDEBUG
  // Start the lock debug thread (system already initialized earlier)
  if (lock_debug_start_thread() != 0) {
    FATAL(ERROR_THREAD, "Failed to start lock debug thread");
  }
  // Initialize statistics system
  if (stats_init() != 0) {
    FATAL(ERROR_THREAD, "Statistics system initialization failed");
  }
#endif

  // Start statistics logging thread for periodic performance monitoring
  if (ascii_thread_create(&g_stats_logger_thread, stats_logger_thread, NULL) != 0) {
    LOG_ERRNO_IF_SET("Statistics logger thread creation failed");
  } else {
    g_stats_logger_thread_created = true;
    log_info("Statistics logger thread started");
  }

  // Network setup - Support dual-stack IPv4 and IPv6 binding
  // Logic:
  // - Default: bind to both 127.0.0.1 (IPv4) and ::1 (IPv6) for dual-stack on localhost
  // - If only opt_address is set: bind only to that IPv4 address
  // - If only opt_address6 is set: bind only to that IPv6 address
  // - If both are set: bind to both (dual-stack)

  bool bind_ipv4 = false;
  bool bind_ipv6 = false;
  const char *ipv4_address = NULL;
  const char *ipv6_address = NULL;

  // Determine which addresses to bind to
  // Check if addresses were explicitly set (non-empty and not default values)
  // Defaults set in options_init: opt_address="127.0.0.1", opt_address6="::1" (for server)
  // We need to detect if user explicitly provided --address or --address6
  // Since defaults are set before config/CLI parsing, we check if values differ from defaults

  log_debug("Config check: opt_address='%s', opt_address6='%s'", opt_address, opt_address6);

  bool ipv4_has_value = (strlen(opt_address) > 0);
  bool ipv6_has_value = (strlen(opt_address6) > 0);
  bool ipv4_is_default = (strcmp(opt_address, "127.0.0.1") == 0);
  bool ipv6_is_default = (strcmp(opt_address6, "::1") == 0);

  log_debug("Binding decision: ipv4_has_value=%d, ipv6_has_value=%d, ipv4_is_default=%d, ipv6_is_default=%d",
            ipv4_has_value, ipv6_has_value, ipv4_is_default, ipv6_is_default);

  // Logic:
  // - If both are defaults (or empty): bind to both defaults (dual-stack)
  // - If only IPv4 was explicitly set (non-default value): bind only IPv4
  // - If only IPv6 was explicitly set (non-default value): bind only IPv6
  // - If both were explicitly set: bind both (dual-stack)

  if (ipv4_has_value && ipv6_has_value && ipv4_is_default && ipv6_is_default) {
    // Both are defaults: dual-stack with default localhost addresses
    bind_ipv4 = true;
    bind_ipv6 = true;
    ipv4_address = "127.0.0.1";
    ipv6_address = "::1";
    log_info("Default dual-stack: binding to 127.0.0.1 (IPv4) and ::1 (IPv6)");
  } else if (ipv4_has_value && !ipv4_is_default && (!ipv6_has_value || ipv6_is_default)) {
    // IPv4 explicitly set, IPv6 is default or empty: bind only IPv4
    bind_ipv4 = true;
    bind_ipv6 = false;
    ipv4_address = opt_address;
    log_info("Binding only to IPv4 address: %s", ipv4_address);
  } else if (ipv6_has_value && !ipv6_is_default && (ipv4_is_default || !ipv4_has_value)) {
    // IPv6 explicitly set, IPv4 is default or empty: bind only IPv6
    bind_ipv4 = false;
    bind_ipv6 = true;
    ipv6_address = opt_address6;
    log_info("Binding only to IPv6 address: %s", ipv6_address);
  } else {
    // Both explicitly set or one explicit + one default: dual-stack
    bind_ipv4 = true;
    bind_ipv6 = true;
    ipv4_address = ipv4_has_value ? opt_address : "127.0.0.1";
    ipv6_address = ipv6_has_value ? opt_address6 : "::1";
    log_info("Dual-stack binding: IPv4=%s, IPv6=%s", ipv4_address, ipv6_address);
  }

  // Bind IPv4 socket if needed
  if (bind_ipv4) {
    socket_t new_listenfd = bind_and_listen(ipv4_address, AF_INET, port);
    atomic_store(&listenfd, new_listenfd);
  }

  // Bind IPv6 socket if needed
  if (bind_ipv6) {
    socket_t new_listenfd6 = bind_and_listen(ipv6_address, AF_INET6, port);
    atomic_store(&listenfd6, new_listenfd6);
  }

  struct sockaddr_storage client_addr;
  socklen_t client_len = sizeof(client_addr);

  struct timespec last_stats_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

  // Initialize synchronization primitives
  if (rwlock_init(&g_client_manager_rwlock) != 0) {
    FATAL(ERROR_THREAD, "Failed to initialize client manager rwlock");
  }

  // Lock debug system already initialized earlier in main()

  // Check if SIGINT was received during initialization
  if (atomic_load(&g_server_should_exit)) {
    // Skip rest of initialization and go straight to main loop
    // which will detect g_server_should_exit and exit cleanly
    goto main_loop;
  }

  // Lock debug thread already started earlier in main()

  // NOTE: g_client_manager is already zero-initialized in client.c with = {0}
  // We only need to initialize the mutex
  mutex_init(&g_client_manager.mutex);

  // Initialize uthash head pointer for O(1) lookup (uthash requires NULL initialization)
  g_client_manager.clients_by_id = NULL;

  // Initialize audio mixer (always enabled on server)
  if (!atomic_load(&g_server_should_exit)) {
    log_debug("Initializing audio mixer for per-client audio rendering...");
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      LOG_ERRNO_IF_SET("Failed to initialize audio mixer");
      if (!atomic_load(&g_server_should_exit)) {
        FATAL(ERROR_AUDIO, "Failed to initialize audio mixer");
      }
    } else {
      if (!atomic_load(&g_server_should_exit)) {
        log_debug("Audio mixer initialized successfully for per-client audio rendering");
      }
    }
  }

  // ========================================================================
  // MAIN CONNECTION LOOP - Heart of the modular server architecture
  // ========================================================================
  //
  // This loop orchestrates the entire multi-client server lifecycle:
  // 1. Clean up disconnected clients (free slots for new connections)
  // 2. Accept new client connections (with timeout to check shutdown)
  // 3. Initialize new clients (spawn threads via client.c functions)
  // 4. Repeat until shutdown signal received
  //
  // CRITICAL ORDERING: Client cleanup MUST happen before accept() to ensure
  // maximum connection slots are available. Otherwise, slots remain occupied
  // by dead connections, eventually preventing new clients from joining.

main_loop:
  while (!atomic_load(&g_server_should_exit)) {
    // Debug: Log loop iteration
    // Check if we received a shutdown signal
    if (atomic_load(&g_server_should_exit)) {
      break;
    }

    // Rate-limited logging: Only show status when client count actually changes
    // This prevents log spam while maintaining visibility into server state
    // THREAD SAFETY: Protect read of client_count with rwlock
    static int last_logged_count = -1;
    rwlock_rdlock(&g_client_manager_rwlock);
    int current_count = g_client_manager.client_count;
    rwlock_rdunlock(&g_client_manager_rwlock);
    if (current_count != last_logged_count) {
      log_debug("Waiting for client connections... (%d/%d clients)", current_count, MAX_CLIENTS);
      last_logged_count = current_count;
    }

    // ====================================================================
    // PHASE 1: DISCONNECT CLEANUP - Free slots from terminated clients
    // ====================================================================
    //
    // This implements a lock-safe cleanup pattern to avoid infinite loops:
    // 1. Collect cleanup tasks under read lock (minimal critical section)
    // 2. Release lock before processing (prevents lock contention)
    // 3. Process each cleanup task without locks (allows other threads to continue)
    //
    // This pattern prevents deadlocks while ensuring all disconnected
    // clients are properly cleaned up before accepting new ones.
    typedef struct {
      uint32_t client_id;
      asciithread_t receive_thread;
    } cleanup_task_t;

    cleanup_task_t cleanup_tasks[MAX_CLIENTS];
    int cleanup_count = 0;

    // FIXED: Only do cleanup if there are actually clients connected
    // THREAD SAFETY: Check client_count under the lock to avoid race
    rwlock_rdlock(&g_client_manager_rwlock);
    if (g_client_manager.client_count > 0) {
      for (int i = 0; i < MAX_CLIENTS; i++) {
        client_info_t *client = &g_client_manager.clients[i];
        // Check if this client has been marked inactive by its receive thread
        // Only check clients that have been initialized (client_id != 0)
        // FIXED: Only access mutex for initialized clients to avoid accessing uninitialized mutex
        if (atomic_load(&client->client_id) == 0) {
          continue; // Skip uninitialized clients
        }

        // DEADLOCK FIX: Use snapshot pattern to avoid holding both locks simultaneously
        // This prevents deadlock by not acquiring client_state_mutex while holding rwlock
        uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read is safe under rwlock
        bool is_active = atomic_load(&client->active);                 // Use atomic read to avoid deadlock

        if (!is_active && ascii_thread_is_initialized(&client->receive_thread)) {
          // Collect cleanup task
          cleanup_tasks[cleanup_count].client_id = client_id_snapshot;
          cleanup_tasks[cleanup_count].receive_thread = client->receive_thread;
          cleanup_count++;

          // Clear the thread handle immediately to avoid double-join
          // Use platform-safe thread initialization
          ascii_thread_init(&client->receive_thread);
        }
      }
    }
    rwlock_rdunlock(&g_client_manager_rwlock);

    // Process cleanup tasks without holding lock (prevents infinite loops)
    for (int i = 0; i < cleanup_count; i++) {
      bool is_shutting_down = atomic_load(&g_server_should_exit);
      if (is_shutting_down) {
        // During shutdown, give receive thread a brief chance to exit cleanly with timeout
        int join_result = ascii_thread_join_timeout(&cleanup_tasks[i].receive_thread, NULL, 200);
        if (join_result == -2) {
          log_warn("Receive thread for client %u timed out during shutdown (continuing)", cleanup_tasks[i].client_id);
          // Don't try to remove client if thread didn't exit cleanly
          continue;
        }
      } else {
        ascii_thread_join(&cleanup_tasks[i].receive_thread, NULL);
      }

      (void)remove_client(cleanup_tasks[i].client_id);
    }

    // Check if listening sockets were closed by signal handler
    socket_t current_listenfd = atomic_load(&listenfd);
    socket_t current_listenfd6 = atomic_load(&listenfd6);
    if (current_listenfd == INVALID_SOCKET_VALUE && current_listenfd6 == INVALID_SOCKET_VALUE) {
      break; // Both sockets closed, exit
    }

    // Accept network connection with timeout
    // Use select() to check both IPv4 and IPv6 sockets when both are bound

    // Check g_server_should_exit right before accept
    if (atomic_load(&g_server_should_exit)) {
      break;
    }

    // Build fd_set for select() if we have multiple sockets
    fd_set read_fds;
    socket_fd_zero(&read_fds);
    // NOTE: max_fd starts at 0 for select() calculation - this is correct
    // On Windows, select() ignores this parameter anyway
    socket_t max_fd = 0;

    if (current_listenfd != INVALID_SOCKET_VALUE) {
      socket_fd_set(current_listenfd, &read_fds);
      max_fd = (current_listenfd > max_fd) ? current_listenfd : max_fd;
    }
    if (current_listenfd6 != INVALID_SOCKET_VALUE) {
      socket_fd_set(current_listenfd6, &read_fds);
      max_fd = (current_listenfd6 > max_fd) ? current_listenfd6 : max_fd;
    }

    // Use select() with timeout to check both sockets
    struct timeval timeout;
    timeout.tv_sec = ACCEPT_TIMEOUT;
    timeout.tv_usec = 0;

    int select_result = socket_select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (select_result <= 0) {
      if (select_result == 0) {
        // Timeout - expected, continue loop
        continue;
      }
      // Error - check if socket was closed
      if (atomic_load(&listenfd) == INVALID_SOCKET_VALUE && atomic_load(&listenfd6) == INVALID_SOCKET_VALUE) {
        break;
      }
      continue;
    }

    // Determine which socket has a connection ready
    socket_t accept_listenfd = INVALID_SOCKET_VALUE;
    if (current_listenfd != INVALID_SOCKET_VALUE && socket_fd_isset(current_listenfd, &read_fds)) {
      accept_listenfd = current_listenfd;
    } else if (current_listenfd6 != INVALID_SOCKET_VALUE && socket_fd_isset(current_listenfd6, &read_fds)) {
      accept_listenfd = current_listenfd6;
    } else {
      // Neither socket ready (shouldn't happen if select() returned > 0)
      continue;
    }

    // CRITICAL: Final check right before accept to prevent race condition
    // The signal handler could close the socket between atomic_load() and accept()
    socket_t final_listenfd = INVALID_SOCKET_VALUE;
    if (accept_listenfd == current_listenfd) {
      final_listenfd = atomic_load(&listenfd);
    } else {
      final_listenfd = atomic_load(&listenfd6);
    }
    if (final_listenfd == INVALID_SOCKET_VALUE || final_listenfd != accept_listenfd) {
      log_debug("Main loop: listenfd was closed by signal handler, breaking accept loop");
      break;
    }

    ASSERT_NO_ERRNO();

    int client_sock = accept_with_timeout(accept_listenfd, (struct sockaddr *)&client_addr, &client_len, 0);

    // Get the error code from asciichat_errno_context (which accept_with_timeout sets)
    int saved_errno = asciichat_errno_context.system_errno;
    if (client_sock < 0) {
      if (saved_errno == ETIMEDOUT || saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
#ifdef DEBUG_NETWORK
        log_debug("Main loop: accept_with_timeout returned: client_sock=%d, errno=%d (%s)", client_sock, saved_errno,
                  client_sock < 0 ? socket_get_error_string() : "success");
#endif

#ifdef DEBUG_MEMORY
        // Hot loop memory reporting for bug detection and sanity.
        // debug_memory_report();
#endif
        // Clear asciichat_errno for timeouts (expected behavior)
        if (HAS_ERRNO(&asciichat_errno_context)) {
          if (asciichat_errno_context.system_errno == ETIMEDOUT) {
            asciichat_clear_errno();
          } else {
            PRINT_ERRNO_CONTEXT(&asciichat_errno_context);
          }
        }
        ASSERT_NO_ERRNO();

        continue;
      }
      if (saved_errno == EINTR) {
        // Interrupted by signal - check if we should exit
        log_debug("accept() interrupted by signal");
        if (atomic_load(&g_server_should_exit)) {
          break;
        }
        continue;
      }
      if (saved_errno == EBADF || saved_errno == ENOTSOCK
#ifdef _WIN32
          || saved_errno == WSAENOTSOCK || saved_errno == WSAEBADF || saved_errno == WSAEINVAL
#endif
      ) {
        // Socket was closed by signal handler
#ifdef _WIN32
        char error_buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, saved_errno, 0, error_buf, sizeof(error_buf), NULL);
        log_debug("accept() failed because socket was closed: %s", error_buf);
#else
        log_debug("accept() failed because socket was closed: %s", SAFE_STRERROR(saved_errno));
#endif
        break;
      }
      // This point in code isn't an actual error and a simple TCP timeout happened. Loop.
      continue;
    }

    // Log client connection - handle both IPv4 and IPv6
    char client_ip[INET6_ADDRSTRLEN]; // Large enough for both IPv4 and IPv6
    int client_port = 0;

    if (((struct sockaddr *)&client_addr)->sa_family == AF_INET) {
      // IPv4 address
      struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_addr;
      inet_ntop(AF_INET, &addr_in->sin_addr, client_ip, sizeof(client_ip));
      client_port = ntohs(addr_in->sin_port);
      log_debug("New client connected from %s:%d (IPv4)", client_ip, client_port);
    } else if (((struct sockaddr *)&client_addr)->sa_family == AF_INET6) {
      // IPv6 address
      struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&client_addr;
      inet_ntop(AF_INET6, &addr_in6->sin6_addr, client_ip, sizeof(client_ip));
      client_port = ntohs(addr_in6->sin6_port);

      // Check if it's an IPv4-mapped IPv6 address (::ffff:x.x.x.x)
      if (IN6_IS_ADDR_V4MAPPED(&addr_in6->sin6_addr)) {
        log_debug("New client connected from [%s]:%d (IPv4-mapped IPv6)", client_ip, client_port);
      } else {
        log_debug("New client connected from [%s]:%d (IPv6)", client_ip, client_port);
      }
    } else {
      safe_snprintf(client_ip, sizeof(client_ip), "unknown");
      log_warn("New client connected with unknown address family %d", ((struct sockaddr *)&client_addr)->sa_family);
    }

    // Add client to multi-client manager
    int client_id = add_client(client_sock, client_ip, client_port);
    if (client_id < 0) {
      log_error("Failed to add client, rejecting connection");
      // Print error context if available (helps debug crypto/network failures)
      if (HAS_ERRNO(&asciichat_errno_context)) {
        PRINT_ERRNO_CONTEXT(&asciichat_errno_context);
        // Clear the error so ASSERT_NO_ERRNO() doesn't abort on next iteration
        CLEAR_ERRNO();
      }
      socket_close(client_sock);
      continue;
    }

    log_debug("Client %d added successfully, total clients: %d", client_id, g_client_manager.client_count);

    // Check if we should exit after processing this client
    if (atomic_load(&g_server_should_exit)) {
      break;
    }

    if (HAS_ERRNO(&asciichat_errno_context)) {
      PRINT_ERRNO_CONTEXT(&asciichat_errno_context);
    }
    ASSERT_NO_ERRNO();
  }

  // Cleanup
  log_info("Server shutting down...");
  atomic_store(&g_server_should_exit, true);

  // Wake up any threads that might be blocked on condition variables
  // (like packet queues) to ensure responsive shutdown
  // This must happen BEFORE client cleanup to wake up any blocked threads
  static_cond_broadcast(&g_shutdown_cond);
  cond_destroy(&g_shutdown_cond.cond);

  // CRITICAL: Close all client sockets immediately to unblock receive threads
  // The signal handler only closed the listening socket, but client receive threads
  // are still blocked in recv_with_timeout(). We need to close their sockets to unblock them.
  log_info("Closing all client sockets to unblock receive threads...");

  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (atomic_load(&client->client_id) != 0 && client->socket != INVALID_SOCKET_VALUE) {
      socket_close(client->socket);
      client->socket = INVALID_SOCKET_VALUE;
    }
  }
  rwlock_rdunlock(&g_client_manager_rwlock);

  log_info("Signaling all clients to stop (sockets closed, g_server_should_exit set)...");

  // Wait for stats logger thread to finish
  if (g_stats_logger_thread_created) {
    ascii_thread_join(&g_stats_logger_thread, NULL);
    g_stats_logger_thread_created = false;
  }

  // Clean up all connected clients
  log_info("Cleaning up connected clients...");
  // FIXED: Simplified to collect client IDs first, then remove them without holding locks
  uint32_t clients_to_remove[MAX_CLIENTS];
  int client_count = 0;

  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    // Only attempt to clean up clients that were actually connected
    // (client_id is 0 for uninitialized clients, starts from 1 for connected clients)
    // FIXED: Only access mutex for initialized clients to avoid accessing uninitialized mutex
    if (atomic_load(&client->client_id) == 0) {
      continue; // Skip uninitialized clients
    }

    // DEADLOCK FIX: Use snapshot pattern to avoid holding both locks simultaneously
    // This prevents deadlock by not acquiring client_state_mutex while holding rwlock
    uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read is safe under rwlock

    // Clean up ANY client that was allocated, whether active or not
    // (disconnected clients may not be active but still have resources)
    clients_to_remove[client_count++] = client_id_snapshot;
  }
  rwlock_rdunlock(&g_client_manager_rwlock);

  // Remove all clients without holding any locks
  for (int i = 0; i < client_count; i++) {
    (void)remove_client(clients_to_remove[i]);
  }

  // Clean up hash table
  // Clean up uthash table (uthash handles deletion when the last item is removed,
  // but we should clear it here just in case there are stragglers)
  if (g_client_manager.clients_by_id) {
    client_info_t *current_client, *tmp;
    HASH_ITER(hh, g_client_manager.clients_by_id, current_client, tmp) {
      HASH_DELETE(hh, g_client_manager.clients_by_id, current_client);
      // Note: We don't free current_client here because it's part of the clients[] array
    }
    g_client_manager.clients_by_id = NULL;
  }

  // Clean up audio mixer
  if (g_audio_mixer) {
    mixer_destroy(g_audio_mixer);
    g_audio_mixer = NULL;
  }

  // Clean up synchronization primitives
  rwlock_destroy(&g_client_manager_rwlock);
  mutex_destroy(&g_client_manager.mutex);

#ifdef NDEBUG
  // Clean up statistics system
  stats_cleanup();
#endif

#ifndef NDEBUG
  // Clean up lock debugging system (always, regardless of build type)
  // Lock debug records are allocated in debug builds too, so they must be cleaned up
  lock_debug_cleanup();
#endif

  // Close listen sockets (both IPv4 and IPv6)
  socket_t current_listenfd = atomic_load(&listenfd);
  if (current_listenfd != INVALID_SOCKET_VALUE) {
    socket_close(current_listenfd);
  }
  socket_t current_listenfd6 = atomic_load(&listenfd6);
  if (current_listenfd6 != INVALID_SOCKET_VALUE) {
    socket_close(current_listenfd6);
  }

  // Clean up SIMD caches
  simd_caches_destroy_all();

  // Clean up symbol cache
  // This must be called BEFORE log_destroy() as symbol_cache_cleanup() uses log_debug()
  // Safe to call even if atexit() runs - it's idempotent (checks g_symbol_cache_initialized)
  // Also called via platform_cleanup() atexit handler, but explicit call ensures proper ordering
  symbol_cache_cleanup();

  // Clean up global buffer pool (explicitly, as atexit may not run on Ctrl-C)
  // Note: This is also registered with atexit(), but calling it explicitly is safe (idempotent)
  // Safe to call even if atexit() runs - it checks g_global_buffer_pool and sets it to NULL
  data_buffer_pool_cleanup_global();

  // Clean up binary path cache explicitly
  // Note: This is also called by platform_cleanup() via atexit(), but it's idempotent
  // (checks g_cache_initialized and sets it to false, sets g_bin_path_cache to NULL)
  // Safe to call even if atexit() runs later
  platform_cleanup_binary_path_cache();

  // Clean up platform-specific resources (Windows: Winsock cleanup, timer restoration)
  // POSIX: minimal cleanup (symbol cache already handled above on Windows)
#ifdef _WIN32
  socket_cleanup();
  timeEndPeriod(1); // Restore Windows timer resolution
#endif

#ifndef NDEBUG
  // Join the lock debug thread as one of the very last things before exit
  lock_debug_cleanup_thread();
#endif

  log_info("Server shutdown complete");

  asciichat_error_stats_print();

  log_destroy();

  // Use exit() to allow atexit() handlers to run
  // Cleanup functions are idempotent (check if initialized first)
  exit(0);
}
