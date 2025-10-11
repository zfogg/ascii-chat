/**
 * @file main.c
 * @brief ASCII-Chat Server - Main Entry Point and Connection Manager
 *
 * This file serves as the core of the ASCII-Chat server's modular architecture,
 * replacing the original monolithic server.c (2408+ lines) with a clean, maintainable
 * design split across multiple specialized modules.
 *
 * ARCHITECTURAL OVERVIEW:
 * ======================
 * This server implements a high-performance multi-client architecture where:
 * - Each client gets dedicated rendering threads (video @ 60fps + audio @ 172fps)
 * - Per-client packet queues eliminate shared bottlenecks and enable linear scaling
 * - Thread-safe design with proper mutex ordering prevents race conditions
 * - Platform abstraction supports Windows, Linux, and macOS seamlessly
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

#include "hashtable.h"
#include "platform/abstraction.h"
#include "platform/socket.h"
#include "platform/init.h"
#include "errno.h"

#ifdef _WIN32
#include <io.h>
#include <ws2tcpip.h> // For getaddrinfo(), gai_strerror(), inet_ntop()
#include <winsock2.h>
#else
#include <unistd.h>
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

#include "image2ascii/image.h"
#include "image2ascii/simd/ascii_simd.h"
#include "image2ascii/simd/common.h"
#include "common.h"
#include "asciichat_errno.h"
#include "network/network.h"
#include "options.h"
#include "buffer_pool.h"
#include "mixer.h"
#include "palette.h"
#include "audio.h"

#include "client.h"
#include "stream.h"
#include "stats.h"
#include "platform/string.h"
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
atomic_bool g_should_exit = false;

/**
 * @brief Shutdown check callback for library code
 *
 * Provides clean separation between application state and library code.
 * Registered with shutdown_register_callback() so library code can check
 * shutdown status without directly accessing g_should_exit.
 */
static bool check_shutdown(void) {
  return atomic_load(&g_should_exit);
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
static _Atomic socket_t listenfd = INVALID_SOCKET_VALUE;

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

/** Global server crypto state */
bool g_server_encryption_enabled = false;
private_key_t g_server_private_key = {0};
public_key_t g_client_whitelist[MAX_CLIENTS] = {0};
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
 * 1. Set atomic g_should_exit flag (signal-safe, checked by all threads)
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
 * allowing threads to check g_should_exit and exit gracefully.
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

  // STEP 1: Set atomic shutdown flag (checked by all worker threads)
  atomic_store(&g_should_exit, true);

  // STEP 2: Use printf for output (signal-safe)
  printf("\nSIGINT received - shutting down server...\n");
  (void)fflush(stdout);

  // STEP 3: Close listening socket to interrupt accept() in main loop
  // This is signal-safe on Windows and necessary to wake up blocked accept()
  socket_t current_listenfd = atomic_load(&listenfd);
  if (current_listenfd != INVALID_SOCKET_VALUE) {
    printf("DEBUG: Signal handler closing listening socket %d\n", (int)current_listenfd);
    (void)fflush(stdout);
    socket_close(current_listenfd);
    atomic_store(&listenfd, INVALID_SOCKET_VALUE);
  }

  // STEP 4: DO NOT access client data structures in signal handler
  // Signal handlers CANNOT safely use mutexes, rwlocks, or access complex data structures
  // This causes deadlocks and memory access violations because:
  // 1. Signal may interrupt a thread that already holds these locks
  // 2. Attempting to acquire locks in signal handler = instant deadlock
  // 3. Client array might be in an inconsistent state during modification
  //
  // SOLUTION: The listening socket closure above is sufficient to unblock accept_with_timeout()
  // The main thread will detect g_should_exit and properly close client sockets with timeouts

  // Debug message to see if handler completes
  printf("DEBUG: Signal handler completed\n");
  (void)fflush(stdout);

  // NOTE: Do NOT call log_destroy() here - it's not async-signal-safe
  // The main thread will handle cleanup when it detects g_should_exit
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
  atomic_store(&g_should_exit, true);

  printf("SIGTERM received - shutting down server...\n");
  (void)fflush(stdout);
  // Return immediately - signal handlers must be minimal
  // Main thread will detect g_should_exit and perform complete shutdown
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

  // Trigger lock debugging output (signal-safe)
  lock_debug_trigger_print();
}

/* ============================================================================
 * Main Function
 * ============================================================================
 */

