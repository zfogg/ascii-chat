/**
 * @file server/client.c
 * @ingroup server_client
 * @brief 👥 Per-client lifecycle manager: threading coordination, state management, and client lifecycle orchestration
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
#include "network/av.h"
#include "packet_queue.h"
#include "audio.h"
#include "mixer.h"
#include "opus_codec.h"
#include "video_frame.h"
#include "util/uthash.h"
#include "platform/abstraction.h"
#include "platform/string.h"
#include "platform/socket.h"
#include "crc32.h"
#include "util/time.h"

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

static void handle_client_error_packet(client_info_t *client, const void *data, size_t len) {
  asciichat_error_t reported_error = ASCIICHAT_OK;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result =
      packet_parse_error_message(data, len, &reported_error, message, sizeof(message), NULL);
  uint32_t client_id = client ? atomic_load(&client->client_id) : 0;

  if (parse_result != ASCIICHAT_OK) {
    log_warn("Failed to parse error packet from client %u: %s", client_id, asciichat_error_string(parse_result));
    return;
  }

  log_error("Client %u reported error %d (%s): %s", client_id, reported_error, asciichat_error_string(reported_error),
            message);
}

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
// NOLINTNEXTLINE: uthash intentionally uses unsigned overflow for hash operations
__attribute__((no_sanitize("integer"))) client_info_t *find_client_by_id(uint32_t client_id) {
  if (client_id == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client ID");
    return NULL;
  }

  // Protect uthash lookup with read lock to prevent concurrent access issues
  rwlock_rdlock(&g_client_manager_rwlock);

  client_info_t *result = NULL;
  uint32_t search_id = client_id; // uthash needs an lvalue for the key
  HASH_FIND_INT(g_client_manager.clients_by_id, &search_id, result);

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

// NOLINTNEXTLINE: uthash intentionally uses unsigned overflow for hash operations
__attribute__((no_sanitize("integer"))) int add_client(socket_t socket, const char *client_ip, int port) {
  rwlock_wrlock(&g_client_manager_rwlock);

  // Find empty slot - this is the authoritative check
  int slot = -1;
  int existing_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (slot == -1 && atomic_load(&g_client_manager.clients[i].client_id) == 0) {
      slot = i; // Take first available slot
    }
    // Count only active clients
    if (atomic_load(&g_client_manager.clients[i].client_id) != 0 && atomic_load(&g_client_manager.clients[i].active)) {
      existing_count++;
    }
  }

  // Check if we've hit the configured max-clients limit (not the array size)
  if (existing_count >= opt_max_clients) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Maximum client limit reached (%d/%d active clients)", existing_count,
              opt_max_clients);
    log_error("Maximum client limit reached (%d/%d active clients)", existing_count, opt_max_clients);

    // Send a rejection message to the client before closing
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    ssize_t send_result = send(socket, reject_msg, strlen(reject_msg), 0);
    if (send_result < 0) {
      log_warn("Failed to send rejection message to client: %s", SAFE_STRERROR(errno));
    }

    return -1;
  }

  if (slot == -1) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "No available client slots (all %d array slots are in use)", MAX_CLIENTS);
    log_error("No available client slots (all %d array slots are in use)", MAX_CLIENTS);

    // Send a rejection message to the client before closing
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    ssize_t send_result = send(socket, reject_msg, strlen(reject_msg), 0);
    if (send_result < 0) {
      log_warn("Failed to send rejection message to client: %s", SAFE_STRERROR(errno));
    }

    return -1;
  }

  // Update client_count to match actual count before adding new client
  g_client_manager.client_count = existing_count;

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];
  memset(client, 0, sizeof(client_info_t));

  client->socket = socket;
  uint32_t new_client_id = atomic_fetch_add(&g_client_manager.next_client_id, 1) + 1;
  atomic_store(&client->client_id, new_client_id);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  atomic_store(&client->active, true);
  log_info("Added new client ID=%u from %s:%d (socket=%d, slot=%d)", new_client_id, client_ip, port, socket, slot);
  atomic_store(&client->shutting_down, false);
  atomic_store(&client->last_rendered_grid_sources, 0); // Render thread updates this
  atomic_store(&client->last_sent_grid_sources, 0);     // Send thread updates this
  log_debug("Client slot assigned: client_id=%u assigned to slot %d, socket=%d", atomic_load(&client->client_id), slot,
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
  // Audio queue needs larger capacity to handle jitter and render thread lag
  // 500 packets @ 172fps = ~2.9 seconds of buffering (was 100 = 0.58s)
  client->audio_queue =
      packet_queue_create_with_pools(500, 1000, false); // Max 500 audio packets, 1000 nodes, NO local buffer pool
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
  // 64-byte cache-line alignment improves performance for large network buffers
  client->send_buffer = SAFE_MALLOC_ALIGNED(client->send_buffer_size, 64, void *);
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
  log_debug("Client count updated: now %d clients (added client_id=%u to slot %d)", g_client_manager.client_count,
            atomic_load(&client->client_id), slot);

  // Add client to uthash table for O(1) lookup
  // Note: HASH_ADD_INT uses the client_id field directly from the client structure
  uint32_t cid = atomic_load(&client->client_id);
  HASH_ADD_INT(g_client_manager.clients_by_id, client_id, client);
  log_debug("Added client %u to uthash table", cid);

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

  // Initialize send mutex to protect concurrent socket writes
  if (mutex_init(&client->send_mutex) != 0) {
    log_error("Failed to initialize send mutex for client %u", atomic_load(&client->client_id));
    // Clean up all allocated resources
    video_frame_buffer_destroy(client->incoming_video_buffer);
    video_frame_buffer_destroy(client->outgoing_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    packet_queue_destroy(client->audio_queue);
    SAFE_FREE(client->send_buffer);
    mutex_destroy(&client->client_state_mutex);
    client->incoming_video_buffer = NULL;
    client->outgoing_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    client->audio_queue = NULL;
    client->send_buffer = NULL;
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

    log_debug("Crypto handshake completed successfully for client %u", atomic_load(&client->client_id));

    // CRITICAL FIX: After handshake completes, the client immediately sends PACKET_TYPE_CLIENT_CAPABILITIES
    // We must read and process this packet BEFORE starting the receive thread to avoid a race condition
    // where the packet arrives but no thread is listening for it.
    log_debug("Waiting for initial capabilities packet from client %u", atomic_load(&client->client_id));

    // Protect crypto context access with client state mutex
    mutex_lock(&client->client_state_mutex);
    const crypto_context_t *crypto_ctx = crypto_server_get_context(atomic_load(&client->client_id));

    // FIX: Use per-client crypto state to determine enforcement
    // At this point, handshake is complete, so crypto_initialized=true and handshake is ready
    bool enforce_encryption =
        !opt_no_encrypt && client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_handshake_ctx);

    packet_envelope_t envelope;
    packet_recv_result_t result = receive_packet_secure(socket, (void *)crypto_ctx, enforce_encryption, &envelope);
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
    log_debug("Successfully received and processed initial capabilities for client %u",
              atomic_load(&client->client_id));
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

  // Send initial server state to the new client
  if (send_server_state_to_client(client) != 0) {
    log_warn("Failed to send initial server state to client %u", atomic_load(&client->client_id));
  } else {
#ifdef DEBUG_NETWORK
    log_info("Sent initial server state to client %u", atomic_load(&client->client_id));
#endif
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

  // Send initial server state directly to the new client
  const crypto_context_t *crypto_ctx = NULL;
  if (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_handshake_ctx)) {
    crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
  }

  int send_result = send_packet_secure(client->socket, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state),
                                       (crypto_context_t *)crypto_ctx);
  if (send_result != 0) {
    log_warn("Failed to send initial server state to client %u", atomic_load(&client->client_id));
  } else {
    log_debug("Sent initial server state to client %u: %u connected clients", atomic_load(&client->client_id),
              state.connected_client_count);
  }

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

// NOLINTNEXTLINE: uthash intentionally uses unsigned overflow for hash operations
__attribute__((no_sanitize("integer"))) int remove_client(uint32_t client_id) {
  // Phase 1: Mark client inactive and prepare for cleanup while holding write lock
  client_info_t *target_client = NULL;
  char display_name_copy[MAX_DISPLAY_NAME_LEN];

  log_debug("SOCKET_DEBUG: Attempting to remove client %d", client_id);
  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->client_id == client_id && client->client_id != 0) {
      // Mark as shutting down and inactive immediately to stop new operations
      log_info("Removing client %d (socket=%d) - marking inactive and clearing video flags", client_id, client->socket);
      atomic_store(&client->shutting_down, true);
      atomic_store(&client->active, false);
      atomic_store(&client->is_sending_video, false);
      atomic_store(&client->is_sending_audio, false);
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

  // Remove from uthash table
  if (target_client) {
    HASH_DELETE(hh, g_client_manager.clients_by_id, target_client);
    log_debug("Removed client %u from uthash table", client_id);
  } else {
    log_warn("Failed to remove client %u from hash table (client not found)", client_id);
  }

  // Cleanup crypto context for this client
  if (target_client->crypto_initialized) {
    crypto_handshake_cleanup(&target_client->crypto_handshake_ctx);
    target_client->crypto_initialized = false;
    log_debug("Crypto context cleaned up for client %u", client_id);
  }

  // CRITICAL: Verify all threads have actually exited before resetting client_id
  // Threads that are still starting (at RtlUserThreadStart) haven't checked client_id yet
  // We must ensure threads are fully joined before zeroing the client struct
  // Check if any threads are still running - if so, wait a bit longer
  if (ascii_thread_is_initialized(&target_client->send_thread) ||
      ascii_thread_is_initialized(&target_client->receive_thread) ||
      ascii_thread_is_initialized(&target_client->video_render_thread) ||
      ascii_thread_is_initialized(&target_client->audio_render_thread)) {
    log_warn("Client %u: Some threads still appear initialized, waiting longer before cleanup", client_id);
    // Wait longer for threads that might still be starting
    platform_sleep_usec(10000); // 10ms delay
  }

  // Only reset client_id to 0 AFTER confirming threads are joined
  // This prevents threads that are starting from accessing a zeroed client struct
  // CRITICAL: Reset client_id to 0 BEFORE destroying mutexes to prevent race conditions
  // This ensures worker threads can detect shutdown and exit BEFORE the mutex is destroyed
  // If we destroy the mutex first, threads might try to access a destroyed mutex
  atomic_store(&target_client->client_id, 0);

  // Small delay to ensure all threads have seen the client_id reset
  // This prevents race conditions where threads might access mutexes after they're destroyed
  platform_sleep_usec(1000); // 1ms delay

  // Destroy mutexes
  // IMPORTANT: Always destroy these even if threads didn't join properly
  // to prevent issues when the slot is reused
  mutex_destroy(&target_client->client_state_mutex);
  mutex_destroy(&target_client->send_mutex);

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

  log_debug("Client removed: client_id=%u (%s) removed, remaining clients: %d", client_id, display_name_copy,
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

  // CRITICAL: Validate client pointer immediately before any access
  // This prevents crashes if remove_client() has zeroed the client struct
  // while the thread was still starting at RtlUserThreadStart
  if (!client) {
    log_error("Invalid client info in receive thread (NULL pointer)");
    return NULL;
  }

  if (atomic_load(&client->protocol_disconnect_requested)) {
    log_debug("Receive thread for client %u exiting before start (protocol disconnect requested)",
              atomic_load(&client->client_id));
    return NULL;
  }

  // Check if client_id is 0 (client struct has been zeroed by remove_client)
  // This must be checked BEFORE accessing any client fields
  if (atomic_load(&client->client_id) == 0) {
    log_debug("Receive thread: client_id is 0, client struct may have been zeroed, exiting");
    return NULL;
  }

  // Additional validation: check socket is valid
  if (client->socket == INVALID_SOCKET_VALUE) {
    log_error("Invalid client socket in receive thread");
    return NULL;
  }

  // Enable thread cancellation for clean shutdown
  // Thread cancellation not available in platform abstraction
  // Threads should exit when g_server_should_exit is set

  log_debug("Started receive thread for client %u (%s)", atomic_load(&client->client_id), client->display_name);

  // DEBUG: Check loop entry conditions
  bool should_exit = atomic_load(&g_server_should_exit);
  bool is_active = atomic_load(&client->active);
  socket_t sock = client->socket;
  log_debug("RECV_THREAD_START: Client %u conditions: should_exit=%d, active=%d, socket=%d (INVALID=%d)",
            atomic_load(&client->client_id), should_exit, is_active, sock, INVALID_SOCKET_VALUE);

  while (!atomic_load(&g_server_should_exit) && atomic_load(&client->active) &&
         client->socket != INVALID_SOCKET_VALUE) {

    // Use unified secure packet reception with auto-decryption
    // CRITICAL: Check client_id is still valid before accessing client fields
    // This prevents accessing freed memory if remove_client() has zeroed the client struct
    if (atomic_load(&client->client_id) == 0) {
      log_debug("Client client_id reset, exiting receive thread");
      break;
    }

    // LOCK OPTIMIZATION: Access crypto context directly - no need for find_client_by_id() rwlock!
    // Crypto context is stable after handshake and stored in client struct
    // BUT: Must verify client is still active before accessing crypto context
    const crypto_context_t *crypto_ctx = NULL;

    // Check if crypto is ready without acquiring rwlock (optimization for receive thread)
    // CRITICAL: Check client_id before accessing crypto fields to prevent use-after-free
    uint32_t client_id_check = atomic_load(&client->client_id);
    if (client_id_check == 0) {
      log_debug("Client client_id reset during crypto check, exiting receive thread");
      break;
    }

    // CRITICAL: Check client_id AGAIN before accessing crypto fields - they may have been zeroed
    client_id_check = atomic_load(&client->client_id);
    if (client_id_check == 0) {
      log_debug("Client client_id reset before crypto check, exiting receive thread");
      break;
    }

    bool crypto_ready =
        !opt_no_encrypt && client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_handshake_ctx);

    if (crypto_ready) {
      // CRITICAL: Check client_id again before getting crypto context - prevent use-after-free
      client_id_check = atomic_load(&client->client_id);
      if (client_id_check == 0) {
        log_debug("Client client_id reset before getting crypto context, exiting receive thread");
        break;
      }
      crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
    } else {
    }
    packet_envelope_t envelope;

    // CRITICAL: Check client_id again before accessing socket - prevent use-after-free
    client_id_check = atomic_load(&client->client_id);
    if (client_id_check == 0) {
      log_debug("Client client_id reset before socket access, exiting receive thread");
      break;
    }

    // LOCK OPTIMIZATION: Socket is set once at initialization and only invalidated during shutdown
    // No mutex needed for read during normal operation
    socket_t socket = client->socket;

    if (socket == INVALID_SOCKET_VALUE) {
      log_warn("SOCKET_DEBUG: socket is INVALID, client may be disconnecting");
      break;
    }

    // FIX: Use per-client crypto_ready state instead of global opt_no_encrypt
    // This ensures encryption is only enforced AFTER this specific client completes the handshake
    packet_recv_result_t result = receive_packet_secure(socket, (void *)crypto_ctx, crypto_ready, &envelope);

    // Check if socket became invalid during the receive operation
    if (result == PACKET_RECV_ERROR && (errno == EIO || errno == EBADF)) {
      // CRITICAL: Check client_id before accessing mutex - mutex may have been destroyed
      client_id_check = atomic_load(&client->client_id);
      if (client_id_check == 0) {
        log_debug("Client client_id reset during error check, exiting receive thread");
        break;
      }

      // Socket was closed by another thread, check if client is being removed
      // NOTE: Even though we checked client_id, the mutex might still be destroyed
      // if remove_client() is destroying it. Use try-lock or check client_id again after.
      mutex_lock(&client->client_state_mutex);
      client_id_check = atomic_load(&client->client_id);
      bool socket_invalid = (client_id_check == 0) || (client->socket == INVALID_SOCKET_VALUE);
      mutex_unlock(&client->client_state_mutex);
      if (socket_invalid || client_id_check == 0) {
        log_warn("SOCKET_DEBUG: Client %d socket was closed by another thread (errno=%d)", client_id_check, errno);
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
      log_debug("Client %u disconnected (clean close)", client->client_id);
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
    case PACKET_TYPE_AUDIO_OPUS_BATCH:
    case PACKET_TYPE_CLIENT_CAPABILITIES:
    case PACKET_TYPE_PING:
    case PACKET_TYPE_PONG:
    case PACKET_TYPE_REMOTE_LOG:
      // Process all packet types using the unified function
      process_decrypted_packet(client, type, data, len);
      break;

    case PACKET_TYPE_ERROR_MESSAGE:
      handle_client_error_packet(client, data, len);
      break;

    // Session rekeying packets
    case PACKET_TYPE_CRYPTO_REKEY_REQUEST: {
      log_debug("Received REKEY_REQUEST from client %u", client->client_id);

      // Process the client's rekey request
      mutex_lock(&client->client_state_mutex);
      asciichat_error_t crypto_result =
          crypto_handshake_process_rekey_request(&client->crypto_handshake_ctx, data, len);
      mutex_unlock(&client->client_state_mutex);

      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to process REKEY_REQUEST from client %u: %d", client->client_id, crypto_result);
        break;
      }

      // Send REKEY_RESPONSE
      mutex_lock(&client->client_state_mutex);
      // CRITICAL: Also protect socket write with send_mutex (follows lock ordering)
      mutex_lock(&client->send_mutex);
      crypto_result = crypto_handshake_rekey_response(&client->crypto_handshake_ctx, client->socket);
      mutex_unlock(&client->send_mutex);
      mutex_unlock(&client->client_state_mutex);

      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to send REKEY_RESPONSE to client %u: %d", client->client_id, crypto_result);
      } else {
        log_debug("Sent REKEY_RESPONSE to client %u", client->client_id);
      }
      break;
    }

    case PACKET_TYPE_CRYPTO_REKEY_RESPONSE: {
      log_debug("Received REKEY_RESPONSE from client %u", client->client_id);

      // Process the client's rekey response
      mutex_lock(&client->client_state_mutex);
      asciichat_error_t crypto_result =
          crypto_handshake_process_rekey_response(&client->crypto_handshake_ctx, data, len);
      mutex_unlock(&client->client_state_mutex);

      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to process REKEY_RESPONSE from client %u: %d", client->client_id, crypto_result);
        break;
      }

      // Send REKEY_COMPLETE to confirm and activate new key
      mutex_lock(&client->client_state_mutex);
      // CRITICAL: Also protect socket write with send_mutex (follows lock ordering)
      mutex_lock(&client->send_mutex);
      crypto_result = crypto_handshake_rekey_complete(&client->crypto_handshake_ctx, client->socket);
      mutex_unlock(&client->send_mutex);
      mutex_unlock(&client->client_state_mutex);

      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to send REKEY_COMPLETE to client %u: %d", client->client_id, crypto_result);
      } else {
        log_debug("Sent REKEY_COMPLETE to client %u - session rekeying complete", client->client_id);
      }
      break;
    }

    case PACKET_TYPE_CRYPTO_REKEY_COMPLETE: {
      log_debug("Received REKEY_COMPLETE from client %u", client->client_id);

      // Process and commit to new key
      mutex_lock(&client->client_state_mutex);
      asciichat_error_t crypto_result =
          crypto_handshake_process_rekey_complete(&client->crypto_handshake_ctx, data, len);
      mutex_unlock(&client->client_state_mutex);

      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to process REKEY_COMPLETE from client %u: %d", client->client_id, crypto_result);
      } else {
        log_debug("Session rekeying completed successfully with client %u", client->client_id);
      }
      break;
    }

    default:
      log_warn("Received unhandled packet type %d from client %u", type, client->client_id);
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

  log_debug("Receive thread for client %u terminated, signaled all threads to stop", client->client_id);
  return NULL;
}

// Thread function to handle sending data to a specific client
void *client_send_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;

  // CRITICAL: Validate client pointer immediately before any access
  // This prevents crashes if remove_client() has zeroed the client struct
  // while the thread was still starting at RtlUserThreadStart
  if (!client) {
    log_error("Invalid client info in send thread (NULL pointer)");
    return NULL;
  }

  // Check if client_id is 0 (client struct has been zeroed by remove_client)
  // This must be checked BEFORE accessing any client fields
  if (atomic_load(&client->client_id) == 0) {
    log_debug("Send thread: client_id is 0, client struct may have been zeroed, exiting");
    return NULL;
  }

  // Additional validation: check socket is valid
  if (client->socket == INVALID_SOCKET_VALUE) {
    log_error("Invalid client socket in send thread");
    return NULL;
  }

  log_debug("Started send thread for client %u (%s)", client->client_id, client->display_name);

  // Mark thread as running
  atomic_store(&client->send_thread_running, true);

  // Track timing for video frame sends
  uint64_t last_video_send_time = 0;
  const uint64_t video_send_interval_us = 16666; // 60fps = ~16.67ms

  // High-frequency audio loop - separate from video frame loop
  // to ensure audio packets are sent immediately, not rate-limited by video
#define MAX_AUDIO_BATCH 8
  while (!atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down) && atomic_load(&client->active) &&
         atomic_load(&client->send_thread_running)) {
    bool sent_something = false;

    // PRIORITY: Drain all queued audio packets before video
    // Audio must not be rate-limited by video frame sending (16.67ms)
    queued_packet_t *audio_packets[MAX_AUDIO_BATCH];
    int audio_packet_count = 0;

    if (client->audio_queue) {
      // Try to dequeue multiple audio packets
      for (int i = 0; i < MAX_AUDIO_BATCH; i++) {
        audio_packets[i] = packet_queue_try_dequeue(client->audio_queue);
        if (audio_packets[i]) {
          audio_packet_count++;
        } else {
          break; // No more packets available
        }
      }
    }

    // Send batched audio if we have packets
    if (audio_packet_count > 0) {
      // Get crypto context for this client
      const crypto_context_t *crypto_ctx = NULL;
      bool crypto_ready =
          !opt_no_encrypt && client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_handshake_ctx);
      if (crypto_ready) {
        crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
      }

      int result = 0;

      if (audio_packet_count == 1) {
        // Single packet - send directly for low latency
        packet_type_t pkt_type = (packet_type_t)ntohs(audio_packets[0]->header.type);

        mutex_lock(&client->send_mutex);
        result = send_packet_secure(client->socket, pkt_type, audio_packets[0]->data, audio_packets[0]->data_len,
                                    (crypto_context_t *)crypto_ctx);
        mutex_unlock(&client->send_mutex);
      } else {
        // Multiple packets - batch them together
        // Calculate total size for all audio frames
        size_t total_samples = 0;
        for (int i = 0; i < audio_packet_count; i++) {
          total_samples += audio_packets[i]->data_len / sizeof(float);
        }

        // Allocate buffer for batched audio
        float *batched_audio = SAFE_MALLOC(total_samples * sizeof(float), float *);
        if (batched_audio) {
          // Copy all audio packets into batch buffer
          size_t offset = 0;
          for (int i = 0; i < audio_packet_count; i++) {
            size_t packet_samples = audio_packets[i]->data_len / sizeof(float);
            memcpy(batched_audio + offset, audio_packets[i]->data, audio_packets[i]->data_len);
            offset += packet_samples;
          }

          // Send batched audio packet
          mutex_lock(&client->send_mutex);
          result = send_audio_batch_packet(client->socket, batched_audio, (int)total_samples, audio_packet_count,
                                           (crypto_context_t *)crypto_ctx);
          mutex_unlock(&client->send_mutex);

          SAFE_FREE(batched_audio);

          log_debug_every(1000000, "Sent audio batch: %d packets (%zu samples) to client %u", audio_packet_count,
                          total_samples, client->client_id);
        } else {
          log_error("Failed to allocate buffer for audio batch");
          result = -1;
        }
      }

      // Free all audio packets
      for (int i = 0; i < audio_packet_count; i++) {
        packet_queue_free_packet(audio_packets[i]);
      }

      if (result != 0) {
        if (!atomic_load(&g_server_should_exit)) {
          log_error("Failed to send audio to client %u", client->client_id);
        }
        break; // Socket error, exit thread
      }

      sent_something = true;

      // Small sleep to let more audio packets queue (helps batching efficiency)
      if (audio_packet_count > 0) {
        platform_sleep_usec(100); // 0.1ms - minimal delay
      }
    } else {
      // No audio packets - brief sleep to avoid busy-looping, then check for other tasks
      platform_sleep_usec(1000); // 1ms - enough for audio render thread to queue more packets

      // Check if session rekeying should be triggered
      mutex_lock(&client->client_state_mutex);
      bool should_rekey = !opt_no_encrypt && client->crypto_initialized &&
                          crypto_handshake_is_ready(&client->crypto_handshake_ctx) &&
                          crypto_handshake_should_rekey(&client->crypto_handshake_ctx);
      mutex_unlock(&client->client_state_mutex);

      if (should_rekey) {
        log_debug("Rekey threshold reached for client %u, initiating session rekey", client->client_id);
        mutex_lock(&client->client_state_mutex);
        // CRITICAL: Protect socket write with send_mutex to prevent concurrent writes
        mutex_lock(&client->send_mutex);
        asciichat_error_t result = crypto_handshake_rekey_request(&client->crypto_handshake_ctx, client->socket);
        mutex_unlock(&client->send_mutex);
        mutex_unlock(&client->client_state_mutex);

        if (result != ASCIICHAT_OK) {
          log_error("Failed to send REKEY_REQUEST to client %u: %d", client->client_id, result);
        } else {
          log_debug("Sent REKEY_REQUEST to client %u", client->client_id);
        }
      }
    }

    // Always consume frames from the buffer to prevent accumulation
    // Rate-limit the actual sending, but always mark frames as consumed
    if (!client->outgoing_video_buffer) {
      // CRITICAL: Buffer has been destroyed (client is shutting down)
      // Exit cleanly instead of looping forever trying to access freed memory
      log_debug("Client %u send thread exiting: outgoing_video_buffer is NULL", client->client_id);
      break;
    }

    // Get latest frame from double buffer (lock-free operation)
    // This marks the frame as consumed even if we don't send it yet
    const video_frame_t *frame = video_frame_get_latest(client->outgoing_video_buffer);

    // Check if get_latest failed (buffer might have been destroyed)
    if (!frame) {
      log_debug("Client %u send thread: video_frame_get_latest returned NULL, buffer may be destroyed",
                client->client_id);
      break; // Exit thread if buffer is invalid
    }

    // Check if it's time to send a video frame (60fps rate limiting)
    // Only rate-limit the SEND operation, not frame consumption
    struct timespec now_ts, frame_start, frame_end, step1, step2, step3, step4, step5;
    (void)clock_gettime(CLOCK_MONOTONIC, &now_ts);
    uint64_t current_time = (uint64_t)now_ts.tv_sec * 1000000 + (uint64_t)now_ts.tv_nsec / 1000;
    if (current_time - last_video_send_time >= video_send_interval_us) {
      (void)clock_gettime(CLOCK_MONOTONIC, &frame_start);

      // GRID LAYOUT CHANGE: Check if render thread has buffered a frame with different source count
      // If so, send CLEAR_CONSOLE before sending the new frame
      int rendered_sources = atomic_load(&client->last_rendered_grid_sources);
      int sent_sources = atomic_load(&client->last_sent_grid_sources);

      if (rendered_sources != sent_sources && rendered_sources > 0) {
        // Grid layout changed! Send CLEAR_CONSOLE before next frame
        // LOCK OPTIMIZATION: Access crypto context directly - no need for find_client_by_id() rwlock!
        // Crypto context is stable after handshake and stored in client struct
        const crypto_context_t *crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
        // CRITICAL: Protect socket write with send_mutex to prevent concurrent writes
        mutex_lock(&client->send_mutex);
        send_packet_secure(client->socket, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0, (crypto_context_t *)crypto_ctx);
        mutex_unlock(&client->send_mutex);
        log_debug("Client %u: Sent CLEAR_CONSOLE (grid changed %d → %d sources)", client->client_id, sent_sources,
                  rendered_sources);
        atomic_store(&client->last_sent_grid_sources, rendered_sources);
        sent_something = true;
      }

      if (!frame->data) {
        SET_ERRNO(ERROR_INVALID_STATE, "Client %u has no valid frame data: frame=%p, data=%p", client->client_id, frame,
                  frame->data);
        continue;
      }

      if (frame->data && frame->size == 0) {
        // NOTE: This means the we're not ready to send ascii to the client and
        // should wait a little bit.
        log_warn_every(1000000, "Client %u has no valid frame size: size=%zu", client->client_id, frame->size);
        platform_sleep_usec(1000); // 1ms sleep
        continue;
      }

      // Snapshot frame metadata (safe with double-buffer system)
      size_t frame_size = frame->size;
      const void *frame_data = frame->data; // Pointer snapshot - data is stable in front buffer
      (void)clock_gettime(CLOCK_MONOTONIC, &step1);

      // Validate buffer size after releasing lock (fast check)
      size_t payload_size = sizeof(ascii_frame_packet_t) + frame_size;
      if (payload_size > client->send_buffer_size) {
        SET_ERRNO(ERROR_NETWORK_SIZE, "Video frame too large for send buffer: %zu > %zu", payload_size,
                  client->send_buffer_size);
        break;
      }

      // Copy frame data to send buffer WITHOUT holding lock (safe with double-buffer)
      uint8_t *payload = (uint8_t *)client->send_buffer;
      memcpy(payload + sizeof(ascii_frame_packet_t), frame_data, frame_size);
      (void)clock_gettime(CLOCK_MONOTONIC, &step2);

      // Calculate CRC32 on the copied data (NOT while holding lock!)
      // This was the bottleneck - CRC32 takes 12-18ms and was blocking render thread
      uint32_t frame_checksum = asciichat_crc32(payload + sizeof(ascii_frame_packet_t), frame_size);
      (void)clock_gettime(CLOCK_MONOTONIC, &step3);

      // Build ASCII frame packet header (after lock released, with computed CRC)
      ascii_frame_packet_t frame_header = {
          .width = htonl(atomic_load(&client->width)),
          .height = htonl(atomic_load(&client->height)),
          .original_size = htonl((uint32_t)frame_size),
          .compressed_size = htonl(0), // No compression
          .checksum = htonl(frame_checksum),
          .flags = htonl((client->terminal_caps.color_level > TERM_COLOR_NONE) ? FRAME_FLAG_HAS_COLOR : 0)};

      // Copy header into payload buffer
      memcpy(payload, &frame_header, sizeof(ascii_frame_packet_t));
      (void)clock_gettime(CLOCK_MONOTONIC, &step4);

      // DEBUG: Verify CRC32 after header copy
      uint32_t verify_crc = asciichat_crc32(payload + sizeof(ascii_frame_packet_t), frame_size);
      if (verify_crc != frame_checksum) {
        log_error("SERVER BUG: CRC mismatch after header copy! calculated=0x%x, verify=0x%x", frame_checksum,
                  verify_crc);
      } else {
        static bool logged_once = false;
        if (!logged_once) {
          log_debug("SERVER: Sending frame with CRC=0x%x, size=%zu, first_bytes=%02x%02x%02x%02x", frame_checksum,
                    frame_size, payload[sizeof(ascii_frame_packet_t) + 0], payload[sizeof(ascii_frame_packet_t) + 1],
                    payload[sizeof(ascii_frame_packet_t) + 2], payload[sizeof(ascii_frame_packet_t) + 3]);
          logged_once = true;
        }
      }

      // Now perform network I/O without holding video buffer lock
      // LOCK OPTIMIZATION: Access crypto context directly - no need for find_client_by_id() rwlock!
      // Crypto context is stable after handshake and stored in client struct
      const crypto_context_t *crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);

      // CRITICAL: Protect socket write with send_mutex to prevent concurrent writes
      mutex_lock(&client->send_mutex);
      // Send packet without holding any locks (crypto_ctx is safe to use)
      int send_result = send_packet_secure(client->socket, PACKET_TYPE_ASCII_FRAME, payload, payload_size,
                                           (crypto_context_t *)crypto_ctx);
      mutex_unlock(&client->send_mutex);
      (void)clock_gettime(CLOCK_MONOTONIC, &step5);

      if (send_result != 0) {
        if (!atomic_load(&g_server_should_exit)) {
          SET_ERRNO(ERROR_NETWORK, "Failed to send video frame to client %u", client->client_id);
        }
        break;
      }

      sent_something = true;
      last_video_send_time = current_time;

      (void)clock_gettime(CLOCK_MONOTONIC, &frame_end);
      uint64_t frame_time_us = ((uint64_t)frame_end.tv_sec * 1000000 + (uint64_t)frame_end.tv_nsec / 1000) -
                               ((uint64_t)frame_start.tv_sec * 1000000 + (uint64_t)frame_start.tv_nsec / 1000);
      if (frame_time_us > 15000) { // Log if sending a frame takes > 15ms (encryption adds ~5-6ms)
        uint64_t step1_us = ((uint64_t)step1.tv_sec * 1000000 + (uint64_t)step1.tv_nsec / 1000) -
                            ((uint64_t)frame_start.tv_sec * 1000000 + (uint64_t)frame_start.tv_nsec / 1000);
        uint64_t step2_us = ((uint64_t)step2.tv_sec * 1000000 + (uint64_t)step2.tv_nsec / 1000) -
                            ((uint64_t)step1.tv_sec * 1000000 + (uint64_t)step1.tv_nsec / 1000);
        uint64_t step3_us = ((uint64_t)step3.tv_sec * 1000000 + (uint64_t)step3.tv_nsec / 1000) -
                            ((uint64_t)step2.tv_sec * 1000000 + (uint64_t)step2.tv_nsec / 1000);
        uint64_t step4_us = ((uint64_t)step4.tv_sec * 1000000 + (uint64_t)step4.tv_nsec / 1000) -
                            ((uint64_t)step3.tv_sec * 1000000 + (uint64_t)step3.tv_nsec / 1000);
        uint64_t step5_us = ((uint64_t)step5.tv_sec * 1000000 + (uint64_t)step5.tv_nsec / 1000) -
                            ((uint64_t)step4.tv_sec * 1000000 + (uint64_t)step4.tv_nsec / 1000);
        log_warn_every(
            5000000,
            "SEND_THREAD: Frame send took %.2fms for client %u | Snapshot: %.2fms | Memcpy: %.2fms | CRC32: %.2fms | "
            "Header: %.2fms | send_packet_secure: %.2fms",
            frame_time_us / 1000.0, client->client_id, step1_us / 1000.0, step2_us / 1000.0, step3_us / 1000.0,
            step4_us / 1000.0, step5_us / 1000.0);
      }
    }

    // If we didn't send anything, sleep briefly to prevent busy waiting
    if (!sent_something) {
      platform_sleep_usec(1000); // 1ms sleep
    }
  }

  // Mark thread as stopped
  atomic_store(&client->send_thread_running, false);
  log_debug("Send thread for client %u terminated", client->client_id);
  return NULL;
}

/* ============================================================================
 * Broadcast Functions
 * ============================================================================
 */

