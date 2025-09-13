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

#include "platform/abstraction.h"
#include "platform/init.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdatomic.h>

#include "image2ascii/image.h"
#include "image2ascii/ascii.h"
#include "image2ascii/simd/ascii_simd.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "buffer_pool.h"
#include "platform/terminal.h"
#include "aspect_ratio.h"
#include "mixer.h"
#include "palette.h"
#include "os/audio.h"

#include "client.h"
#include "protocol.h"
#include "stream.h"
#include "render.h"
#include "stats.h"

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
 * @brief Platform-abstracted mutex for shutdown coordination
 *
 * Used with g_shutdown_cond to implement interruptible sleep operations.
 * This allows threads to sleep for specific durations but wake immediately
 * when shutdown is requested, preventing long delays during server exit.
 */
static_mutex_t g_shutdown_mutex = STATIC_MUTEX_INIT;

/**
 * @brief Condition variable for fast thread wakeup during shutdown
 *
 * Broadcasted by signal handlers to wake all sleeping threads immediately.
 * This enables responsive shutdown instead of waiting for timeouts to expire.
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
static socket_t listenfd = INVALID_SOCKET_VALUE;

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
 * - No malloc/free (heap corruption if interrupted during allocation)
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

  // STEP 2: Signal-safe logging - avoid log_info() which may not be signal-safe
  const char msg[] = "SIGINT received - shutting down server...\n";
  write(STDOUT_FILENO, msg, sizeof(msg) - 1);

  // STEP 3: Wake up all sleeping threads immediately
  static_cond_broadcast(&g_shutdown_cond);

  // STEP 4: Close all client sockets to interrupt blocking receive threads (signal-safe)
  // Note: This is done without mutex locking since signal handlers should be minimal
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
      // On Windows, shutdown is needed to interrupt blocking recv()
      socket_shutdown(g_client_manager.clients[i].socket, SHUT_RDWR);
      socket_close(g_client_manager.clients[i].socket);
      g_client_manager.clients[i].socket = INVALID_SOCKET_VALUE;
    }
  }

  // STEP 5: Close listening socket to interrupt accept() - this is signal-safe
  if (listenfd != INVALID_SOCKET_VALUE) {
    socket_close(listenfd);
  }

  // STEP 6: Return immediately - don't do complex operations in signal handler
  // Complex cleanup is handled by main thread when it sees g_should_exit = true
  // This prevents deadlocks and ensures deterministic shutdown behavior
}

/**
 * @brief Handler for SIGTERM (termination request) signals on POSIX systems
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
#ifndef _WIN32
static void sigterm_handler(int sigterm) {
  (void)(sigterm);
  atomic_store(&g_should_exit, true);

  // Use log system in signal handler - it has its own safety mechanisms
  log_info("SIGTERM received - shutting down server...");

  // Wake up all sleeping threads immediately - signal-safe operation
  static_cond_broadcast(&g_shutdown_cond);

  // Close listening socket to interrupt accept() - signal-safe
  if (listenfd != INVALID_SOCKET_VALUE) {
    socket_close(listenfd);
  }

  // NOTE: Client socket closure handled by main shutdown sequence (not signal handler)
  // Signal handler should be minimal - just set flag and wake threads
  // Main thread will properly close client sockets with mutex protection
}
#endif

/**
 * @brief Windows-compatible SIGTERM handler with limited signal support
 *
 * Windows has limited POSIX signal support compared to Unix systems.
 * This handler provides basic termination handling but relies more heavily
 * on the main thread for complex cleanup operations.
 *
 * WINDOWS SIGNAL LIMITATIONS:
 * - Signals run in separate threads (unlike POSIX inline execution)
 * - Limited set of supported signals (no SIGPIPE, limited SIGTERM)
 * - Different timing and delivery semantics
 * - Some async-signal-safe restrictions don't apply the same way
 *
 * @param sigterm The signal number (unused, required by signal handler signature)
 */
