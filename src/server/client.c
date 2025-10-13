/**
 * @file client.c
 * @brief Per-Client Lifecycle Management and Threading Coordination
 *
 * This module manages the complete lifecycle of individual clients in the ASCII-Chat
 * server's modular architecture. It replaced the client management portions of the
 * original monolithic server.c, providing clean separation of concerns and improved
 * maintainability.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Client connection establishment and initialization
 * 2. Per-client thread creation and management (receive, send, render)
 * 3. Client state management with thread-safe access patterns
 * 4. Client disconnection handling and resource cleanup
 * 5. Hash table management for O(1) client lookups
 * 6. Integration point between main.c and other modules
 *
 * THREADING ARCHITECTURE PER CLIENT:
 * ===================================
 * Each connected client spawns multiple dedicated threads:
 * - Receive Thread: Handles incoming packets from client (calls protocol.c functions)
 * - Send Thread: Manages outgoing packet delivery using packet queues
 * - Video Render Thread: Generates ASCII frames at 60fps (render.c)
 * - Audio Render Thread: Mixes audio streams at 172fps (render.c)
 *
 * This per-client threading model provides several advantages:
 * - Linear performance scaling (no shared bottlenecks)
 * - Fault isolation (one client's issues don't affect others)
 * - Simplified synchronization (each client owns its resources)
 * - Real-time performance guarantees per client
 *
 * DATA STRUCTURES AND SYNCHRONIZATION:
 * ====================================
 * The module manages two primary data structures:
 *
 * 1. client_manager_t (global singleton):
 *    - Array of client_info_t structs (backing storage)
 *    - Hash table for O(1) client_id -> client_info_t lookups
 *    - Protected by reader-writer lock (g_client_manager_rwlock)
 *    - Allows concurrent reads, exclusive writes
 *
 * 2. client_info_t (per-client state):
 *    - Network connection details and capabilities
 *    - Thread handles and synchronization primitives
 *    - Media buffers (video/audio) and packet queues
 *    - Terminal capabilities and rendering preferences
 *    - Protected by per-client mutex (client_state_mutex)
 *
 * CRITICAL SYNCHRONIZATION PATTERNS:
 * ===================================
 *
 * LOCK ORDERING PROTOCOL (prevents deadlocks):
 * 1. Always acquire g_client_manager_rwlock FIRST
 * 2. Then acquire per-client mutexes if needed
 * 3. Release in reverse order
 *
 * SNAPSHOT PATTERN (reduces lock contention):
 * 1. Acquire mutex
 * 2. Copy needed state variables to local copies
 * 3. Release mutex immediately
 * 4. Process using local copies without locks
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - main.c: Calls add_client() and remove_client() from main loop
 * - protocol.c: Functions called by client receive threads for packet processing
 * - render.c: Render thread functions created per client
 * - stream.c: Stream generation functions called by render threads
 * - stats.c: Accesses client data for performance monitoring
 *
 * THREAD LIFECYCLE MANAGEMENT:
 * ============================
 * Thread creation order (in add_client()):
 * 1. Initialize client data structures and mutexes
 * 2. Create send thread (for outgoing packet delivery)
 * 3. Create receive thread (for incoming packet processing)
 * 4. Create render threads (video + audio generation)
 *
 * Thread termination order (in remove_client()):
 * 1. Set shutdown flags (causes threads to exit main loops)
 * 2. Join send thread (cleanest exit, no blocking I/O)
 * 3. Join receive thread (may be blocked on network I/O)
 * 4. Join render threads (computational work, clean exit)
 * 5. Clean up resources (queues, buffers, mutexes)
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * The original server.c contained all client management code inline, making it:
 * - Hard to understand the client lifecycle
 * - Difficult to modify threading behavior
 * - Impossible to isolate client-related bugs
 * - Challenging to add new client features
 *
 * This modular approach provides:
 * - Clear separation of client vs. server concerns
 * - Easier testing of client management logic
 * - Better code reuse and maintenance
 * - Future extensibility for new client types
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 * @see main.c For overall server architecture
 * @see render.c For per-client rendering implementation
 * @see protocol.c For client packet processing
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#ifndef _WIN32
#include <netinet/tcp.h>
#endif

#include "client.h"
#include "protocol.h"
#include "render.h"
#include "stream.h"
#include "crypto.h"
#include "crypto/handshake.h"
#include "crypto/crypto.h"
#include "common.h"
#include "asciichat_errno.h"
#include "options.h"
#include "buffer_pool.h"
#include "network/network.h"
#include "network/packet.h"
#include "packet_queue.h"
#include "audio.h"
#include "mixer.h"
#include "video_frame.h"
#include "hashtable.h"
#include "platform/abstraction.h"
#include "platform/string.h"
#include "platform/socket.h"
#include "crc32.h"

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

/**
 * @brief Global client manager singleton - central coordination point
 *
 * This is the primary data structure for managing all connected clients.
 * It serves as the bridge between main.c's connection accept loop and
 * the per-client threading architecture.
 *
 * STRUCTURE COMPONENTS:
 * - clients[]: Array backing storage for client_info_t structs
 * - client_hashtable: O(1) lookup table for client_id -> client_info_t*
 * - client_count: Current number of active clients
 * - mutex: Legacy mutex (mostly replaced by rwlock)
 * - next_client_id: Monotonic counter for unique client identification
 *
 * THREAD SAFETY: Protected by g_client_manager_rwlock for concurrent access
 */
client_manager_t g_client_manager = {0};