/**
 * @brief ASCII-Chat Server main entry point - orchestrates the entire server architecture
 *
 * This function serves as the conductor of the ASCII-Chat server's modular architecture.
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
 * 1. Signal handlers set g_should_exit atomically
 * 2. All worker threads check flag and exit gracefully
 * 3. Main thread waits for all threads to finish
 * 4. Resources cleaned up in reverse dependency order
 * 5. No memory leaks or hanging processes
 *
 * @param argc Command line argument count
 * @param argv Command line argument vector
 * @return Exit code: 0 for success, non-zero for failure
 */

#ifdef USE_MIMALLOC_DEBUG
// Wrapper function for mi_stats_print to use with atexit()
// mi_stats_print takes a parameter, but atexit requires void(void)
extern void mi_stats_print(void *out);
static void print_mimalloc_stats(void) {
  mi_stats_print(NULL); // NULL = print to stderr
}
#endif

/**
 * Initialize crypto for server
 * @return 0 on success, -1 on error
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
      log_error("Failed to parse key: %s", opt_encrypt_key);
      log_error("This may be due to:");
      log_error("  - Wrong password for encrypted key");
      log_error("  - Unsupported key type (only Ed25519 is currently supported)");
      log_error("  - Corrupted key file");
      log_error("  - Invalid GPG key ID (use: gpg --list-secret-keys)");
      log_error("");
      log_error("Note: RSA and ECDSA keys are not yet supported");
      log_error("To generate an Ed25519 key: ssh-keygen -t ed25519");
      SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key type: %s", opt_encrypt_key);
      return -1;
    }
  } else if (strlen(opt_password) == 0) {
    // No identity key provided - server will run in simple mode
    // The server will still generate ephemeral keys for encryption, but no identity key
    g_server_private_key.type = KEY_TYPE_UNKNOWN;
    log_info("Server running without identity key (simple mode)");
  }

  // Load client whitelist if provided
  log_debug("opt_client_keys='%s', strlen=%zu", opt_client_keys, strlen(opt_client_keys));
  if (strlen(opt_client_keys) > 0) {
    log_debug("Loading whitelist from: %s", opt_client_keys);
    if (parse_client_keys(opt_client_keys, g_client_whitelist, &g_num_whitelisted_clients, MAX_CLIENTS) != 0) {
      SET_ERRNO(ERROR_CRYPTO_KEY, "Client key parsing failed");
      return -1;
    }
    log_debug("Loaded %zu whitelisted clients", g_num_whitelisted_clients);
    log_info("Server will only accept %zu whitelisted clients", g_num_whitelisted_clients);
  }

  g_server_encryption_enabled = true;
  log_info("Encryption: ENABLED");
  return 0;
}

int main(int argc, char *argv[]) {

  // Initialize platform-specific functionality (Winsock, etc)
  if (platform_init() != 0) {
    FATAL(ERROR_PLATFORM_INIT, "Failed to initialize platform");
  }
  (void)atexit(platform_cleanup);

#if defined(USE_MIMALLOC_DEBUG)
#if !defined(_WIN32)
  // Register mimalloc stats printer at exit
  (void)atexit(print_mimalloc_stats);
#else
  UNUSED(print_mimalloc_stats);
#endif
#endif

  // Note: --help and --version will exit(0) directly within options_init
  int options_result = options_init(argc, argv, false);
  if (options_result != ASCIICHAT_OK) {
    // options_init returns ERROR_USAGE for invalid options (after printing error)
    // Just exit with the returned error code
    return options_result;
  }

  // Initialize logging first so errors are properly logged
  const char *log_filename = (strlen(opt_log_file) > 0) ? opt_log_file : "server.log";
  safe_fprintf(stderr, "Initializing logging to: %s\n", log_filename);
  log_init(log_filename, LOG_DEBUG);
  log_info("Logging initialized to %s", log_filename);

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

  // Initialize lock debugging system after logging is fully set up
  log_info("SERVER: Initializing lock debug system...");
  int lock_debug_result = lock_debug_init();
  if (lock_debug_result != 0) {
    // Print detailed error context if available
    LOG_ERRNO_IF_SET("Lock debug system initialization failed");
    FATAL(ERROR_PLATFORM_INIT, "Lock debug system initialization failed");
  }
  log_info("SERVER: Lock debug system initialized successfully");

  // Initialize palette based on command line options
  const char *custom_chars = opt_palette_custom_set ? opt_palette_custom : NULL;
  if (apply_palette_config(opt_palette_type, custom_chars) != 0) {
    log_error("Failed to apply palette configuration");
    // Print detailed error context if available
    LOG_ERRNO_IF_SET("Palette configuration failed");
    return 1;
  }

  // Handle quiet mode - disable terminal output when opt_quiet is enabled
  log_set_terminal_output(!opt_quiet);
#ifdef DEBUG_MEMORY
  debug_memory_set_quiet_mode(opt_quiet);
#endif

#ifdef DEBUG_MEMORY
  (void)atexit(debug_memory_report);
#endif

  // Initialize global shared buffer pool
  data_buffer_pool_init_global();
  (void)atexit(data_buffer_pool_cleanup_global);

  // Register errno cleanup
  (void)atexit(asciichat_errno_cleanup);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat server starting...");

  // log_info("SERVER: Options initialized, using log file: %s", log_filename);
  int port = strtoint_safe(opt_port);
  if (port == INT_MIN) {
    log_error("Invalid port configuration: %s", opt_port);
    FATAL(ERROR_CONFIG, "Invalid port configuration: %s", opt_port);
  }
  log_info("SERVER: Port set to %d", port);

  log_info("SERVER: Initializing luminance palette...");
  ascii_simd_init();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);
  log_info("SERVER: RGB palettes precalculated");

  // Simple signal handling (temporarily disable complex threading signal handling)
  log_info("SERVER: Setting up simple signal handlers...");

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
  log_info("SERVER: Signal handling setup complete");

  // Start the lock debug thread (system already initialized earlier)
  log_info("SERVER: Starting lock debug thread...");
  int thread_result = lock_debug_start_thread();
  if (thread_result == 0) {
    log_info("SERVER: Lock debug thread started - press '?' to print held locks");
  } else {
    log_error("Failed to start lock debug thread");
  }
  // Initialize statistics system
  log_info("SERVER: Initializing statistics system...");
  if (stats_init() != 0) {
    FATAL(ERROR_THREAD, "Statistics system initialization failed");
  }
  log_info("Statistics system initialized");

  // Start statistics logging thread for periodic performance monitoring
  log_info("SERVER: Creating statistics logger thread...");
  if (ascii_thread_create(&g_stats_logger_thread, stats_logger_thread, NULL) != 0) {
    LOG_ERRNO_IF_SET("Statistics logger thread creation failed");
  } else {
    g_stats_logger_thread_created = true;
    log_info("Statistics logger thread started");
  }

  // Network setup - Dual-stack IPv6 with IPv4 support
  log_info("SERVER: Setting up network sockets...");
  struct sockaddr_storage serv_addr; // Address-family-agnostic storage
  struct sockaddr_storage client_addr;
  socklen_t client_len = sizeof(client_addr);

  // Parse bind address from opt_address
  // Default is "::" (dual-stack: all IPv6 + IPv4-mapped)
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6; // IPv6 socket (with IPV6_V6ONLY=0 for dual-stack)
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST; // For bind() + numeric address

  char port_str[16];
  SAFE_SNPRINTF(port_str, sizeof(port_str), "%d", port);

  log_info("SERVER: Resolving bind address '%s' port %s...", opt_address, port_str);
  int getaddr_result = getaddrinfo(opt_address, port_str, &hints, &res);
  if (getaddr_result != 0) {
    // Try IPv4 fallback if IPv6 resolution fails
    hints.ai_family = AF_INET;
    getaddr_result = getaddrinfo(opt_address, port_str, &hints, &res);
    if (getaddr_result != 0) {
      FATAL(ERROR_NETWORK, "Failed to resolve bind address '%s': %s", opt_address, gai_strerror(getaddr_result));
    }
    log_info("SERVER: Using IPv4 binding (address family: AF_INET)");
  } else {
    log_info("SERVER: Using dual-stack IPv6 binding (address family: AF_INET6)");
  }

  // Copy resolved address to serv_addr
  if (res->ai_addrlen > sizeof(serv_addr)) {
    freeaddrinfo(res);
    FATAL(ERROR_NETWORK, "Address size too large: %zu bytes", (size_t)res->ai_addrlen);
  }
  memcpy(&serv_addr, res->ai_addr, res->ai_addrlen);
  int address_family = res->ai_family;
  socklen_t addrlen = res->ai_addrlen;
  freeaddrinfo(res);

  log_info("SERVER: Creating listen socket...");
  socket_t new_listenfd = socket_create(address_family, SOCK_STREAM, 0);
  if (new_listenfd == INVALID_SOCKET_VALUE) {
    FATAL(ERROR_NETWORK, "Failed to create socket: %s", SAFE_STRERROR(errno));
  }
  log_info("SERVER: Listen socket created (fd=%d)", new_listenfd);

  // Set socket options
  int yes = 1;
  if (socket_setsockopt(new_listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    FATAL(ERROR_NETWORK, "setsockopt SO_REUSEADDR failed: %s", SAFE_STRERROR(errno));
  }

  // Enable dual-stack mode for IPv6 sockets (allow IPv4-mapped addresses)
  if (address_family == AF_INET6) {
    int ipv6only = 0; // 0 = dual-stack (accept both IPv6 and IPv4-mapped)
    if (socket_setsockopt(new_listenfd, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6only, sizeof(ipv6only)) == -1) {
      log_warn("Failed to set IPV6_V6ONLY=0 (dual-stack mode): %s", SAFE_STRERROR(errno));
      log_warn("Server may only accept IPv6 connections");
    } else {
      log_info("SERVER: Dual-stack mode enabled (IPv6 + IPv4-mapped addresses)");
    }
  }

  // If we Set keep-alive on the listener before accept(), connfd will inherit it.
  if (set_socket_keepalive(new_listenfd) < 0) {
    log_warn("Failed to set keep-alive on listener: %s", SAFE_STRERROR(errno));
  }

  // Bind socket
  log_info("Server binding to %s:%d", opt_address, port);
  if (socket_bind(new_listenfd, (struct sockaddr *)&serv_addr, addrlen) < 0) {
    FATAL(ERROR_NETWORK_BIND, "Socket bind failed: %s", SAFE_STRERROR(errno));
  }

  // Listen for connections
  if (socket_listen(new_listenfd, 10) < 0) {
    FATAL(ERROR_NETWORK, "Connection listen failed: %s", SAFE_STRERROR(errno));
  }

  // Store the socket in the atomic variable
  atomic_store(&listenfd, new_listenfd);

  struct timespec last_stats_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

  // Initialize synchronization primitives
  if (rwlock_init(&g_client_manager_rwlock) != 0) {
    FATAL(ERROR_THREAD, "Failed to initialize client manager rwlock");
  }

  // Lock debug system already initialized earlier in main()

  // Check if SIGINT was received during initialization
  if (atomic_load(&g_should_exit)) {
    // Skip rest of initialization and go straight to main loop
    // which will detect g_should_exit and exit cleanly
    goto main_loop;
  }

  // Lock debug thread already started earlier in main()

  // NOTE: g_client_manager is already zero-initialized in client.c with = {0}
  // We only need to initialize the mutex
  mutex_init(&g_client_manager.mutex);

  // Initialize client hash table for O(1) lookup
  g_client_manager.client_hashtable = hashtable_create();
  if (!g_client_manager.client_hashtable) {
    LOG_ERRNO_IF_SET("Failed to create client hash table");
    FATAL(ERROR_MEMORY, "Failed to create client hash table");
  }

  // Initialize audio mixer if audio is enabled
  if (opt_audio_enabled && !atomic_load(&g_should_exit)) {
    log_info("SERVER: Initializing audio mixer for per-client audio rendering...");
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      LOG_ERRNO_IF_SET("Failed to initialize audio mixer");
      if (!atomic_load(&g_should_exit)) {
        log_error("Failed to initialize audio mixer");
      }
    } else {
      if (!atomic_load(&g_should_exit)) {
        log_info("SERVER: Audio mixer initialized successfully for per-client audio rendering");
      }
    }
  } else if (!atomic_load(&g_should_exit)) {
    log_info("SERVER: Audio disabled, skipping audio mixer initialization");
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
  while (!atomic_load(&g_should_exit)) {
    // Debug: Log loop iteration
    // Check if we received a shutdown signal
    if (atomic_load(&g_should_exit)) {
      break;
    }

    // Rate-limited logging: Only show status when client count actually changes
    // This prevents log spam while maintaining visibility into server state
    static int last_logged_count = -1;
    if (g_client_manager.client_count != last_logged_count) {
      log_info("Waiting for client connections... (%d/%d clients)", g_client_manager.client_count, MAX_CLIENTS);
      last_logged_count = g_client_manager.client_count;
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
    if (g_client_manager.client_count > 0) {
      rwlock_rdlock(&g_client_manager_rwlock);
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
      rwlock_rdunlock(&g_client_manager_rwlock);
    }

    // Process cleanup tasks without holding lock (prevents infinite loops)
    for (int i = 0; i < cleanup_count; i++) {
      bool is_shutting_down = atomic_load(&g_should_exit);
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

    // Check if listening socket was closed by signal handler
    if (atomic_load(&listenfd) == INVALID_SOCKET_VALUE) {
      break;
    }

    // Accept network connection with timeout

    // Check g_should_exit right before accept
    if (atomic_load(&g_should_exit)) {
      break;
    }

    // Check if listenfd is still valid before calling accept_with_timeout
    socket_t current_listenfd = atomic_load(&listenfd);
    if (current_listenfd == INVALID_SOCKET_VALUE) {
      log_debug("Main loop: listenfd is invalid, breaking accept loop");
      break;
    }

    // CRITICAL: Final check right before accept to prevent race condition
    // The signal handler could close the socket between atomic_load() and accept()
    socket_t final_listenfd = atomic_load(&listenfd);
    if (final_listenfd == INVALID_SOCKET_VALUE) {
      log_debug("Main loop: listenfd was closed by signal handler, breaking accept loop");
      break;
    }

    ASSERT_NO_ERRNO();

    int client_sock = accept_with_timeout(final_listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);

#ifdef _WIN32
    int saved_errno = asciichat_errno_context.system_errno; // Windows socket error
#else
    int saved_errno = errno; // POSIX errno
#endif
#ifdef DEBUG_NETWORK
    log_debug("Main loop: accept_with_timeout returned: client_sock=%d, errno=%d (%s)", client_sock, saved_errno,
              client_sock < 0 ? socket_get_error_string() : "success");
#endif
    if (client_sock < 0) {
      if (saved_errno == ETIMEDOUT) {
#ifdef DEBUG_NETWORK
        log_debug("Main loop: Accept timed out, checking g_should_exit=%d", atomic_load(&g_should_exit));
#endif
#ifdef DEBUG_MEMORY
        // debug_memory_report();
#endif
        if (HAS_ERRNO(&asciichat_errno_context)) {
          if (asciichat_errno_context.system_errno == ETIMEDOUT) {
            log_warn("Clearing timeout error");
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
        if (atomic_load(&g_should_exit)) {
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
        log_debug("accept() failed because socket was closed: %s", strerror(saved_errno));
#endif
        break;
      }
      // log_error("Network accept failed: %s", network_error_string(saved_errno));
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
      log_info("New client connected from %s:%d (IPv4)", client_ip, client_port);
    } else if (((struct sockaddr *)&client_addr)->sa_family == AF_INET6) {
      // IPv6 address
      struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&client_addr;
      inet_ntop(AF_INET6, &addr_in6->sin6_addr, client_ip, sizeof(client_ip));
      client_port = ntohs(addr_in6->sin6_port);

      // Check if it's an IPv4-mapped IPv6 address (::ffff:x.x.x.x)
      if (IN6_IS_ADDR_V4MAPPED(&addr_in6->sin6_addr)) {
        log_info("New client connected from [%s]:%d (IPv4-mapped IPv6)", client_ip, client_port);
      } else {
        log_info("New client connected from [%s]:%d (IPv6)", client_ip, client_port);
      }
    } else {
      safe_snprintf(client_ip, sizeof(client_ip), "unknown");
      log_warn("New client connected with unknown address family %d", ((struct sockaddr *)&client_addr)->sa_family);
    }

    // Add client to multi-client manager
    int client_id = add_client(client_sock, client_ip, client_port);
    if (client_id < 0) {
      log_error("Failed to add client, rejecting connection");
      socket_close(client_sock);
      continue;
    }

    log_info("Client %d added successfully, total clients: %d", client_id, g_client_manager.client_count);

    // Check if we should exit after processing this client
    if (atomic_load(&g_should_exit)) {
      break;
    }

    if (HAS_ERRNO(&asciichat_errno_context)) {
      PRINT_ERRNO_CONTEXT(&asciichat_errno_context);
    }
    ASSERT_NO_ERRNO();
  }

  // Cleanup
  log_info("Server shutting down...");
  atomic_store(&g_should_exit, true);

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

  log_info("Signaling all clients to stop (sockets closed, g_should_exit set)...");

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
  if (g_client_manager.client_hashtable) {
    hashtable_destroy(g_client_manager.client_hashtable);
    g_client_manager.client_hashtable = NULL;
  }

  // Clean up audio mixer
  if (g_audio_mixer) {
    mixer_destroy(g_audio_mixer);
    g_audio_mixer = NULL;
  }

  // Clean up synchronization primitives
  rwlock_destroy(&g_client_manager_rwlock);
  mutex_destroy(&g_client_manager.mutex);

  // Clean up statistics system
  stats_cleanup();

  // Clean up lock debugging system
  lock_debug_print_state();
  lock_debug_cleanup();

  // Close listen socket
  socket_t current_listenfd = atomic_load(&listenfd);
  if (current_listenfd != INVALID_SOCKET_VALUE) {
    socket_close(current_listenfd);
  }

  // Clean up SIMD caches
  simd_caches_destroy_all();

  // Join the lock debug thread as one of the very last things before exit
  lock_debug_cleanup_thread();

  log_info("Server shutdown complete");

  asciichat_error_stats_print();

  log_destroy();
  return 0;
}