#ifdef _WIN32
static void sigterm_handler(int sigterm) {
  (void)(sigterm);
  atomic_store(&g_should_exit, true);
  log_info("SIGTERM received - shutting down server...");

  // Wake up all sleeping threads immediately
  static_cond_broadcast(&g_shutdown_cond);

  // Close listening socket to interrupt accept()
  if (listenfd != INVALID_SOCKET_VALUE) {
    socket_close(listenfd);
  }
}
#endif

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
int main(int argc, char *argv[]) {

  // Initialize platform-specific functionality (Winsock, etc)
  if (platform_init() != 0) {
    fprintf(stderr, "FATAL: Failed to initialize platform\n");
    return 1;
  }
  atexit(platform_cleanup);


  options_init(argc, argv, false);

  // Initialize logging - use specified log file or default
  const char *log_filename = (strlen(opt_log_file) > 0) ? opt_log_file : "server.log";
  log_init(log_filename, LOG_DEBUG);

  // Initialize palette based on command line options
  const char *custom_chars = opt_palette_custom_set ? opt_palette_custom : NULL;
  if (apply_palette_config(opt_palette_type, custom_chars) != 0) {
    log_error("Failed to apply palette configuration");
    return 1;
  }

  // Handle quiet mode - disable terminal output when opt_quiet is enabled
  log_set_terminal_output(!opt_quiet);
#ifdef DEBUG_MEMORY
  debug_memory_set_quiet_mode(opt_quiet);
#endif

  atexit(log_destroy);

#ifdef DEBUG_MEMORY
  atexit(debug_memory_report);
#endif

  // Initialize global shared buffer pool
  data_buffer_pool_init_global();
  atexit(data_buffer_pool_cleanup_global);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat server starting...");

  log_info("SERVER: Options initialized, using log file: %s", log_filename);
  int port = strtoint_safe(opt_port);
  if (port == INT_MIN) {
    log_error("Invalid port configuration: %s", opt_port);
    exit(EXIT_FAILURE);
  }
  log_info("SERVER: Port set to %d", port);

  log_info("SERVER: Initializing luminance palette...");
  ascii_simd_init();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);
  log_info("SERVER: RGB palettes precalculated");

  // Simple signal handling (temporarily disable complex threading signal handling)
  log_info("SERVER: Setting up simple signal handlers...");

  // Handle Ctrl+C for cleanup
  signal(SIGINT, sigint_handler);
  // Handle termination signal (SIGTERM is defined with limited support on Windows)
  signal(SIGTERM, sigterm_handler);

#ifndef _WIN32
  // Ignore SIGPIPE (not on Windows)
  signal(SIGPIPE, SIG_IGN);