/**
 * @brief Reader-writer lock protecting the global client manager
 *
 * This lock enables high-performance concurrent access patterns:
 * - Multiple threads can read client data simultaneously (stats, rendering)
 * - Only one thread can modify client data at a time (add/remove operations)
 * - Eliminates contention between read-heavy operations
 *
 * USAGE PATTERN:
 * - Read operations: rwlock_rdlock() for client lookups, stats gathering
 * - Write operations: rwlock_wrlock() for add_client(), remove_client()
 * - Always acquire THIS lock before per-client mutexes (lock ordering)
 */
rwlock_t g_client_manager_rwlock = {0};

// External globals from main.c
extern atomic_bool g_server_should_exit; ///< Global shutdown flag from main.c
extern mixer_t *g_audio_mixer;           ///< Global audio mixer from main.c

// Forward declarations for internal functions
// client_receive_thread is implemented below
void *client_send_thread_func(void *arg);         ///< Client packet send thread
void broadcast_server_state_to_all_clients(void); ///< Notify all clients of state changes

/* ============================================================================
 * Client Lookup Functions
 * ============================================================================
 */

/**
 * @brief Fast O(1) client lookup by ID using hash table
 *
 * This is the primary method for locating clients throughout the server.
 * It uses a hash table for constant-time lookups regardless of client count,
 * making it suitable for high-performance operations like rendering and stats.
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Time Complexity: O(1) average case, O(n) worst case (hash collision)
 * - Space Complexity: O(1)
 * - Thread Safety: Hash table is internally thread-safe for lookups
 *
 * USAGE PATTERNS:
 * - Called by render threads to find target clients for frame generation
 * - Used by protocol handlers to locate clients for packet processing
 * - Stats collection for per-client performance monitoring
 *
 * @param client_id Unique identifier for the client (0 is invalid)
 * @return Pointer to client_info_t if found, NULL if not found or invalid ID
 *
 * @note Does not require external locking - hash table provides thread safety
 * @note Returns direct pointer to client struct - caller should use snapshot pattern
 */
client_info_t *find_client_by_id(uint32_t client_id) {
  if (client_id == 0 || !g_client_manager.client_hashtable) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client ID or client hashtable not initialized");
    return NULL;
  }

  // Protect hashtable lookup with read lock to prevent concurrent access issues
  rwlock_rdlock(&g_client_manager_rwlock);
  client_info_t *result = (client_info_t *)hashtable_lookup(g_client_manager.client_hashtable, client_id);
  rwlock_rdunlock(&g_client_manager_rwlock);

  if (!result) {
    log_warn("Client not found for ID %u", client_id);
  }

  return result;
}

/**
 * @brief Find client by socket descriptor using linear search
 *
 * This function provides socket-based client lookup, primarily used during
 * connection establishment before client IDs are assigned. Less efficient
 * than find_client_by_id() but necessary for socket-based operations.
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Time Complexity: O(n) where n = number of active clients
 * - Space Complexity: O(1)
 * - Thread Safety: Internally acquires read lock on g_client_manager_rwlock
 *
 * USAGE PATTERNS:
 * - Connection establishment during add_client() processing
 * - Socket error handling and cleanup operations
 * - Debugging and diagnostic functions
 *
 * @param socket Platform-abstracted socket descriptor to search for
 * @return Pointer to client_info_t if found, NULL if not found
 *
 * @note Only searches active clients (avoids returning stale entries)
 * @note Caller should use snapshot pattern when accessing returned client data
 */
client_info_t *find_client_by_socket(socket_t socket) {
  rwlock_rdlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].socket == socket && atomic_load(&g_client_manager.clients[i].active)) {
      client_info_t *client = &g_client_manager.clients[i];
      rwlock_rdunlock(&g_client_manager_rwlock);
      return client;
    }
  }

  rwlock_rdunlock(&g_client_manager_rwlock);
  return NULL;
}

/* ============================================================================
 * Client Management Functions
 * ============================================================================
 */

