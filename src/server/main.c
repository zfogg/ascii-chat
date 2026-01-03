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
#include "util/endian.h"
#include "util/ip.h"
#include "util/uthash.h"
#include "platform/abstraction.h"
#include "platform/socket.h"
#include "platform/init.h"
#include "platform/question.h"
#include "video/image.h"
#include "video/simd/ascii_simd.h"
#include "video/simd/common.h"
#include "asciichat_errno.h"
#include "network/network.h"
#include "network/tcp/server.h"
#include "network/acip/client.h"
#include "thread_pool.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "buffer_pool.h"
#include "audio/mixer.h"
#include "audio/audio.h"
#include "client.h"
#include "stream.h"
#include "stats.h"
#include "platform/string.h"
#include "platform/symbols.h"
#include "platform/system.h"
#include "crypto/keys.h"
#include "network/rate_limit/rate_limit.h"
#include "network/mdns/mdns.h"
#include "network/mdns/discovery.h"
#include "network/errors.h"
#include "network/nat/upnp.h"

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
 * @brief Global rate limiter for connection attempts and packet processing
 *
 * In-memory rate limiter to prevent connection flooding and DoS attacks.
 * Tracks connection attempts and packet rates per IP address with configurable limits.
 *
 * Default limits (from rate_limit.c):
 * - RATE_EVENT_CONNECTION: 50 connections per 60 seconds
 * - RATE_EVENT_IMAGE_FRAME: 144 FPS (8640 frames/min)
 * - RATE_EVENT_AUDIO: 172 FPS (10320 packets/min)
 * - RATE_EVENT_PING: 2 Hz (120 pings/min)
 * - RATE_EVENT_CLIENT_JOIN: 10 joins per 60 seconds
 * - RATE_EVENT_CONTROL: 100 control packets per 60 seconds
 *
 * THREAD SAFETY: The rate limiter is thread-safe and can be used concurrently
 * from the main accept loop and packet handlers without external synchronization.
 *
 * @see main.h for extern declaration
 */
rate_limiter_t *g_rate_limiter = NULL;

/**
 * @brief TCP server instance for accepting client connections
 *
 * Uses lib/network/tcp_server abstraction for dual-stack IPv4/IPv6 support.
 * Handles socket creation, binding, listening, and provides thread-safe
 * client registry for managing connected clients.
 *
 * PLATFORM NOTE: tcp_server_t handles platform-abstracted socket_t types
 * internally for proper cross-platform Windows/POSIX support.
 *
 * @ingroup server_main
 */
static tcp_server_t g_tcp_server;

/**
 * @brief Global UPnP context for port mapping on home routers
 *
 * Stores the active UPnP/NAT-PMP port mapping state. Enables direct TCP
 * connectivity for ~70% of home users without requiring WebRTC.
 * Set to NULL if UPnP is disabled, unavailable, or failed to map.
 *
 * @ingroup server_main
 */
static nat_upnp_context_t *g_upnp_ctx = NULL;

/**
 * @brief Global mDNS context for LAN service discovery
 *
 * Used to advertise the server on the local network via mDNS (Multicast DNS).
 * Set to NULL if mDNS is disabled or fails to initialize.
 * Advertises service as "_ascii-chat._tcp.local"
 *
 * @ingroup server_main
 */
static asciichat_mdns_t *g_mdns_ctx = NULL;

/**
 * @brief Global client manager for signal handler access
 *
 * Made global so signal handler can close client sockets immediately
 * during shutdown to interrupt blocking recv() calls.
 */
extern client_manager_t g_client_manager;
extern rwlock_t g_client_manager_rwlock;

/**
 * @brief Background worker thread pool for server operations
 *
 * Manages background threads like stats logger, lock debugging, etc.
 * Threads in this pool are independent of client connections.
 */
static thread_pool_t *g_server_worker_pool = NULL;

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
  // printf() and fflush() are NOT async-signal-safe and can cause deadlocks
  // Use write() which is guaranteed to be safe in signal handlers
  const char *msg = "SIGINT received - shutting down server...\n";
  ssize_t unused = write(STDOUT_FILENO, msg, strlen(msg));
  (void)unused; // Suppress unused variable warning

  // STEP 3: Signal TCP server to stop and close listening sockets
  // This interrupts the accept() call in the main loop
  atomic_store(&g_tcp_server.running, false);
  if (g_tcp_server.listen_socket != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket);
  }
  if (g_tcp_server.listen_socket6 != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket6);
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