#endif
  log_info("SERVER: Signal handling setup complete");

  // Start statistics logging thread for periodic performance monitoring
  log_info("SERVER: Creating statistics logger thread...");
  if (ascii_thread_create(&g_stats_logger_thread, stats_logger_thread, NULL) != 0) {
    log_error("Failed to create statistics logger thread");
  } else {
    g_stats_logger_thread_created = true;
    log_info("Statistics logger thread started");
  }

  // Network setup
  log_info("SERVER: Setting up network sockets...");
  struct sockaddr_in serv_addr;
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  log_info("SERVER: Creating listen socket...");
  listenfd = socket_create(AF_INET, SOCK_STREAM, 0);
  if (listenfd == INVALID_SOCKET_VALUE) {
    log_fatal("Failed to create socket: %s", strerror(errno));
    exit(1);
  }
  log_info("SERVER: Listen socket created (fd=%d)", listenfd);

  log_info("Server listening on port %d", port);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  // Set socket options
  int yes = 1;
  if (socket_setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    log_fatal("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    exit(ASCIICHAT_ERR_NETWORK);
  }

  // If we Set keep-alive on the listener before accept(), connfd will inherit it.
  if (set_socket_keepalive(listenfd) < 0) {
    log_warn("Failed to set keep-alive on listener: %s", strerror(errno));
  }

  // Bind socket
  if (socket_bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    log_fatal("Socket bind failed: %s", strerror(errno));
    exit(1);
  }

  // Listen for connections
  if (socket_listen(listenfd, 10) < 0) {
    log_fatal("Connection listen failed: %s", strerror(errno));
    exit(1);
  }

  struct timespec last_stats_time;
  clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

  // Initialize synchronization primitives
  if (rwlock_init(&g_client_manager_rwlock) != 0) {
    log_fatal("Failed to initialize client manager rwlock");
    exit(1);
  }
  if (mutex_init(&g_frame_cache_mutex) != 0) {
    log_fatal("Failed to initialize frame cache mutex");
    exit(1);
  }

  // Initialize client manager
  memset(&g_client_manager, 0, sizeof(g_client_manager));
  mutex_init(&g_client_manager.mutex);
  g_client_manager.next_client_id = 0;

  // Initialize client hash table for O(1) lookup
  g_client_manager.client_hashtable = hashtable_create();
  if (!g_client_manager.client_hashtable) {
    log_fatal("Failed to create client hash table");
    exit(1);
  }

  // Initialize audio mixer if audio is enabled
  if (opt_audio_enabled) {
    log_info("SERVER: Initializing audio mixer for per-client audio rendering...");
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      log_error("Failed to initialize audio mixer");
    } else {
      log_info("SERVER: Audio mixer initialized successfully for per-client audio rendering");
    }
  } else {
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

  while (!atomic_load(&g_should_exit)) {
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

    rwlock_rdlock(&g_client_manager_rwlock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      // Check if this client has been marked inactive by its receive thread
      if (client->client_id != 0 && !client->active && ascii_thread_is_initialized(&client->receive_thread)) {
        // Collect cleanup task
        cleanup_tasks[cleanup_count].client_id = client->client_id;
        cleanup_tasks[cleanup_count].receive_thread = client->receive_thread;
        cleanup_count++;

        // Clear the thread handle immediately to avoid double-join
        memset(&client->receive_thread, 0, sizeof(asciithread_t));
      }
    }
    rwlock_unlock(&g_client_manager_rwlock);

    // Process cleanup tasks without holding lock (prevents infinite loops)
    for (int i = 0; i < cleanup_count; i++) {
      log_info("Cleaning up disconnected client %u", cleanup_tasks[i].client_id);
      // Wait for receive thread to finish
      ascii_thread_join(&cleanup_tasks[i].receive_thread, NULL);
      // Remove the client and clean up resources
      remove_client(cleanup_tasks[i].client_id);
    }

    // Accept network connection with timeout
    log_debug("Main loop: Calling accept_with_timeout on fd=%d with timeout=%d", listenfd, ACCEPT_TIMEOUT);
    int client_sock = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    int saved_errno = errno; // Capture errno immediately to prevent corruption
    log_debug("Main loop: accept_with_timeout returned: client_sock=%d, errno=%d (%s)", client_sock, saved_errno,
              client_sock < 0 ? strerror(saved_errno) : "success");
    if (client_sock < 0) {
      if (saved_errno == ETIMEDOUT) {
        log_debug("Main loop: Accept timed out, checking g_should_exit=%d", atomic_load(&g_should_exit));
        // Timeout is normal, just continue
        // log_debug("Accept timed out after %d seconds, continuing loop", ACCEPT_TIMEOUT);
#ifdef DEBUG_MEMORY
        // debug_memory_report();
#endif
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
      // log_error("Network accept failed: %s", network_error_string(saved_errno));
      continue;
    }

    // Log client connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    log_info("New client connected from %s:%d", client_ip, client_port);

    // Add client to multi-client manager
    int client_id = add_client(client_sock, client_ip, client_port);
    if (client_id < 0) {
      log_error("Failed to add client, rejecting connection");
      close(client_sock);
      continue;
    }

    log_info("Client %d added successfully, total clients: %d", client_id, g_client_manager.client_count);

    // Check if we should exit after processing this client
    if (atomic_load(&g_should_exit)) {
      break;
    }
  }

  // Cleanup
  log_info("Server shutting down...");
  atomic_store(&g_should_exit, true);

  // Wake up all sleeping threads before waiting for them
  static_cond_broadcast(&g_shutdown_cond);

  // CRITICAL: Close all client sockets to interrupt blocking receive_packet() calls
  log_info("Closing all client sockets to interrupt blocking I/O...");
  rwlock_wrlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
      log_debug("Closing socket for client %u to interrupt receive thread", g_client_manager.clients[i].client_id);
      socket_shutdown(g_client_manager.clients[i].socket, SHUT_RDWR);
      socket_close(g_client_manager.clients[i].socket);
      g_client_manager.clients[i].socket = INVALID_SOCKET_VALUE;
    }
  }
  rwlock_unlock(&g_client_manager_rwlock);

  // Wait for stats logger thread to finish
  if (g_stats_logger_thread_created) {
    log_info("Waiting for stats logger thread to finish...");
    log_info("About to call ascii_thread_join for stats thread");
    int join_result = ascii_thread_join(&g_stats_logger_thread, NULL);
    log_info("ascii_thread_join returned: %d", join_result);
    log_info("Stats logger thread stopped");
    g_stats_logger_thread_created = false;
  }

  // Clean up all connected clients
  log_info("Cleaning up connected clients...");

  // First, close all client sockets to interrupt receive threads
  rwlock_wrlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->active && client->socket != INVALID_SOCKET_VALUE) {
      log_debug("Closing socket for client %u", client->client_id);
      socket_shutdown(client->socket, SHUT_RDWR);
      socket_close(client->socket);
      client->socket = INVALID_SOCKET_VALUE;
    }
  }
  rwlock_unlock(&g_client_manager_rwlock);

  // Collect active clients to clean up (without holding lock during cleanup)
  uint32_t active_clients[MAX_CLIENTS];
  int active_count = 0;
  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active) {
      active_clients[active_count++] = g_client_manager.clients[i].client_id;
    }
  }
  rwlock_unlock(&g_client_manager_rwlock);

  // Clean up each active client
  for (int i = 0; i < active_count; i++) {
    log_info("Removing client %u", active_clients[i]);
    remove_client(active_clients[i]);
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
  mutex_destroy(&g_frame_cache_mutex);
  mutex_destroy(&g_client_manager.mutex);

  // Close listen socket
  if (listenfd != INVALID_SOCKET_VALUE) {
    socket_close(listenfd);
  }

  log_info("Server shutdown complete");
  return 0;
}