int add_client(socket_t socket, const char *client_ip, int port) {
  rwlock_wrlock(&g_client_manager_rwlock);

  // Find empty slot - this is the authoritative check
  int slot = -1;
  int existing_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (slot == -1 && atomic_load(&g_client_manager.clients[i].client_id) == 0) {
      slot = i; // Take first available slot
    } else {
      existing_count++;
    }
  }

  if (slot == -1) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "No available client slots (all %d slots are in use)", MAX_CLIENTS);
    log_error("No available client slots (all %d slots are in use)", MAX_CLIENTS);

    // Send a rejection message to the client before closing
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    send(socket, reject_msg, strlen(reject_msg), 0); // MSG_NOSIGNAL not on Windows

    return -1;
  }

  // Update client_count to match actual count before adding new client
  g_client_manager.client_count = existing_count;

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];
  memset(client, 0, sizeof(client_info_t));

  client->socket = socket;
  log_debug("SOCKET_DEBUG: Client socket set to %d", socket);
  atomic_store(&client->client_id, atomic_fetch_add(&g_client_manager.next_client_id, 1) + 1);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  atomic_store(&client->active, true);
  atomic_store(&client->shutting_down, false);
  atomic_store(&client->last_rendered_grid_sources, 0); // Render thread updates this
  atomic_store(&client->last_sent_grid_sources, 0);     // Send thread updates this
  log_info("CLIENT SLOT ASSIGNED: client_id=%u assigned to slot %d, socket=%d", atomic_load(&client->client_id), slot,
           socket);
  client->connected_at = time(NULL);

  // Initialize crypto context for this client
  memset(&client->crypto_handshake_ctx, 0, sizeof(client->crypto_handshake_ctx));
  client->crypto_initialized = false;

  // Configure socket options for optimal performance
  if (set_socket_keepalive(socket) < 0) {
    log_warn("Failed to set socket keepalive for client %u: %s", atomic_load(&client->client_id),
             network_error_string());
  }

  // Set socket buffer sizes for large data transmission
  int send_buffer_size = 1024 * 1024; // 1MB send buffer
  int recv_buffer_size = 1024 * 1024; // 1MB receive buffer

  if (socket_setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
    log_warn("Failed to set send buffer size for client %u: %s", atomic_load(&client->client_id),
             network_error_string());
  }

  if (socket_setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
    log_warn("Failed to set receive buffer size for client %u: %s", atomic_load(&client->client_id),
             network_error_string());
  }

  // Enable TCP_NODELAY to reduce latency for large packets
  int nodelay = 1;
  if (socket_setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    log_warn("Failed to set TCP_NODELAY for client %u: %s", atomic_load(&client->client_id), network_error_string());
  }

  safe_snprintf(client->display_name, sizeof(client->display_name), "Client%u", atomic_load(&client->client_id));

  // Create individual video buffer for this client using modern double-buffering
  client->incoming_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  if (!client->incoming_video_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create video buffer for client %u", atomic_load(&client->client_id));
    log_error("Failed to create video buffer for client %u", atomic_load(&client->client_id));
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create();
  if (!client->incoming_audio_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for client %u", atomic_load(&client->client_id));
    log_error("Failed to create audio buffer for client %u", atomic_load(&client->client_id));
    video_frame_buffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create packet queues for outgoing data
  // Use node pools but share the global buffer pool
  client->audio_queue =
      packet_queue_create_with_pools(100, 200, false); // Max 100 audio packets, 200 nodes, NO local buffer pool
  if (!client->audio_queue) {
    LOG_ERRNO_IF_SET("Failed to create audio queue for client");
    video_frame_buffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create outgoing video buffer for ASCII frames (double buffered, no dropping)
  client->outgoing_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  if (!client->outgoing_video_buffer) {
    LOG_ERRNO_IF_SET("Failed to create outgoing video buffer for client");
    video_frame_buffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    packet_queue_destroy(client->audio_queue);
    client->incoming_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    client->audio_queue = NULL;
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  // Pre-allocate send buffer to avoid malloc/free in send thread (prevents deadlocks)
  client->send_buffer_size = 2 * 1024 * 1024; // 2MB should handle largest frames
  client->send_buffer = SAFE_MALLOC(client->send_buffer_size, void *);
  if (!client->send_buffer) {
    log_error("Failed to allocate send buffer for client %u", atomic_load(&client->client_id));
    video_frame_buffer_destroy(client->incoming_video_buffer);
    video_frame_buffer_destroy(client->outgoing_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    packet_queue_destroy(client->audio_queue);
    client->incoming_video_buffer = NULL;
    client->outgoing_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    client->audio_queue = NULL;
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  g_client_manager.client_count = existing_count + 1; // We just added a client
  log_info("CLIENT COUNT UPDATED: now %d clients (added client_id=%u to slot %d)", g_client_manager.client_count,
           atomic_load(&client->client_id), slot);

  // Add client to hash table for O(1) lookup
  if (!hashtable_insert(g_client_manager.client_hashtable, atomic_load(&client->client_id), client)) {
    log_error("Failed to add client %u to hash table", atomic_load(&client->client_id));
    // Continue anyway - hash table is optimization, not critical
  }

  // Register this client's audio buffer with the mixer
  if (g_audio_mixer && client->incoming_audio_buffer) {
    if (mixer_add_source(g_audio_mixer, atomic_load(&client->client_id), client->incoming_audio_buffer) < 0) {
      log_warn("Failed to add client %u to audio mixer", atomic_load(&client->client_id));
    } else {
#ifdef DEBUG_AUDIO
      log_debug("Added client %u to audio mixer", atomic_load(&client->client_id));
#endif
    }
  }

  // Initialize mutexes BEFORE creating any threads to prevent race conditions
  // These mutexes might be accessed by receive thread which starts before render threads
  if (mutex_init(&client->client_state_mutex) != 0) {
    log_error("Failed to initialize client state mutex for client %u", atomic_load(&client->client_id));
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  if (rwlock_init(&client->video_buffer_rwlock) != 0) {
    log_error("Failed to initialize video buffer rwlock for client %u", atomic_load(&client->client_id));
    mutex_destroy(&client->client_state_mutex);
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  rwlock_wrunlock(&g_client_manager_rwlock);

  // CRITICAL: Perform crypto handshake BEFORE starting threads
  // This ensures the handshake uses the socket directly without interference from receive thread
  if (server_crypto_init() == 0) {
    // Set timeout for crypto handshake to prevent indefinite blocking
    // This prevents clients from connecting but never completing the handshake
    const int HANDSHAKE_TIMEOUT_SECONDS = 30;
    if (set_socket_timeout(socket, HANDSHAKE_TIMEOUT_SECONDS) < 0) {
      log_warn("Failed to set handshake timeout for client %u: %s", atomic_load(&client->client_id),
               network_error_string());
      // Continue anyway - timeout is a safety feature, not critical
    }

    int crypto_result = server_crypto_handshake(client);
    if (crypto_result != 0) {
      log_error("Crypto handshake failed for client %u: %s", atomic_load(&client->client_id), network_error_string());
      (void)remove_client(atomic_load(&client->client_id));
      return -1;
    }

    // Clear socket timeout after handshake completes successfully
    // This allows normal operation without timeouts on data transfer
    if (set_socket_timeout(socket, 0) < 0) {
      log_warn("Failed to clear handshake timeout for client %u: %s", atomic_load(&client->client_id),
               network_error_string());
      // Continue anyway - we can still communicate even with timeout set
    }

    log_info("Crypto handshake completed successfully for client %u", atomic_load(&client->client_id));

    // CRITICAL FIX: After handshake completes, the client immediately sends PACKET_TYPE_CLIENT_CAPABILITIES
    // We must read and process this packet BEFORE starting the receive thread to avoid a race condition
    // where the packet arrives but no thread is listening for it.
    log_debug("Waiting for initial capabilities packet from client %u", atomic_load(&client->client_id));

    // Protect crypto context access with client state mutex
    mutex_lock(&client->client_state_mutex);
    const crypto_context_t *crypto_ctx = crypto_server_get_context(atomic_load(&client->client_id));
    packet_envelope_t envelope;
    packet_recv_result_t result = receive_packet_secure(socket, (void *)crypto_ctx, !opt_no_encrypt, &envelope);
    mutex_unlock(&client->client_state_mutex);

    if (result != PACKET_RECV_SUCCESS) {
      log_error("Failed to receive initial capabilities packet from client %u: result=%d",
                atomic_load(&client->client_id), result);
      if (envelope.allocated_buffer) {
        buffer_pool_free(envelope.allocated_buffer, envelope.allocated_size);
      }
      (void)remove_client(atomic_load(&client->client_id));
      return -1;
    }

    if (envelope.type != PACKET_TYPE_CLIENT_CAPABILITIES) {
      log_error("Expected PACKET_TYPE_CLIENT_CAPABILITIES but got packet type %d from client %u", envelope.type,
                atomic_load(&client->client_id));
      if (envelope.allocated_buffer) {
        buffer_pool_free(envelope.allocated_buffer, envelope.allocated_size);
      }
      (void)remove_client(atomic_load(&client->client_id));
      return -1;
    }

    // Process the capabilities packet directly
    log_debug("Processing initial capabilities packet from client %u", atomic_load(&client->client_id));
    handle_client_capabilities_packet(client, envelope.data, envelope.len);

    // Free the packet data
    if (envelope.allocated_buffer) {
      buffer_pool_free(envelope.allocated_buffer, envelope.allocated_size);
    }
    log_info("Successfully received and processed initial capabilities for client %u", atomic_load(&client->client_id));
  }

  // Start threads for this client (AFTER crypto handshake AND initial capabilities)
  if (ascii_thread_create(&client->receive_thread, client_receive_thread, client) != 0) {
    LOG_ERRNO_IF_SET("Client receive thread creation failed");
    // Don't destroy mutexes here - remove_client() will handle it
    (void)remove_client(atomic_load(&client->client_id));
    return -1;
  }

  // Start send thread for this client
  if (ascii_thread_create(&client->send_thread, client_send_thread_func, client) != 0) {
    LOG_ERRNO_IF_SET("Client send thread creation failed");
    // Join the receive thread before cleaning up to prevent race conditions
    ascii_thread_join(&client->receive_thread, NULL);
    // Now safe to remove client (won't double-free since first thread creation succeeded)
    (void)remove_client(atomic_load(&client->client_id));
    return -1;
  }

  // Queue initial server state to the new client
  server_state_packet_t state;
  state.connected_client_count = g_client_manager.client_count;
  state.active_client_count = 0; // Will be updated by broadcast thread
  memset(state.reserved, 0, sizeof(state.reserved));

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = htonl(state.connected_client_count);
  net_state.active_client_count = htonl(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // TODO: Send initial server state directly via socket
  (void)net_state; // Suppress unused variable warning
#ifdef DEBUG_NETWORK
  log_info("Queued initial server state for client %u: %u connected clients", atomic_load(&client->client_id),
           state.connected_client_count);
#endif

  // NEW: Create per-client rendering threads
  log_debug("Creating render threads for client %u", client->client_id);
  if (create_client_render_threads(client) != 0) {
    log_error("Failed to create render threads for client %u", client->client_id);
    (void)remove_client(client->client_id);
    return -1;
  }
  log_debug("Successfully created render threads for client %u", client->client_id);

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return client->client_id;
}

int remove_client(uint32_t client_id) {
  // Phase 1: Mark client inactive and prepare for cleanup while holding write lock
  client_info_t *target_client = NULL;
  char display_name_copy[MAX_DISPLAY_NAME_LEN];

  log_debug("SOCKET_DEBUG: Attempting to remove client %d", client_id);
  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->client_id == client_id && client->client_id != 0) {
      // Mark as shutting down and inactive immediately to stop new operations
      log_debug("SOCKET_DEBUG: Found client %d to remove, socket=%d", client_id, client->socket);
      atomic_store(&client->shutting_down, true);
      atomic_store(&client->active, false);
      target_client = client;

      // Store display name before clearing
      SAFE_STRNCPY(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

      // Shutdown socket to unblock I/O operations, then close
      mutex_lock(&client->client_state_mutex);
      if (client->socket != INVALID_SOCKET_VALUE) {
        log_debug("SOCKET_DEBUG: Client %d closing socket %d", client->client_id, client->socket);
        // Shutdown both send and receive operations to unblock any pending I/O
        socket_shutdown(client->socket, 2); // 2 = SHUT_RDWR on POSIX, SD_BOTH on Windows
        socket_close(client->socket);
        client->socket = INVALID_SOCKET_VALUE;
        log_debug("SOCKET_DEBUG: Client %d socket set to INVALID", client->client_id);
      }
      mutex_unlock(&client->client_state_mutex);

      // Shutdown packet queues to unblock send thread
      if (client->audio_queue) {
        packet_queue_shutdown(client->audio_queue);
      }
      // Video now uses double buffer, no queue to shutdown

      break;
    }
  }

  // If client not found, unlock and return
  if (!target_client) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    log_warn("Cannot remove client %u: not found", client_id);
    return -1;
  }

  // CRITICAL: Release write lock before joining threads
  // This prevents deadlock with render threads that need read locks
  rwlock_wrunlock(&g_client_manager_rwlock);

  // Phase 2: Join threads without holding any locks

  // Wait for send thread to exit
  if (ascii_thread_is_initialized(&target_client->send_thread)) {
    bool is_shutting_down = atomic_load(&g_server_should_exit);
    int join_result;

    if (is_shutting_down) {
      join_result = ascii_thread_join_timeout(&target_client->send_thread, NULL, 100);
      if (join_result == -2) {
        log_warn("Send thread for client %u timed out during shutdown (continuing)", client_id);
        // Clear thread handle using platform abstraction
        ascii_thread_init(&target_client->send_thread);
      }
    } else {
      join_result = ascii_thread_join(&target_client->send_thread, NULL);
      if (join_result != 0) {
        log_warn("Failed to join send thread for client %u: %d", client_id, join_result);
      }
    }
  }

  // Receive thread is already joined by main.c
  log_debug("Receive thread for client %u was already joined by main thread", client_id);

  // Stop render threads (this joins them)
  stop_client_render_threads(target_client);

  // Phase 3: Clean up resources with write lock
  rwlock_wrlock(&g_client_manager_rwlock);

  // Use the dedicated cleanup functions to ensure all resources are freed
  cleanup_client_media_buffers(target_client);
  cleanup_client_packet_queues(target_client);

  // Remove from audio mixer
  if (g_audio_mixer) {
    mixer_remove_source(g_audio_mixer, client_id);
#ifdef DEBUG_AUDIO
    log_debug("Removed client %u from audio mixer", client_id);
#endif
  }

  // Remove from hash table
  if (!hashtable_remove(g_client_manager.client_hashtable, client_id)) {
    log_warn("Failed to remove client %u from hash table", client_id);
  }

  // Cleanup crypto context for this client
  if (target_client->crypto_initialized) {
    crypto_handshake_cleanup(&target_client->crypto_handshake_ctx);
    target_client->crypto_initialized = false;
    log_debug("Crypto context cleaned up for client %u", client_id);
  }

  // Destroy mutexes and rwlocks
  // IMPORTANT: Always destroy these even if threads didn't join properly
  // to prevent issues when the slot is reused
  rwlock_destroy(&target_client->video_buffer_rwlock);
  mutex_destroy(&target_client->client_state_mutex);

  // CRITICAL: Reset client_id to 0 BEFORE clearing the structure to prevent race conditions
  // This ensures that if a new client connects while cleanup is happening,
  // it won't get assigned to a slot that's still being cleaned up
  atomic_store(&target_client->client_id, 0);

  // Small delay to ensure all threads have seen the client_id reset
  // This prevents race conditions where new clients might get assigned
  // to slots that are still being cleaned up
  usleep(1000); // 1ms delay

  // Clear client structure
  // NOTE: After memset, the mutex handles are zeroed but the OS resources
  // have been released by the destroy calls above
  memset(target_client, 0, sizeof(client_info_t));

  // Recalculate client count
  int remaining_count = 0;
  for (int j = 0; j < MAX_CLIENTS; j++) {
    if (g_client_manager.clients[j].client_id != 0) {
      remaining_count++;
    }
  }
  g_client_manager.client_count = remaining_count;

  log_info("CLIENT REMOVED: client_id=%u (%s) removed, remaining clients: %d", client_id, display_name_copy,
           remaining_count);

  rwlock_wrunlock(&g_client_manager_rwlock);

  // Broadcast updated state
  broadcast_server_state_to_all_clients();

  return 0;
}

/* ============================================================================
 * Client Thread Functions
 * ============================================================================
 */

void *client_receive_thread(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in receive thread");
    return NULL;
  }

  // Enable thread cancellation for clean shutdown
  // Thread cancellation not available in platform abstraction
  // Threads should exit when g_server_should_exit is set

  log_info("Started receive thread for client %u (%s)", atomic_load(&client->client_id), client->display_name);

  while (!atomic_load(&g_server_should_exit) && atomic_load(&client->active) &&
         client->socket != INVALID_SOCKET_VALUE) {

    // Use unified secure packet reception with auto-decryption
    const crypto_context_t *crypto_ctx = NULL;
    if (crypto_server_is_ready(client->client_id)) {
      // Protect crypto context access with client state mutex
      mutex_lock(&client->client_state_mutex);
      crypto_ctx = crypto_server_get_context(client->client_id);
      mutex_unlock(&client->client_state_mutex);
    }
    packet_envelope_t envelope;

    // Use shorter timeout for faster packet processing
    // Protect socket access to prevent race conditions
    mutex_lock(&client->client_state_mutex);
    socket_t socket = client->socket;
    mutex_unlock(&client->client_state_mutex);

    if (socket == INVALID_SOCKET_VALUE) {
      log_warn("SOCKET_DEBUG: Client %d socket is INVALID, client may be disconnecting", client->client_id);
      break;
    }

    packet_recv_result_t result = receive_packet_secure(socket, (void *)crypto_ctx, !opt_no_encrypt, &envelope);

    // Check if socket became invalid during the receive operation
    if (result == PACKET_RECV_ERROR && (errno == EIO || errno == EBADF)) {
      // Socket was closed by another thread, check if client is being removed
      mutex_lock(&client->client_state_mutex);
      bool socket_invalid = (client->socket == INVALID_SOCKET_VALUE);
      mutex_unlock(&client->client_state_mutex);
      if (socket_invalid) {
        log_warn("SOCKET_DEBUG: Client %d socket was closed by another thread (errno=%d)", client->client_id, errno);
        break;
      }
    }

    // Check if shutdown was requested during the network call
    if (atomic_load(&g_server_should_exit)) {
      // Free any data that might have been allocated
      if (envelope.allocated_buffer) {
        buffer_pool_free(envelope.allocated_buffer, envelope.allocated_size);
      }
      break;
    }

    // Handle different result codes
    if (result == PACKET_RECV_EOF) {
      log_info("DISCONNECT: Client %u disconnected (clean close)", client->client_id);
      break;
    }

    if (result == PACKET_RECV_ERROR) {
      // Check if this is a timeout error
      if (errno == ETIMEDOUT) {
        log_debug("Client %u receive timeout (normal behavior)", client->client_id);
        // Timeout is normal - just continue the loop
        continue;
      }
      log_error("DISCONNECT: Error receiving from client %u: %s", client->client_id, SAFE_STRERROR(errno));
      break;
    }

    if (result == PACKET_RECV_SECURITY_VIOLATION) {
      log_error("SECURITY: Client %u violated encryption policy - terminating server", client->client_id);
      // Exit the server as a security measure
      atomic_store(&g_server_should_exit, true);
      break;
    }

    // Extract packet details from envelope
    packet_type_t type = envelope.type;
    void *data = envelope.data;
    size_t len = envelope.len;
    // Note: sender_id removed - client_id already validated by crypto handshake

    // Handle different packet types from client
    // NOTE: PACKET_TYPE_ENCRYPTED is now handled automatically by receive_packet_secure()
    switch (type) {
    case PACKET_TYPE_CLIENT_JOIN:
    case PACKET_TYPE_STREAM_START:
    case PACKET_TYPE_STREAM_STOP:
    case PACKET_TYPE_IMAGE_FRAME:
    case PACKET_TYPE_AUDIO:
    case PACKET_TYPE_AUDIO_BATCH:
    case PACKET_TYPE_CLIENT_CAPABILITIES:
    case PACKET_TYPE_PING:
    case PACKET_TYPE_PONG:
      // Process all packet types using the unified function
      process_decrypted_packet(client, type, data, len);
      break;

    default:
      log_debug("Received unhandled packet type %d from client %u", type, client->client_id);
      break;
    }

    // Free the allocated buffer (not the data pointer which may be offset into it)
    if (envelope.allocated_buffer) {
      buffer_pool_free(envelope.allocated_buffer, envelope.allocated_size);
    }
  }

  // Mark client as inactive and stop all threads
  // CRITICAL: Must stop render threads when client disconnects
  // OPTIMIZED: Use atomic operations for thread control flags (lock-free)
  atomic_store(&client->active, false);
  atomic_store(&client->send_thread_running, false);
  atomic_store(&client->video_render_thread_running, false);
  atomic_store(&client->audio_render_thread_running, false);

  // Don't call remove_client() from the receive thread itself - this causes a deadlock
  // because main thread may be trying to join this thread via remove_client()
  // The main cleanup code will handle client removal after threads exit

  log_info("Receive thread for client %u terminated, signaled all threads to stop", client->client_id);
  return NULL;
}

// Thread function to handle sending data to a specific client
void *client_send_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in send thread");
    return NULL;
  }

  log_info("Started send thread for client %u (%s)", client->client_id, client->display_name);

  // Mark thread as running
  atomic_store(&client->send_thread_running, true);

  // Track timing for video frame sends
  uint64_t last_video_send_time = 0;
  const uint64_t video_send_interval_us = 16666; // 60fps = ~16.67ms

  while (!atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down) && atomic_load(&client->active) &&
         atomic_load(&client->send_thread_running)) {
    bool sent_something = false;

    // Try to get audio packet first (higher priority for low latency)
    queued_packet_t *audio_packet = NULL;
    if (client->audio_queue) {
      audio_packet = packet_queue_try_dequeue(client->audio_queue);
    }

    // Send audio packet if we have one
    if (audio_packet) {
      // Send header
      ssize_t sent =
          send_with_timeout(client->socket, &audio_packet->header, sizeof(audio_packet->header), SEND_TIMEOUT);
      if (sent != sizeof(audio_packet->header)) {
        if (!atomic_load(&g_server_should_exit)) {
          log_error("Failed to send audio packet header to client %u: %zd/%zu bytes", client->client_id, sent,
                    sizeof(audio_packet->header));
        }
        packet_queue_free_packet(audio_packet);
        break; // Socket error, exit thread
      }

      // Send payload
      if (audio_packet->data_len > 0 && audio_packet->data) {
        sent = send_with_timeout(client->socket, audio_packet->data, audio_packet->data_len, SEND_TIMEOUT);
        if (sent != (ssize_t)audio_packet->data_len) {
          if (!atomic_load(&g_server_should_exit)) {
            log_error("Failed to send audio packet payload to client %u: %zd/%zu bytes", client->client_id, sent,
                      audio_packet->data_len);
          }
          packet_queue_free_packet(audio_packet);
          break; // Socket error, exit thread
        }
      }

      packet_queue_free_packet(audio_packet);
      sent_something = true;
    }

    // Check if it's time to send a video frame (60fps rate limiting)
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t current_time = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
    if (current_time - last_video_send_time >= video_send_interval_us) {
      // GRID LAYOUT CHANGE: Check if render thread has buffered a frame with different source count
      // If so, send CLEAR_CONSOLE before sending the new frame
      int rendered_sources = atomic_load(&client->last_rendered_grid_sources);
      int sent_sources = atomic_load(&client->last_sent_grid_sources);

      if (rendered_sources != sent_sources && rendered_sources > 0) {
        // Grid layout changed! Send CLEAR_CONSOLE before next frame
        // Protect crypto context access with client state mutex
        mutex_lock(&client->client_state_mutex);
        const crypto_context_t *crypto_ctx = crypto_server_get_context(client->client_id);
        send_packet_secure(client->socket, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0, (crypto_context_t *)crypto_ctx);
        mutex_unlock(&client->client_state_mutex);
        log_info("Client %u: Sent CLEAR_CONSOLE (grid changed %d → %d sources)", client->client_id, sent_sources,
                 rendered_sources);
        atomic_store(&client->last_sent_grid_sources, rendered_sources);
        sent_something = true;
      }

      if (!client->outgoing_video_buffer) {
        SET_ERRNO(ERROR_INVALID_STATE, "Client %u has no outgoing video buffer", client->client_id);
        break;
      }

      // Try to get latest video frame from double buffer
      rwlock_rdlock(&client->video_buffer_rwlock);
      const video_frame_t *frame = video_frame_get_latest(client->outgoing_video_buffer);

      if (!frame || !frame->data) {
        rwlock_rdunlock(&client->video_buffer_rwlock);
        SET_ERRNO(ERROR_INVALID_STATE, "Client %u has no valid frame or frame->data: frame=%p, data=%p",
                  client->client_id, frame, frame->data);
        continue;
      }

      if (frame && frame->data && frame->size == 0) {
        // NOTE: This means the we're not ready to send ascii to the client and
        // should wait a little bit.
        log_warn_every(1000000, "Client %u has no valid frame size: size=%zu", client->client_id, frame->size);
        rwlock_rdunlock(&client->video_buffer_rwlock);
        platform_sleep_usec(1000); // 1ms sleep
        continue;
      }

      // Build ASCII frame packet
      ascii_frame_packet_t frame_header = {
          .width = htonl(atomic_load(&client->width)),
          .height = htonl(atomic_load(&client->height)),
          .original_size = htonl((uint32_t)frame->size),
          .compressed_size = htonl(0), // No compression
          .checksum = htonl(asciichat_crc32(frame->data, frame->size)),
          .flags = htonl((client->terminal_caps.color_level > TERM_COLOR_NONE) ? FRAME_FLAG_HAS_COLOR : 0)};

      size_t payload_size = sizeof(ascii_frame_packet_t) + frame->size;
      if (payload_size > client->send_buffer_size) {
        rwlock_rdunlock(&client->video_buffer_rwlock);
        SET_ERRNO(ERROR_NETWORK_SIZE, "Video frame too large for send buffer: %zu > %zu", payload_size,
                  client->send_buffer_size);
        break;
      }

      uint8_t *payload = (uint8_t *)client->send_buffer;
      memcpy(payload, &frame_header, sizeof(ascii_frame_packet_t));
      memcpy(payload + sizeof(ascii_frame_packet_t), frame->data, frame->size);

      // Use unified packet processing pipeline
      // Protect crypto context access with client state mutex
      mutex_lock(&client->client_state_mutex);
      const crypto_context_t *crypto_ctx = crypto_server_get_context(client->client_id);
      int send_result = send_packet_secure(client->socket, PACKET_TYPE_ASCII_FRAME, payload, payload_size,
                                           (crypto_context_t *)crypto_ctx);
      mutex_unlock(&client->client_state_mutex);

      if (send_result != 0) {
        rwlock_rdunlock(&client->video_buffer_rwlock);
        if (!atomic_load(&g_server_should_exit)) {
          SET_ERRNO(ERROR_NETWORK, "Failed to send video frame to client %u", client->client_id);
        }
        break;
      }

      sent_something = true;
      last_video_send_time = current_time;
      rwlock_rdunlock(&client->video_buffer_rwlock);
    }

    // If we didn't send anything, sleep briefly to prevent busy waiting
    if (!sent_something) {
      platform_sleep_usec(1000); // 1ms sleep
    }
  }

  // Mark thread as stopped
  atomic_store(&client->send_thread_running, false);
  log_info("Send thread for client %u terminated", client->client_id);
  return NULL;
}

/* ============================================================================
 * Broadcast Functions
 * ============================================================================
 */

// Broadcast server state to all connected clients
void broadcast_server_state_to_all_clients(void) {
  // Count active clients with video
  int active_video_count = 0;

  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active && atomic_load(&g_client_manager.clients[i].is_sending_video)) {
      active_video_count++;
    }
  }

  // Prepare server state packet
  server_state_packet_t state;
  state.connected_client_count = g_client_manager.client_count;
  state.active_client_count = active_video_count;
  memset(state.reserved, 0, sizeof(state.reserved));

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = htonl(state.connected_client_count);
  net_state.active_client_count = htonl(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // Send to all active clients
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->active && client->socket != INVALID_SOCKET_VALUE) {
      mutex_lock(&client->client_state_mutex);
      const crypto_context_t *crypto_ctx = crypto_server_get_context(client->client_id);
      int result = send_packet_secure(client->socket, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state),
                                      (crypto_context_t *)crypto_ctx);
      mutex_unlock(&client->client_state_mutex);

      if (result != 0) {
        log_error("Failed to send server state to client %u", client->client_id);
      } else {
        log_debug("Sent server state to client %u: %u connected, %u active", client->client_id,
                  state.connected_client_count, state.active_client_count);
      }
    }
  }
  rwlock_rdunlock(&g_client_manager_rwlock);
}

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