// Broadcast server state to all connected clients
void broadcast_server_state_to_all_clients(void) {
  // SNAPSHOT PATTERN: Collect client data while holding lock, then release before network I/O
  typedef struct {
    socket_t socket;
    uint32_t client_id;
    const crypto_context_t *crypto_ctx;
  } client_snapshot_t;

  client_snapshot_t client_snapshots[MAX_CLIENTS];
  int snapshot_count = 0;
  int active_video_count = 0;

  struct timespec lock_start, lock_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &lock_start);
  rwlock_rdlock(&g_client_manager_rwlock);
  (void)clock_gettime(CLOCK_MONOTONIC, &lock_end);
  uint64_t lock_time_us = ((uint64_t)lock_end.tv_sec * 1000000 + (uint64_t)lock_end.tv_nsec / 1000) -
                          ((uint64_t)lock_start.tv_sec * 1000000 + (uint64_t)lock_start.tv_nsec / 1000);
  if (lock_time_us > 1000) { // Log if > 1ms
    double lock_time_ms = lock_time_us / 1000.0;
    char duration_str[32];
    format_duration_ms(lock_time_ms, duration_str, sizeof(duration_str));
    log_warn("broadcast_server_state: rwlock_rdlock took %s", duration_str);
  }

  // Count active clients and snapshot client data while holding lock
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active && atomic_load(&g_client_manager.clients[i].is_sending_video)) {
      active_video_count++;
    }
    if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
      // Get crypto context and skip if not ready (handshake still in progress)
      const crypto_context_t *crypto_ctx =
          crypto_handshake_get_context(&g_client_manager.clients[i].crypto_handshake_ctx);
      if (!crypto_ctx) {
        // Skip clients that haven't completed crypto handshake yet
        log_debug("Skipping server_state broadcast to client %u: crypto handshake not ready",
                  g_client_manager.clients[i].client_id);
        continue;
      }

      client_snapshots[snapshot_count].socket = g_client_manager.clients[i].socket;
      client_snapshots[snapshot_count].client_id = g_client_manager.clients[i].client_id;
      client_snapshots[snapshot_count].crypto_ctx = crypto_ctx;
      snapshot_count++;
    }
  }

  // Prepare server state packet while still holding lock (fast operation)
  server_state_packet_t state;
  state.connected_client_count = g_client_manager.client_count;
  state.active_client_count = active_video_count;
  memset(state.reserved, 0, sizeof(state.reserved));

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = htonl(state.connected_client_count);
  net_state.active_client_count = htonl(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // CRITICAL FIX: Release lock BEFORE sending (snapshot pattern)
  // Sending while holding lock blocks all client operations
  (void)clock_gettime(CLOCK_MONOTONIC, &lock_end);
  uint64_t lock_held_us = ((uint64_t)lock_end.tv_sec * 1000000 + (uint64_t)lock_end.tv_nsec / 1000) -
                          ((uint64_t)lock_start.tv_sec * 1000000 + (uint64_t)lock_start.tv_nsec / 1000);
  rwlock_rdunlock(&g_client_manager_rwlock);

  // Send to all clients AFTER releasing the lock
  // This prevents blocking other threads during network I/O
  for (int i = 0; i < snapshot_count; i++) {
    log_debug("BROADCAST_DEBUG: Sending SERVER_STATE to client %u (socket %d) with crypto_ctx=%p",
              client_snapshots[i].client_id, client_snapshots[i].socket, (void *)client_snapshots[i].crypto_ctx);

    // CRITICAL: Protect socket write with per-client send_mutex
    client_info_t *target = find_client_by_id(client_snapshots[i].client_id);
    if (target) {
      // IMPORTANT: Verify client_id matches expected value - prevents use-after-free
      // if client was removed and replaced with another client in same slot
      if (atomic_load(&target->client_id) != client_snapshots[i].client_id) {
        log_warn("Client %u ID mismatch during broadcast (found %u), skipping send", client_snapshots[i].client_id,
                 atomic_load(&target->client_id));
        continue;
      }

      mutex_lock(&target->send_mutex);

      // Double-check client_id again after acquiring mutex (stronger protection)
      if (atomic_load(&target->client_id) != client_snapshots[i].client_id) {
        mutex_unlock(&target->send_mutex);
        log_warn("Client %u was removed during broadcast send (now %u), skipping", client_snapshots[i].client_id,
                 atomic_load(&target->client_id));
        continue;
      }

      int result = send_packet_secure(client_snapshots[i].socket, PACKET_TYPE_SERVER_STATE, &net_state,
                                      sizeof(net_state), (crypto_context_t *)client_snapshots[i].crypto_ctx);
      mutex_unlock(&target->send_mutex);

      if (result != 0) {
        log_error("Failed to send server state to client %u", client_snapshots[i].client_id);
      } else {
        log_debug("Sent server state to client %u: %u connected, %u active", client_snapshots[i].client_id,
                  state.connected_client_count, state.active_client_count);
      }
    } else {
      log_warn("Client %u removed before broadcast send could complete", client_snapshots[i].client_id);
    }
  }

  if (lock_held_us > 1000) { // Log if held > 1ms (should be very rare now with optimized send)
    log_warn("broadcast_server_state: rwlock held for %.2fms (includes network I/O)", lock_held_us / 1000.0);
  }
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
  if (client->outgoing_video_buffer) {
    video_frame_buffer_destroy(client->outgoing_video_buffer);
    client->outgoing_video_buffer = NULL;
  }

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

  // Clean up Opus decoder
  if (client->opus_decoder) {
    opus_codec_destroy((opus_codec_t *)client->opus_decoder);
    client->opus_decoder = NULL;
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

  case PACKET_TYPE_AUDIO_OPUS_BATCH:
    handle_audio_opus_batch_packet(client, data, len);
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
    // CRITICAL: Protect socket write with send_mutex to prevent concurrent writes
    mutex_lock(&client->send_mutex);
    // Get crypto context for encryption
    const crypto_context_t *crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
    if (send_packet_secure(client->socket, PACKET_TYPE_PONG, NULL, 0, (crypto_context_t *)crypto_ctx) < 0) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send PONG response to client %u", client->client_id);
    }
    mutex_unlock(&client->send_mutex);
    break;

  case PACKET_TYPE_PONG:
    // Client acknowledged our PING - no action needed
    break;

  case PACKET_TYPE_REMOTE_LOG:
    handle_remote_log_packet_from_client(client, data, len);
    break;

  default:
    disconnect_client_for_bad_data(client, "Unknown packet type: %d (len=%zu)", type, len);
    break;
  }
}