// bind_and_listen() function removed - now using lib/network/tcp_server abstraction

/**
 * @brief Handler for SIGTERM (termination request) signals
 *
 * SIGTERM is the standard "please terminate gracefully" signal sent by process
 * managers, systemd, Docker, etc. Unlike SIGINT (user Ctrl+C), SIGTERM indicates
 * a system-initiated shutdown request that should be honored promptly.
 *
 * IMPLEMENTATION STRATEGY:
 * This handler must aggressively interrupt the accept loop, just like SIGINT,
 * to ensure responsive shutdown when triggered by automated systems like docker stop.
 * Process managers and Docker expect clean shutdown within a timeout window.
 *
 * SIGNAL SAFETY:
 * - Sets atomic flags (signal-safe)
 * - Closes listening sockets to interrupt accept() (signal-safe)
 * - Does NOT access complex data structures (avoids deadlocks)
 *
 * @param sigterm The signal number (unused, required by signal handler signature)
 */
static void sigterm_handler(int sigterm) {
  (void)(sigterm);
  atomic_store(&g_server_should_exit, true);

  // Use async-signal-safe write() instead of printf()/fflush()
  // printf() and fflush() are NOT async-signal-safe and can cause deadlocks
  const char *msg = "SIGTERM received - shutting down server...\n";
  ssize_t unused = write(STDOUT_FILENO, msg, strlen(msg));
  (void)unused; // Suppress unused variable warning

  // CRITICAL: Stop the TCP server accept loop immediately
  // Without this, the select() call with ACCEPT_TIMEOUT could delay shutdown
  atomic_store(&g_tcp_server.running, false);
  if (g_tcp_server.listen_socket != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket);
  }
  if (g_tcp_server.listen_socket6 != INVALID_SOCKET_VALUE) {
    socket_close(g_tcp_server.listen_socket6);
  }
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
 * Client Handler Thread (for tcp_server integration)
 * ============================================================================
 */

/**
 * @brief Client handler thread function for tcp_server integration
 *
 * Called by tcp_server_run() for each accepted connection. This function:
 * 1. Extracts client connection info from tcp_client_context_t
 * 2. Performs connection rate limiting
 * 3. Calls add_client() to initialize client structure and spawn workers
 * 4. Blocks until client disconnects
 * 5. Calls remove_client() to cleanup
 *
 * @param arg Pointer to tcp_client_context_t allocated by tcp_server_run()
 * @return NULL (thread exit value)
 */