void stop_client_threads(client_info_t *client) {
  if (!client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client is NULL");
    return;
  }

  // Signal threads to stop
  atomic_store(&client->active, false);
  atomic_store(&client->send_thread_running, false);

  // Wait for threads to finish
  if (ascii_thread_is_initialized(&client->send_thread)) {
    ascii_thread_join(&client->send_thread, NULL);
  }
  if (ascii_thread_is_initialized(&client->receive_thread)) {
    ascii_thread_join(&client->receive_thread, NULL);
  }
}

void cleanup_client_media_buffers(client_info_t *client) {
  if (!client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client is NULL");
    return;
  }

  if (client->incoming_video_buffer) {
    video_frame_buffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
  }

  // Clean up outgoing video buffer (for ASCII frames)
  rwlock_wrlock(&client->video_buffer_rwlock);
  if (client->outgoing_video_buffer) {
    video_frame_buffer_destroy(client->outgoing_video_buffer);
    client->outgoing_video_buffer = NULL;
  }
  rwlock_wrunlock(&client->video_buffer_rwlock);

  // Clean up pre-allocated send buffer
  if (client->send_buffer) {
    SAFE_FREE(client->send_buffer);
    client->send_buffer = NULL;
    client->send_buffer_size = 0;
  }

  if (client->incoming_audio_buffer) {
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_audio_buffer = NULL;
  }
}

