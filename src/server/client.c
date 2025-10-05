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
#include "common.h"
#include "buffer_pool.h"
#include "network.h"
#include "packet_queue.h"
#include "audio.h"
#include "mixer.h"
#include "video_frame.h"
#include "hashtable.h"
#include "platform/abstraction.h"
#include "platform/string.h"
#include "platform/socket.h"
#include "crc32_hw.h"

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
extern atomic_bool g_should_exit; ///< Global shutdown flag from main.c
extern mixer_t *g_audio_mixer;    ///< Global audio mixer from main.c

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
    return NULL;
  }

  return (client_info_t *)hashtable_lookup(g_client_manager.client_hashtable, client_id);
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
    if (atomic_load(&g_client_manager.clients[i].client_id) == 0) {
      if (slot == -1) {
        slot = i; // Take first available slot
      }
    } else {
      existing_count++;
    }
  }

  if (slot == -1) {
    rwlock_wrunlock(&g_client_manager_rwlock);
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
  atomic_store(&client->client_id, ++g_client_manager.next_client_id);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  atomic_store(&client->active, true);
  atomic_store(&client->shutting_down, false);
  atomic_store(&client->needs_display_clear, true); // New clients need display clear before first frame
  log_info("CLIENT SLOT ASSIGNED: client_id=%u assigned to slot %d, socket=%d", atomic_load(&client->client_id), slot,
           socket);
  client->connected_at = time(NULL);

  // Configure socket options for optimal performance
  if (set_socket_keepalive(socket) < 0) {
    log_warn("Failed to set socket keepalive for client %u: %s", atomic_load(&client->client_id),
             network_error_string(errno));
  }

  // Set socket buffer sizes for large data transmission
  int send_buffer_size = 1024 * 1024; // 1MB send buffer
  int recv_buffer_size = 1024 * 1024; // 1MB receive buffer

  if (socket_setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
    log_warn("Failed to set send buffer size for client %u: %s", atomic_load(&client->client_id),
             network_error_string(errno));
  }

  if (socket_setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
    log_warn("Failed to set receive buffer size for client %u: %s", atomic_load(&client->client_id),
             network_error_string(errno));
  }

  // Enable TCP_NODELAY to reduce latency for large packets
  int nodelay = 1;
  if (socket_setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    log_warn("Failed to set TCP_NODELAY for client %u: %s", atomic_load(&client->client_id),
             network_error_string(errno));
  }
  SAFE_IGNORE_PRINTF_RESULT(
      safe_snprintf(client->display_name, sizeof(client->display_name), "Client%u", atomic_load(&client->client_id)));

  log_info("DEBUG: About to create incoming video buffer for client %u", atomic_load(&client->client_id));
  // Create individual video buffer for this client using modern double-buffering
  client->incoming_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  log_info("DEBUG: Created incoming video buffer for client %u", atomic_load(&client->client_id));
  if (!client->incoming_video_buffer) {
    log_error("Failed to create video buffer for client %u", atomic_load(&client->client_id));
    rwlock_wrunlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create();
  if (!client->incoming_audio_buffer) {
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
    log_error("Failed to create audio queue for client %u", atomic_load(&client->client_id));
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
    log_error("Failed to create outgoing video buffer for client %u", atomic_load(&client->client_id));
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
  SAFE_MALLOC(client->send_buffer, client->send_buffer_size, void *);
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

  // Start threads for this client
  if (ascii_thread_create(&client->receive_thread, client_receive_thread, client) != 0) {
    log_error("Failed to create receive thread for client %u", atomic_load(&client->client_id));
    // Don't destroy mutexes here - remove_client() will handle it
    (void)remove_client(atomic_load(&client->client_id));
    return -1;
  }

  // Start send thread for this client
  if (ascii_thread_create(&client->send_thread, client_send_thread_func, client) != 0) {
    log_error("Failed to create send thread for client %u", atomic_load(&client->client_id));
    // Join the receive thread before cleaning up to prevent race conditions
    ascii_thread_join(&client->receive_thread, NULL);
    // Now safe to remove client (won't double-free since first thread creation succeeded)
    (void)remove_client(client->client_id);
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
  if (create_client_render_threads(client) != 0) {
    log_error("Failed to create render threads for client %u", client->client_id);
    (void)remove_client(client->client_id);
    return -1;
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return client->client_id;
}

int remove_client(uint32_t client_id) {
  // Phase 1: Mark client inactive and prepare for cleanup while holding write lock
  client_info_t *target_client = NULL;
  char display_name_copy[MAX_DISPLAY_NAME_LEN];

  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->client_id == client_id && client->client_id != 0) {
      // Mark as shutting down and inactive immediately to stop new operations
      atomic_store(&client->shutting_down, true);
      atomic_store(&client->active, false);
      target_client = client;

      // Store display name before clearing
      SAFE_STRNCPY(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

      // Shutdown socket to unblock I/O operations, then close
      if (client->socket != INVALID_SOCKET_VALUE) {
        // Shutdown both send and receive operations to unblock any pending I/O
        socket_shutdown(client->socket, 2); // 2 = SHUT_RDWR on POSIX, SD_BOTH on Windows
        socket_close(client->socket);
        client->socket = INVALID_SOCKET_VALUE;
      }

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
    bool is_shutting_down = atomic_load(&g_should_exit);
    int join_result;

    if (is_shutting_down) {
      log_info("DEBUG_REMOVE_CLIENT: Shutdown mode: joining send thread for client %u with 100ms timeout", client_id);
      join_result = ascii_thread_join_timeout(&target_client->send_thread, NULL, 100);
      if (join_result == -2) {
        log_warn("Send thread for client %u timed out during shutdown (continuing)", client_id);
        // Clear thread handle using platform abstraction
        ascii_thread_init(&target_client->send_thread);
      }
      log_info("DEBUG_REMOVE_CLIENT: Send thread join completed for client %u", client_id);
    } else {
      log_info("DEBUG_REMOVE_CLIENT: Joining send thread for client %u", client_id);
      join_result = ascii_thread_join(&target_client->send_thread, NULL);
      if (join_result != 0) {
        log_warn("Failed to join send thread for client %u: %d", client_id, join_result);
      }
      log_info("DEBUG_REMOVE_CLIENT: Send thread join completed for client %u", client_id);
    }
  }

  // Receive thread is already joined by main.c
  log_debug("Receive thread for client %u was already joined by main thread", client_id);

  // Stop render threads (this joins them)
  log_info("DEBUG_REMOVE_CLIENT: Stopping render threads for client %u", client_id);
  stop_client_render_threads(target_client);
  log_info("DEBUG_REMOVE_CLIENT: Render threads stopped for client %u", client_id);

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

  // Destroy mutexes and rwlocks
  // IMPORTANT: Always destroy these even if threads didn't join properly
  // to prevent issues when the slot is reused
  rwlock_destroy(&target_client->video_buffer_rwlock);
  mutex_destroy(&target_client->client_state_mutex);

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
  // Threads should exit when g_should_exit is set

  log_info("Started receive thread for client %u (%s)", client->client_id, client->display_name);

  packet_type_t type;
  uint32_t sender_id;
  void *data = NULL; // Initialize to prevent static analyzer warning about uninitialized use
  size_t len;

  while (!atomic_load(&g_should_exit) && atomic_load(&client->active) && client->socket != INVALID_SOCKET_VALUE) {
    // Receive packet from this client
    int result = receive_packet_with_client(client->socket, &type, &sender_id, &data, &len);

    // Check if shutdown was requested during the network call
    if (atomic_load(&g_should_exit)) {
      // Free any data that might have been allocated
      if (data) {
        buffer_pool_free(data, len);
        data = NULL;
      }
      break;
    }

    if (result <= 0) {
      if (result == 0) {
        log_info("DISCONNECT: Client %u disconnected (clean close, result=0)", client->client_id);
      } else {
        log_error("DISCONNECT: Error receiving from client %u (result=%d): %s", client->client_id, result,
                  SAFE_STRERROR(errno));
      }
      // Free any data that might have been allocated before the error
      if (data) {
        buffer_pool_free(data, len);
        data = NULL;
      }
      // Don't just mark inactive - properly remove the client
      // This will be done after the loop exits
      break;
    }

    // Handle different packet types from client
    switch (type) {
    case PACKET_TYPE_CLIENT_JOIN:
      handle_client_join_packet(client, data, len);
      break;

    case PACKET_TYPE_STREAM_START:
      handle_stream_start_packet(client, data, len);
      break;

    case PACKET_TYPE_STREAM_STOP:
      handle_stream_stop_packet(client, data, len);
      break;

    case PACKET_TYPE_IMAGE_FRAME:
      log_debug("Received IMAGE_FRAME packet from client %u (len=%zu)", client->client_id, len);
      handle_image_frame_packet(client, data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(client, data, len);
      break;

    case PACKET_TYPE_AUDIO_BATCH:
      handle_audio_batch_packet(client, data, len);
      break;

    case PACKET_TYPE_CLIENT_CAPABILITIES:
      handle_client_capabilities_packet(client, data, len);
      break;

    case PACKET_TYPE_PING:
      handle_ping_packet(client);
      break;

    case PACKET_TYPE_PONG:
      // Handle pong from client - just log it
      log_debug("Received PONG from client %u", client->client_id);
      break;

    default:
      log_debug("Received unhandled packet type %d from client %u", type, client->client_id);
      break;
    }

    if (data) {
      buffer_pool_free(data, len);
      data = NULL;
    }
  }

  // CRITICAL: Cleanup any remaining allocated packet data if thread exited loop early during shutdown
  if (data) {
    buffer_pool_free(data, len);
    data = NULL;
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

  while (!atomic_load(&g_should_exit) && !atomic_load(&client->shutting_down) && atomic_load(&client->active) &&
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
        if (!atomic_load(&g_should_exit)) {
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
          if (!atomic_load(&g_should_exit)) {
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
      // GRID LAYOUT CHANGE: Check if we need to send CLEAR_CONSOLE before next video frame
      // This ensures the client clears its display before receiving frames with new grid layout
      if (atomic_load(&client->needs_display_clear)) {
        // Send CLEAR_CONSOLE packet directly (header only, no payload)
        packet_header_t clear_header = {
            .magic = htonl(PACKET_MAGIC),
            .type = htons(PACKET_TYPE_CLEAR_CONSOLE),
            .length = htonl(0),   // No payload
            .crc32 = htonl(0),    // No payload to checksum
            .client_id = htonl(0) // From server
        };

        ssize_t sent = send_with_timeout(client->socket, &clear_header, sizeof(clear_header), SEND_TIMEOUT);
        if (sent == sizeof(clear_header)) {
          log_info("Sent CLEAR_CONSOLE to client %u before video frame (grid layout changed)", client->client_id);
          atomic_store(&client->needs_display_clear, false); // Clear the flag
          sent_something = true;
        } else {
          if (!atomic_load(&g_should_exit)) {
            log_error("Failed to send CLEAR_CONSOLE to client %u: %zd/%zu bytes", client->client_id, sent,
                      sizeof(clear_header));
          }
          break; // Socket error - exit thread
        }
      }

      // Try to get latest video frame from double buffer
      // CRITICAL: Protect video buffer access with read lock to prevent use-after-free
      rwlock_rdlock(&client->video_buffer_rwlock);
      if (client->outgoing_video_buffer) {
        const video_frame_t *frame = video_frame_get_latest(client->outgoing_video_buffer);
        if (frame && frame->data && frame->size > 0) {
          // Build ASCII frame packet
          ascii_frame_packet_t frame_header = {
              .width = htonl(atomic_load(&client->width)),
              .height = htonl(atomic_load(&client->height)),
              .original_size = htonl((uint32_t)frame->size),
              .compressed_size = htonl(0), // No compression
              .checksum = htonl(asciichat_crc32(frame->data, frame->size)),
              .flags = htonl((client->terminal_caps.color_level > TERM_COLOR_NONE) ? FRAME_FLAG_HAS_COLOR : 0)};

          // Build packet header
          packet_header_t header = {.magic = htonl(PACKET_MAGIC),
                                    .type = htons(PACKET_TYPE_ASCII_FRAME),
                                    .length = htonl((uint32_t)(sizeof(ascii_frame_packet_t) + frame->size)),
                                    .crc32 = 0,             // Will be calculated below
                                    .client_id = htonl(0)}; // Server sends as client_id 0

          // Use pre-allocated buffer to avoid malloc/free (prevents deadlocks)
          size_t payload_size = sizeof(ascii_frame_packet_t) + frame->size;
          if (payload_size <= client->send_buffer_size) {
            uint8_t *payload = (uint8_t *)client->send_buffer;
            memcpy(payload, &frame_header, sizeof(ascii_frame_packet_t));
            memcpy(payload + sizeof(ascii_frame_packet_t), frame->data, frame->size);
            header.crc32 = htonl(asciichat_crc32(payload, payload_size));

            // Send packet header
            ssize_t sent = send_with_timeout(client->socket, &header, sizeof(header), SEND_TIMEOUT);
            if (sent == sizeof(header)) {
              // Send payload
              sent = send_with_timeout(client->socket, payload, payload_size, SEND_TIMEOUT);
              if (sent != (ssize_t)payload_size) {
                if (!atomic_load(&g_should_exit)) {
                  log_error("Failed to send video frame payload to client %u: %zd/%zu bytes", client->client_id, sent,
                            payload_size);
                }
                rwlock_rdunlock(&client->video_buffer_rwlock); // CRITICAL: Unlock before break
                break;                                         // Socket error
              }
              sent_something = true;
              last_video_send_time = current_time;
            } else {
              if (!atomic_load(&g_should_exit)) {
                log_error("Failed to send video frame header to client %u: %zd/%zu bytes", client->client_id, sent,
                          sizeof(header));
              }
              rwlock_rdunlock(&client->video_buffer_rwlock); // CRITICAL: Unlock before break
              break;                                         // Socket error
            }
          } else {
            log_warn("Video frame too large for send buffer: %zu > %zu", payload_size, client->send_buffer_size);
          }
        }
      }
      rwlock_rdunlock(&client->video_buffer_rwlock);
    }

    // If we didn't send anything, sleep briefly to prevent busy waiting
    if (!sent_something) {
      // DEBUG: Track when no frames are available to send
      static uint64_t no_frame_count = 0;
      no_frame_count++;
      if (no_frame_count % 300 == 0) { // Log every 300 occurrences (10 seconds at 30fps)
        log_info("DEBUG_NO_FRAME: [%llu] No frame available to send for client %u", no_frame_count, client->client_id);
      }
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
    // TODO: Send server state updates directly via socket
    // client_info_t *client = &g_client_manager.clients[i];
    (void)i;         // Suppress unused variable warning for now
    (void)net_state; // Suppress unused variable warning
  }
  rwlock_rdunlock(&g_client_manager_rwlock);

  log_info("Broadcast server state to all clients: %d connected, %d active with video", state.connected_client_count,
           active_video_count);
}

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

void stop_client_threads(client_info_t *client) {
  if (!client)
    return;

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
  if (!client)
    return;

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
    free(client->send_buffer);
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