static void *ascii_chat_client_handler(void *arg) {
  tcp_client_context_t *ctx = (tcp_client_context_t *)arg;
  if (!ctx) {
    log_error("Client handler: NULL context");
    return NULL;
  }

  // Extract server context from user_data
  server_context_t *server_ctx = (server_context_t *)ctx->user_data;
  if (!server_ctx) {
    log_error("Client handler: NULL server context");
    socket_close(ctx->client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  socket_t client_socket = ctx->client_socket;

  // Extract client IP and port using tcp_server helpers
  char client_ip[INET6_ADDRSTRLEN] = {0};
  if (!tcp_client_context_get_ip(ctx, client_ip, sizeof(client_ip))) {
    safe_snprintf(client_ip, sizeof(client_ip), "unknown");
  }

  int client_port = tcp_client_context_get_port(ctx);
  if (client_port < 0) {
    client_port = 0;
  }

  log_info("Client handler started for %s:%d", client_ip, client_port);

  // Check connection rate limit (prevent DoS attacks)
  if (server_ctx->rate_limiter) {
    bool allowed = false;
    asciichat_error_t rate_check =
        rate_limiter_check(server_ctx->rate_limiter, client_ip, RATE_EVENT_CONNECTION, NULL, &allowed);
    if (rate_check != ASCIICHAT_OK || !allowed) {
      tcp_server_reject_client(client_socket, "Connection rate limit exceeded");
      SAFE_FREE(ctx);
      return NULL;
    }
    // Record successful connection attempt
    rate_limiter_record(server_ctx->rate_limiter, client_ip, RATE_EVENT_CONNECTION);
  }

  // Add client (initializes structures, spawns workers via tcp_server_spawn_thread)
  int client_id = add_client(server_ctx, client_socket, client_ip, client_port);
  if (client_id < 0) {
    if (HAS_ERRNO(&asciichat_errno_context)) {
      PRINT_ERRNO_CONTEXT(&asciichat_errno_context);
      CLEAR_ERRNO();
    }
    tcp_server_reject_client(client_socket, "Failed to add client");
    SAFE_FREE(ctx);
    return NULL;
  }

  log_debug("Client %d added successfully from %s:%d", client_id, client_ip, client_port);

  // Block until client disconnects (active flag is set by receive thread)
  client_info_t *client = find_client_by_id((uint32_t)client_id);
  if (client) {
    while (atomic_load(&client->active) && !atomic_load(server_ctx->server_should_exit)) {
      platform_sleep_ms(100); // Check every 100ms
    }
    log_info("Client %d disconnected from %s:%d", client_id, client_ip, client_port);
  }

  // Cleanup (this will call tcp_server_stop_client_threads internally)
  remove_client(server_ctx, (uint32_t)client_id);

  // Close socket and free context
  socket_close(client_socket);
  SAFE_FREE(ctx);

  log_info("Client handler finished for %s:%d", client_ip, client_port);
  return NULL;
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
  if (GET_OPTION(no_encrypt)) {
    log_info("Encryption: DISABLED (--no-encrypt)");
    g_server_encryption_enabled = false;
    return 0;
  }

  // Load server private key if provided via --key
  if (strlen(GET_OPTION(encrypt_key)) > 0) {
    // --key requires signing capabilities (SSH key files or GPG keys with gpg-agent)

    // Validate SSH key file (skip validation for special prefixes - they have their own validation)
    bool is_special_key =
        (strncmp(GET_OPTION(encrypt_key), "gpg:", 4) == 0 || strncmp(GET_OPTION(encrypt_key), "github:", 7) == 0 ||
         strncmp(GET_OPTION(encrypt_key), "gitlab:", 7) == 0);

    if (!is_special_key) {
      if (validate_ssh_key_file(GET_OPTION(encrypt_key)) != 0) {
        SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid SSH key file: %s", GET_OPTION(encrypt_key));
        return -1;
      }
    }

    // Parse key (handles SSH files and gpg: prefix, rejects github:/gitlab:)
    log_info("Loading key for authentication: %s", GET_OPTION(encrypt_key));
    if (parse_private_key(GET_OPTION(encrypt_key), &g_server_private_key) == ASCIICHAT_OK) {
      log_info("Successfully loaded server key: %s", GET_OPTION(encrypt_key));
    } else {
      log_error("Failed to parse key: %s\n"
                "This may be due to:\n"
                "  - Wrong password for encrypted key\n"
                "  - Unsupported key type (only Ed25519 is currently supported)\n"
                "  - Corrupted key file\n"
                "\n"
                "Note: RSA and ECDSA keys are not yet supported\n"
                "To generate an Ed25519 key: ssh-keygen -t ed25519\n",
                GET_OPTION(encrypt_key));
      SET_ERRNO(ERROR_CRYPTO_KEY, "Key parsing failed: %s", GET_OPTION(encrypt_key));
      return -1;
    }
  } else if (strlen(GET_OPTION(password)) == 0) {
    // No identity key provided - server will run in simple mode
    // The server will still generate ephemeral keys for encryption, but no identity key
    g_server_private_key.type = KEY_TYPE_UNKNOWN;
    log_info("Server running without identity key (simple mode)");
  }

  // Load client whitelist if provided
  if (strlen(GET_OPTION(client_keys)) > 0) {
    if (parse_public_keys(GET_OPTION(client_keys), g_client_whitelist, &g_num_whitelisted_clients, MAX_CLIENTS) != 0) {
      SET_ERRNO(ERROR_CRYPTO_KEY, "Client key parsing failed: %s", GET_OPTION(client_keys));
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

  // Handle quiet mode - disable terminal output when GET_OPTION(quiet) is enabled
  log_set_terminal_output(!GET_OPTION(quiet));

  log_info("ASCII Chat server starting...");

  // log_info("SERVER: Options initialized, using log file: %s", log_filename);
  int port = strtoint_safe(GET_OPTION(port));
  if (port == INT_MIN) {
    log_error("Invalid port configuration: %s", GET_OPTION(port));
    FATAL(ERROR_CONFIG, "Invalid port configuration: %s", GET_OPTION(port));
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

  // Create background worker thread pool for server operations
  g_server_worker_pool = thread_pool_create("server_workers");
  if (!g_server_worker_pool) {
    LOG_ERRNO_IF_SET("Failed to create server worker thread pool");
    FATAL(ERROR_MEMORY, "Failed to create server worker thread pool");
  }

  // Spawn statistics logging thread in worker pool
  if (thread_pool_spawn(g_server_worker_pool, stats_logger_thread, NULL, 0, "stats_logger") != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("Statistics logger thread creation failed");
  } else {
    log_info("Statistics logger thread started");
  }

  // Network setup - Use tcp_server abstraction for dual-stack IPv4/IPv6 binding
  log_debug("Config check: GET_OPTION(address)='%s', GET_OPTION(address6)='%s'", GET_OPTION(address),
            GET_OPTION(address6));

  bool ipv4_has_value = (strlen(GET_OPTION(address)) > 0);
  bool ipv6_has_value = (strlen(GET_OPTION(address6)) > 0);
  bool ipv4_is_default = (strcmp(GET_OPTION(address), "127.0.0.1") == 0);
  bool ipv6_is_default = (strcmp(GET_OPTION(address6), "::1") == 0);

  log_debug("Binding decision: ipv4_has_value=%d, ipv6_has_value=%d, ipv4_is_default=%d, ipv6_is_default=%d",
            ipv4_has_value, ipv6_has_value, ipv4_is_default, ipv6_is_default);

  // Determine bind configuration
  bool bind_ipv4 = false;
  bool bind_ipv6 = false;
  const char *ipv4_address = NULL;
  const char *ipv6_address = NULL;

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
    ipv4_address = GET_OPTION(address);
    log_info("Binding only to IPv4 address: %s", ipv4_address);
  } else if (ipv6_has_value && !ipv6_is_default && (ipv4_is_default || !ipv4_has_value)) {
    // IPv6 explicitly set, IPv4 is default or empty: bind only IPv6
    bind_ipv4 = false;
    bind_ipv6 = true;
    ipv6_address = GET_OPTION(address6);
    log_info("Binding only to IPv6 address: %s", ipv6_address);
  } else {
    // Both explicitly set or one explicit + one default: dual-stack
    bind_ipv4 = true;
    bind_ipv6 = true;
    ipv4_address = ipv4_has_value ? GET_OPTION(address) : "127.0.0.1";
    ipv6_address = ipv6_has_value ? GET_OPTION(address6) : "::1";
    log_info("Dual-stack binding: IPv4=%s, IPv6=%s", ipv4_address, ipv6_address);
  }

  // Create server context - encapsulates all server state for passing to client handlers
  // This reduces global state and improves modularity by using tcp_server.user_data
  server_context_t server_ctx = {
      .tcp_server = &g_tcp_server,
      .rate_limiter = g_rate_limiter,
      .client_manager = &g_client_manager,
      .client_manager_rwlock = &g_client_manager_rwlock,
      .server_should_exit = &g_server_should_exit,
      .audio_mixer = g_audio_mixer,
      .stats = &g_stats,
      .stats_mutex = &g_stats_mutex,
      .encryption_enabled = g_server_encryption_enabled,
      .server_private_key = &g_server_private_key,
      .client_whitelist = g_client_whitelist,
      .num_whitelisted_clients = g_num_whitelisted_clients,
  };

  // Configure TCP server
  tcp_server_config_t tcp_config = {
      .port = port,
      .ipv4_address = ipv4_address,
      .ipv6_address = ipv6_address,
      .bind_ipv4 = bind_ipv4,
      .bind_ipv6 = bind_ipv6,
      .accept_timeout_sec = ACCEPT_TIMEOUT,
      .client_handler = ascii_chat_client_handler,
      .user_data = &server_ctx, // Pass server context to client handlers
  };

  // Initialize TCP server (creates and binds sockets)
  memset(&g_tcp_server, 0, sizeof(g_tcp_server));
  asciichat_error_t tcp_init_result = tcp_server_init(&g_tcp_server, &tcp_config);
  if (tcp_init_result != ASCIICHAT_OK) {
    FATAL(ERROR_NETWORK, "Failed to initialize TCP server");
  }

  // =========================================================================
  // UPnP Port Mapping (Quick Win for Direct TCP)
  // =========================================================================
  // Try to open port via UPnP so direct TCP works for ~70% of home users.
  // If this fails, clients fall back to WebRTC automatically - not fatal.
  //
  // Strategy:
  //   1. UPnP (works on ~90% of home routers)
  //   2. NAT-PMP fallback (Apple routers)
  //   3. If both fail: use ACDS + WebRTC (reliable, but slightly higher latency)
  if (GET_OPTION(enable_upnp) && !GET_OPTION(no_upnp)) {
    asciichat_error_t upnp_result = nat_upnp_open(port, "ASCII-Chat Server", &g_upnp_ctx);

    if (upnp_result == ASCIICHAT_OK && g_upnp_ctx) {
      char public_addr[22];
      if (nat_upnp_get_address(g_upnp_ctx, public_addr, sizeof(public_addr)) == ASCIICHAT_OK) {
        printf("🌐 Public endpoint: %s (direct TCP)\\n", public_addr);
        log_info("UPnP: Port mapping successful, public endpoint: %s", public_addr);
      }
    } else {
      log_info("UPnP: Port mapping unavailable or failed - will use WebRTC fallback");
      printf("📡 Clients behind strict NATs will use WebRTC fallback\\n");
    }
  } else {
    if (GET_OPTION(no_upnp)) {
      log_info("UPnP: Disabled via --no-upnp option");
    } else {
      log_info("UPnP: Disabled via environment variable or configuration");
    }
    printf("📡 WebRTC will be used for all clients\\n");
  }

  struct timespec last_stats_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

  // Initialize synchronization primitives
  if (rwlock_init(&g_client_manager_rwlock) != 0) {
    FATAL(ERROR_THREAD, "Failed to initialize client manager rwlock");
  }

  // Lock debug system already initialized earlier in main()

  // Check if SIGINT was received during initialization
  // If so, tcp_server_run() will detect it and exit immediately
  if (atomic_load(&g_server_should_exit)) {
    log_info("Shutdown signal received during initialization, skipping server startup");
  }

  // Lock debug thread already started earlier in main()

  // NOTE: g_client_manager is already zero-initialized in client.c with = {0}
  // We only need to initialize the mutex
  mutex_init(&g_client_manager.mutex);

  // Initialize uthash head pointer for O(1) lookup (uthash requires NULL initialization)
  g_client_manager.clients_by_id = NULL;

  // Initialize connection rate limiter (prevents DoS attacks)
  if (!atomic_load(&g_server_should_exit)) {
    log_debug("Initializing connection rate limiter...");
    g_rate_limiter = rate_limiter_create_memory();
    if (!g_rate_limiter) {
      LOG_ERRNO_IF_SET("Failed to initialize rate limiter");
      if (!atomic_load(&g_server_should_exit)) {
        FATAL(ERROR_MEMORY, "Failed to create connection rate limiter");
      }
    } else {
      log_info("Connection rate limiter initialized (50 connections/min per IP)");
    }
  }

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

  // Initialize mDNS for LAN service discovery (optional)
  // mDNS allows clients on the LAN to discover this server without knowing its IP
  // Can be disabled with --no-mdns-advertise
  if (!atomic_load(&g_server_should_exit) && !GET_OPTION(no_mdns_advertise)) {
    log_debug("Initializing mDNS for LAN service discovery...");
    g_mdns_ctx = asciichat_mdns_init();
    if (!g_mdns_ctx) {
      LOG_ERRNO_IF_SET("Failed to initialize mDNS (non-fatal, LAN discovery disabled)");
      log_warn("mDNS disabled - LAN service discovery will not be available");
      g_mdns_ctx = NULL;
    } else {
      // Advertise service on the LAN
      // Build session name from hostname if available
      char session_name[256] = "ASCII-Chat-Server";
      char hostname[256] = {0};
      if (gethostname(hostname, sizeof(hostname) - 1) == 0 && strlen(hostname) > 0) {
        snprintf(session_name, sizeof(session_name), "%s", hostname);
      }

      // Prepare TXT records with session string and host public key
      char txt_session_string[512];
      char txt_host_pubkey[512];
      const char *txt_records[2];
      int txt_count = 0;

      // Add session string to TXT records (for client discovery)
      // Use hostname-based session string for Phase 1
      snprintf(txt_session_string, sizeof(txt_session_string), "session_string=%s", session_name);
      txt_records[txt_count++] = txt_session_string;

      // Add host public key to TXT records (for cryptographic verification)
      // Convert server's Ed25519 public key to hex format
      if (g_server_encryption_enabled) {
        char hex_pubkey[65];
        pubkey_to_hex(g_server_private_key.public_key, hex_pubkey);
        snprintf(txt_host_pubkey, sizeof(txt_host_pubkey), "host_pubkey=%s", hex_pubkey);
        txt_records[txt_count++] = txt_host_pubkey;
        log_debug("mDNS: Host pubkey=%s", hex_pubkey);
      } else {
        // If encryption is disabled, still advertise a zero pubkey for clients to detect
        snprintf(txt_host_pubkey, sizeof(txt_host_pubkey), "host_pubkey=");
        for (int i = 0; i < 32; i++) {
          snprintf(txt_host_pubkey + strlen(txt_host_pubkey), sizeof(txt_host_pubkey) - strlen(txt_host_pubkey), "00");
        }
        txt_records[txt_count++] = txt_host_pubkey;
        log_debug("mDNS: Encryption disabled, advertising zero pubkey");
      }

      asciichat_mdns_service_t service = {
          .name = session_name,
          .type = "_ascii-chat._tcp",
          .host = hostname,
          .port = (uint16_t)port,
          .txt_records = txt_records,
          .txt_count = txt_count,
      };

      asciichat_error_t mdns_advertise_result = asciichat_mdns_advertise(g_mdns_ctx, &service);
      if (mdns_advertise_result != ASCIICHAT_OK) {
        LOG_ERRNO_IF_SET("Failed to advertise mDNS service");
        log_warn("mDNS advertising failed - LAN discovery disabled");
        asciichat_mdns_shutdown(g_mdns_ctx);
        g_mdns_ctx = NULL;
      } else {
        printf("🌐 mDNS: Server advertised as '%s.local' on LAN\n", session_name);
        log_info("mDNS: Service advertised as '%s.local' (name=%s, port=%d, txt_count=%d)", service.type, service.name,
                 service.port, service.txt_count);
      }
    }
  } else if (GET_OPTION(no_mdns_advertise)) {
    log_info("mDNS service advertisement disabled via --no-mdns-advertise");
  }

  // ========================================================================
  // MAIN CONNECTION LOOP - Delegated to tcp_server
  // ========================================================================
  //
  // The tcp_server module handles:
  // 1. Dual-stack IPv4/IPv6 accept loop with select() timeout
  // 2. Spawning client_handler threads for each connection
  // 3. Responsive shutdown when g_tcp_server.running is set to false
  //
  // Client lifecycle is managed by ascii_chat_client_handler() which:
  // - Performs rate limiting
  // - Calls add_client() to initialize structures and spawn workers
  // - Blocks until client disconnects
  // - Calls remove_client() to cleanup and stop worker threads

  // ACDS Session Creation: Register this server with discovery service
  // Security Requirement Check (Issue #239):
  // Server IP must be protected by password, identity verification, or explicit opt-in
  char session_string[64] = {0};

  // Auto-detection: Check if password or identity verification is configured
  const char *password = GET_OPTION(password);
  bool has_password = password && strlen(password) > 0;
  const char *encrypt_key = GET_OPTION(encrypt_key);
  bool has_identity = encrypt_key && strlen(encrypt_key) > 0;
  bool explicit_expose = GET_OPTION(acds_expose_ip) != 0;

  // Validate security configuration BEFORE attempting ACDS connection
  bool acds_expose_ip_flag = false;

  if (has_password || has_identity) {
    // Auto-enable privacy: IP revealed only after verification
    acds_expose_ip_flag = false;
    log_plain("🔒 ACDS privacy enabled: IP disclosed only after %s verification",
              has_password ? "password" : "identity");
  } else if (explicit_expose) {
    // Explicit opt-in to public IP disclosure - requires confirmation
    log_plain_stderr("");
    log_plain_stderr("⚠️  WARNING: You are about to allow PUBLIC IP disclosure!");
    log_plain_stderr("⚠️  Anyone with the session string will be able to see your IP address.");
    log_plain_stderr("⚠️  This is NOT RECOMMENDED unless you understand the privacy implications.");
    log_plain_stderr("");

    if (!platform_prompt_yes_no("Do you want to proceed with public IP disclosure", false)) {
      log_plain_stderr("");
      log_plain_stderr("❌ IP disclosure not confirmed. Server will run WITHOUT discovery service.");
      goto skip_acds_session;
    }

    // User confirmed - proceed with public IP disclosure
    acds_expose_ip_flag = true;
    log_plain_stderr("");
    log_plain_stderr("⚠️  Public IP disclosure CONFIRMED");
    log_plain_stderr("⚠️  Your IP address will be visible to anyone with the session string");
  } else {
    // Security violation: No password, no identity, no explicit opt-in
    log_plain_stderr("❌ Cannot create ACDS session: No security configured!");
    log_plain_stderr("   You must either:");
    log_plain_stderr("   1. Set a password: --password \"your-secret\"");
    log_plain_stderr("   2. Use identity key: --key ~/.ssh/id_ed25519");
    log_plain_stderr("   3. Explicitly allow public IP: --acds-expose-ip (NOT RECOMMENDED)");
    log_plain_stderr("");
    log_plain_stderr("Server will run WITHOUT discovery service.");
    goto skip_acds_session;
  }

  // Security is configured, proceed with ACDS connection
  const char *acds_server = GET_OPTION(acds_server);
  uint16_t acds_port = (uint16_t)GET_OPTION(acds_port);

  log_info("Attempting to create session on ACDS server at %s:%d...", acds_server, acds_port);

  acds_client_config_t acds_config;
  acds_client_config_init_defaults(&acds_config);
  SAFE_STRNCPY(acds_config.server_address, acds_server, sizeof(acds_config.server_address));
  acds_config.server_port = acds_port;
  acds_config.timeout_ms = 5000;

  acds_client_t acds_client;
  asciichat_error_t acds_connect_result = acds_client_connect(&acds_client, &acds_config);
  if (acds_connect_result == ASCIICHAT_OK) {
    // Prepare session creation parameters
    acds_session_create_params_t create_params;
    memset(&create_params, 0, sizeof(create_params));

    // Use server's Ed25519 identity public key if available
    if (g_server_encryption_enabled && has_identity) {
      memcpy(create_params.identity_pubkey, g_server_private_key.public_key, 32);
      log_debug("Using server identity key for ACDS session");
    } else {
      // No identity key available - use zero key
      // ACDS will accept this if identity verification is not required
      memset(create_params.identity_pubkey, 0, 32);
      log_debug("No server identity key - using zero key for ACDS session");
    }

    create_params.capabilities = 0x03; // Video + Audio
    create_params.max_participants = GET_OPTION(max_clients);

    // Set password if configured
    create_params.has_password = has_password;
    if (has_password) {
      // TODO: Hash password with Argon2id
      SAFE_STRNCPY(create_params.password, password, sizeof(create_params.password));
    }

    // Set IP disclosure policy (determined above)
    create_params.acds_expose_ip = acds_expose_ip_flag;

    // Set session type (Direct TCP or WebRTC)
    create_params.session_type = GET_OPTION(webrtc) ? SESSION_TYPE_WEBRTC : SESSION_TYPE_DIRECT_TCP;

    // Server connection information (where clients should connect)
    SAFE_STRNCPY(create_params.server_address, GET_OPTION(address), sizeof(create_params.server_address));
    create_params.server_port = port;

    // Create session
    acds_session_create_result_t create_result;
    asciichat_error_t create_err = acds_session_create(&acds_client, &create_params, &create_result);
    acds_client_disconnect(&acds_client);

    if (create_err == ASCIICHAT_OK) {
      SAFE_STRNCPY(session_string, create_result.session_string, sizeof(session_string));
      log_plain("✨ Session created successfully!");
      log_plain("📋 Session string: %s", session_string);
      log_plain("🔗 Share this with others to join:");
      log_plain("   ascii-chat %s", session_string);
    } else {
      log_warn("Failed to create session on ACDS server (server will run without discovery)");
    }
  } else {
    log_warn("Could not connect to ACDS server at %s:%d (server will run without discovery)", acds_server, acds_port);
  }

skip_acds_session:
  log_info("Server entering accept loop (port %d)...", port);

  // Run TCP server (blocks until shutdown signal received)
  // tcp_server_run() handles:
  // - select() on IPv4/IPv6 sockets with timeout
  // - accept() new connections
  // - Spawn ascii_chat_client_handler() thread for each connection
  // - Responsive shutdown when atomic_store(&g_tcp_server.running, false)
  asciichat_error_t run_result = tcp_server_run(&g_tcp_server);
  if (run_result != ASCIICHAT_OK) {
    log_error("TCP server exited with error");
  }

  log_info("Server accept loop exited");

  // Cleanup
  log_info("Server shutting down...");
  atomic_store(&g_server_should_exit, true);

  // Wake up any threads that might be blocked on condition variables
  // (like packet queues) to ensure responsive shutdown
  // This must happen BEFORE client cleanup to wake up any blocked threads
  static_cond_broadcast(&g_shutdown_cond);
  // NOTE: Do NOT call cond_destroy() on statically-initialized condition variables
  // g_shutdown_cond uses STATIC_COND_INIT which doesn't allocate resources that need cleanup
  // Calling cond_destroy() on a static cond is undefined behavior on some platforms

  // CRITICAL: Close all client sockets immediately to unblock receive threads
  // The signal handler only closed the listening socket, but client receive threads
  // are still blocked in recv_with_timeout(). We need to close their sockets to unblock them.
  log_info("Closing all client sockets to unblock receive threads...");

  // Use write lock since we're modifying client->socket
  rwlock_wrlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (atomic_load(&client->client_id) != 0 && client->socket != INVALID_SOCKET_VALUE) {
      socket_close(client->socket);
      client->socket = INVALID_SOCKET_VALUE;
    }
  }
  rwlock_wrunlock(&g_client_manager_rwlock);

  log_info("Signaling all clients to stop (sockets closed, g_server_should_exit set)...");

  // Stop and destroy server worker thread pool (stats logger, etc.)
  if (g_server_worker_pool) {
    thread_pool_destroy(g_server_worker_pool);
    g_server_worker_pool = NULL;
    log_info("Server worker thread pool stopped");
  }

  // Destroy rate limiter
  if (g_rate_limiter) {
    rate_limiter_destroy(g_rate_limiter);
    g_rate_limiter = NULL;
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

    // Use snapshot pattern to avoid holding both locks simultaneously
    // This prevents deadlock by not acquiring client_state_mutex while holding rwlock
    uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read is safe under rwlock

    // Clean up ANY client that was allocated, whether active or not
    // (disconnected clients may not be active but still have resources)
    clients_to_remove[client_count++] = client_id_snapshot;
  }
  rwlock_rdunlock(&g_client_manager_rwlock);

  // Remove all clients without holding any locks
  for (int i = 0; i < client_count; i++) {
    if (remove_client(&server_ctx, clients_to_remove[i]) != 0) {
      log_error("Failed to remove client %u during shutdown", clients_to_remove[i]);
    }
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

  // Clean up mDNS context
  if (g_mdns_ctx) {
    asciichat_mdns_shutdown(g_mdns_ctx);
    g_mdns_ctx = NULL;
    log_info("mDNS context shut down");
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

  // Shutdown TCP server (closes listen sockets and cleans up)
  tcp_server_shutdown(&g_tcp_server);

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
  buffer_pool_cleanup_global();

  // Clean up binary path cache explicitly
  // Note: This is also called by platform_cleanup() via atexit(), but it's idempotent
  // (checks g_cache_initialized and sets it to false, sets g_bin_path_cache to NULL)
  // Safe to call even if atexit() runs later
  platform_cleanup_binary_path_cache();

  // Clean up errno context (allocated strings, backtrace symbols)
  asciichat_errno_cleanup();

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