void cleanup_client_packet_queues(client_info_t *client) {
  if (!client)
    return;

  if (client->audio_queue) {
    packet_queue_destroy(client->audio_queue);
    client->audio_queue = NULL;
  }

  // Video now uses double buffer, cleaned up in cleanup_client_media_buffers
}

/**
 * Process an encrypted packet from a client
 *
 * @param client Client info structure
 * @param type Pointer to packet type (will be updated with decrypted type)
 * @param data Pointer to packet data (will be updated with decrypted data)
 * @param len Pointer to packet length (will be updated with decrypted length)
 * @param sender_id Pointer to sender ID (will be updated with decrypted sender ID)
 * @return 0 on success, -1 on error
 */
int process_encrypted_packet(client_info_t *client, packet_type_t *type, void **data, size_t *len,
                             uint32_t *sender_id) {
  if (!crypto_server_is_ready(client->client_id)) {
    log_error("Received encrypted packet but crypto not ready for client %u", client->client_id);
    buffer_pool_free(*data, *len);
    *data = NULL;
    return -1;
  }

  void *decrypted_data = buffer_pool_alloc(*len);
  size_t decrypted_len;
  int decrypt_result = crypto_server_decrypt_packet(client->client_id, (const uint8_t *)*data, *len,
                                                    (uint8_t *)decrypted_data, *len, &decrypted_len);

  if (decrypt_result != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to process encrypted packet from client %u (result=%d)", client->client_id,
              decrypt_result);
    buffer_pool_free(*data, *len);
    buffer_pool_free(decrypted_data, *len);
    *data = NULL;
    return -1;
  }

  // Replace encrypted data with decrypted data
  buffer_pool_free(*data, *len);

  *data = decrypted_data;
  *len = decrypted_len;

  // Now process the decrypted packet by parsing its header
  if (*len < sizeof(packet_header_t)) {
    SET_ERRNO(ERROR_CRYPTO, "Decrypted packet too small for header from client %u", client->client_id);
    buffer_pool_free(*data, *len);
    *data = NULL;
    return -1;
  }

  packet_header_t *header = (packet_header_t *)*data;
  *type = (packet_type_t)ntohs(header->type);
  *sender_id = ntohl(header->client_id);

  // Adjust data pointer to skip header
  *data = (uint8_t *)*data + sizeof(packet_header_t);
  *len -= sizeof(packet_header_t);

  return 0;
}

/**
 * Process a decrypted packet from a client
 *
 * @param client Client info structure
 * @param type Packet type
 * @param data Packet data
 * @param len Packet length
 */
void process_decrypted_packet(client_info_t *client, packet_type_t type, void *data, size_t len) {
  switch (type) {
  case PACKET_TYPE_IMAGE_FRAME:
    handle_image_frame_packet(client, data, len);
    break;

  case PACKET_TYPE_AUDIO:
    handle_audio_packet(client, data, len);
    break;

  case PACKET_TYPE_AUDIO_BATCH:
    handle_audio_batch_packet(client, data, len);
    break;

  case PACKET_TYPE_CLIENT_JOIN:
    handle_client_join_packet(client, data, len);
    break;

  case PACKET_TYPE_STREAM_START:
    handle_stream_start_packet(client, data, len);
    break;

  case PACKET_TYPE_STREAM_STOP:
    handle_stream_stop_packet(client, data, len);
    break;

  case PACKET_TYPE_CLIENT_CAPABILITIES:
    handle_client_capabilities_packet(client, data, len);
    break;

  case PACKET_TYPE_PING:
    // Respond with PONG
    if (send_pong_packet(client->socket) < 0) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send PONG response to client %u", client->client_id);
    }
    break;

  case PACKET_TYPE_PONG:
    // Client acknowledged our PING - no action needed
    break;

  default:
    SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unknown decrypted packet type: %d from client %u", type, client->client_id);
    break;
  }
}
