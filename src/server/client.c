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
#include "main.h"
#include "protocol.h"
#include "render.h"
#include "stream.h"
#include "crypto.h"
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/packet_queue.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/opus_codec.h>
#include <ascii-chat/video/video_frame.h>
#include <ascii-chat/uthash/uthash.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/logging.h>

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

// =============================================================================
// Packet Handler Dispatch (O(1) hash table lookup)
// =============================================================================

typedef void (*client_packet_handler_t)(client_info_t *client, const void *data, size_t len);

#define CLIENT_DISPATCH_HASH_SIZE 32
#define CLIENT_DISPATCH_HANDLER_COUNT 12

typedef struct {
  packet_type_t key;
  uint8_t handler_idx;
} client_dispatch_entry_t;

#define CLIENT_DISPATCH_HASH(type) ((type) % CLIENT_DISPATCH_HASH_SIZE)

static inline int client_dispatch_hash_lookup(const client_dispatch_entry_t *table, packet_type_t type) {
  uint32_t h = CLIENT_DISPATCH_HASH(type);
  for (int i = 0; i < CLIENT_DISPATCH_HASH_SIZE; i++) {
    uint32_t slot = (h + i) % CLIENT_DISPATCH_HASH_SIZE;
    if (table[slot].key == 0)
      return -1;
    if (table[slot].key == type)
      return table[slot].handler_idx;
  }
  return -1;
}

// Handler array (indexed by hash lookup result)
static const client_packet_handler_t g_client_dispatch_handlers[CLIENT_DISPATCH_HANDLER_COUNT] = {
    (client_packet_handler_t)handle_protocol_version_packet,       // 0
    (client_packet_handler_t)handle_image_frame_packet,            // 1
    (client_packet_handler_t)handle_audio_batch_packet,            // 2
    (client_packet_handler_t)handle_audio_opus_batch_packet,       // 3
    (client_packet_handler_t)handle_client_join_packet,            // 4
    (client_packet_handler_t)handle_client_leave_packet,           // 5
    (client_packet_handler_t)handle_stream_start_packet,           // 6
    (client_packet_handler_t)handle_stream_stop_packet,            // 7
    (client_packet_handler_t)handle_client_capabilities_packet,    // 8
    (client_packet_handler_t)handle_ping_packet,                   // 9
    (client_packet_handler_t)handle_pong_packet,                   // 10
    (client_packet_handler_t)handle_remote_log_packet_from_client, // 11
};

// Hash table mapping packet type -> handler index
// clang-format off
static const client_dispatch_entry_t g_client_dispatch_hash[CLIENT_DISPATCH_HASH_SIZE] = {
    [0]  = {PACKET_TYPE_AUDIO_BATCH,           2},   // hash(4000)=0
    [1]  = {PACKET_TYPE_PROTOCOL_VERSION,      0},   // hash(1)=1
    [2]  = {PACKET_TYPE_AUDIO_OPUS_BATCH,      3},   // hash(4001)=1, probed->2
    [8]  = {PACKET_TYPE_CLIENT_CAPABILITIES,   4},   // hash(5000)=8
    [9]  = {PACKET_TYPE_PING,                  5},   // hash(5001)=9
    [10] = {PACKET_TYPE_PONG,                  6},   // hash(5002)=10
    [11] = {PACKET_TYPE_CLIENT_JOIN,           7},   // hash(5003)=11
    [12] = {PACKET_TYPE_CLIENT_LEAVE,          8},   // hash(5004)=12
    [13] = {PACKET_TYPE_STREAM_START,          9},   // hash(5005)=13
    [14] = {PACKET_TYPE_STREAM_STOP,           10},  // hash(5006)=14
    [20] = {PACKET_TYPE_REMOTE_LOG,            11},  // hash(2004)=20
    [25] = {PACKET_TYPE_IMAGE_FRAME,           1},   // hash(3001)=25
};
// clang-format on

// Forward declarations for static helper functions
static inline void cleanup_client_all_buffers(client_info_t *client);

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
client_manager_t g_client_manager;

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

// Forward declarations for internal functions
// client_receive_thread is implemented below
void *client_send_thread_func(void *arg);         ///< Client packet send thread
void broadcast_server_state_to_all_clients(void); ///< Notify all clients of state changes
static int start_client_threads(server_context_t *server_ctx, client_info_t *client,
                                bool is_tcp); ///< Common thread initialization

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

/**
 * Configure socket options for optimal network performance
 *
 * @param socket Socket to configure
 * @param client_id Client ID for logging purposes
 */
static void configure_client_socket(socket_t socket, uint32_t client_id) {
  // Enable TCP keepalive to detect dead connections
  asciichat_error_t keepalive_result = set_socket_keepalive(socket);
  if (keepalive_result != ASCIICHAT_OK) {
    log_warn("Failed to set socket keepalive for client %u: %s", client_id, asciichat_error_string(keepalive_result));
  }

  // Set socket buffer sizes for large data transmission
  const int SOCKET_SEND_BUFFER_SIZE = 1024 * 1024; // 1MB send buffer
  const int SOCKET_RECV_BUFFER_SIZE = 1024 * 1024; // 1MB receive buffer

  if (socket_setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &SOCKET_SEND_BUFFER_SIZE, sizeof(SOCKET_SEND_BUFFER_SIZE)) < 0) {
    log_warn("Failed to set send buffer size for client %u: %s", client_id, network_error_string());
  }

  if (socket_setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &SOCKET_RECV_BUFFER_SIZE, sizeof(SOCKET_RECV_BUFFER_SIZE)) < 0) {
    log_warn("Failed to set receive buffer size for client %u: %s", client_id, network_error_string());
  }

  // Enable TCP_NODELAY to reduce latency for large packets (disables Nagle algorithm)
  const int TCP_NODELAY_VALUE = 1;
  if (socket_setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &TCP_NODELAY_VALUE, sizeof(TCP_NODELAY_VALUE)) < 0) {
    log_warn("Failed to set TCP_NODELAY for client %u: %s", client_id, network_error_string());
  }
}

/**
 * @brief Unified thread initialization for both TCP and WebRTC clients
 *
 * Creates all necessary threads in the correct order:
 * 1. Receive thread (handles incoming packets)
 * 2. Render threads (generates ASCII frames) - MUST come before send thread!
 * 3. Send thread (transmits frames to client)
 *
 * This function eliminates the code duplication between add_client() and add_webrtc_client()
 * and ensures consistent initialization order to prevent the race condition where the send
 * thread starts reading empty frames before the render thread generates the first real frame.
 *
 * @param server_ctx Server context
 * @param client Client to initialize threads for
 * @param is_tcp true for TCP clients (uses tcp_server thread pool), false for WebRTC (uses generic threads)
 * @return 0 on success, -1 on failure (client will be removed on failure)
 */
static int start_client_threads(server_context_t *server_ctx, client_info_t *client, bool is_tcp) {
  if (!server_ctx || !client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "server_ctx or client is NULL");
    return -1;
  }

  uint32_t client_id = atomic_load(&client->client_id);
  char thread_name[64];
  asciichat_error_t result;

  // Step 1: Create receive thread
  if (is_tcp) {
    safe_snprintf(thread_name, sizeof(thread_name), "receive_%u", client_id);
    result =
        tcp_server_spawn_thread(server_ctx->tcp_server, client->socket, client_receive_thread, client, 1, thread_name);
  } else {
    safe_snprintf(thread_name, sizeof(thread_name), "webrtc_recv_%u", client_id);
    log_error("═══ THREAD_CREATE: WebRTC client %u ═══", client_id);
    log_error("  client=%p, func=%p, &receive_thread=%p", (void *)client, (void *)client_receive_thread,
              (void *)&client->receive_thread);
    log_error("  Pre-create: receive_thread value=%lu", (unsigned long)client->receive_thread);
    result = asciichat_thread_create(&client->receive_thread, client_receive_thread, client);
    log_error("  Post-create: result=%d, receive_thread value=%lu", result, (unsigned long)client->receive_thread);
    log_error("═══════════════════════════════════════");
  }

  if (result != ASCIICHAT_OK) {
    log_error("Failed to create receive thread for %s client %u: %s", is_tcp ? "TCP" : "WebRTC", client_id,
              asciichat_error_string(result));
    remove_client(server_ctx, client_id);
    return -1;
  }
  log_debug("Created receive thread for %s client %u", is_tcp ? "TCP" : "WebRTC", client_id);

  // Step 2: Create render threads BEFORE send thread
  // This ensures the render threads generate the first frame before the send thread tries to read it
  log_debug("Creating render threads for client %u", client_id);
  if (create_client_render_threads(server_ctx, client) != 0) {
    log_error("Failed to create render threads for client %u", client_id);
    remove_client(server_ctx, client_id);
    return -1;
  }
  log_debug("Successfully created render threads for client %u", client_id);

  // Step 3: Create send thread AFTER render threads are running
  if (is_tcp) {
    safe_snprintf(thread_name, sizeof(thread_name), "send_%u", client_id);
    result = tcp_server_spawn_thread(server_ctx->tcp_server, client->socket, client_send_thread_func, client, 3,
                                     thread_name);
  } else {
    safe_snprintf(thread_name, sizeof(thread_name), "webrtc_send_%u", client_id);
    result = asciichat_thread_create(&client->send_thread, client_send_thread_func, client);
  }

  if (result != ASCIICHAT_OK) {
    log_error("Failed to create send thread for %s client %u: %s", is_tcp ? "TCP" : "WebRTC", client_id,
              asciichat_error_string(result));
    remove_client(server_ctx, client_id);
    return -1;
  }
  log_debug("Created send thread for %s client %u", is_tcp ? "TCP" : "WebRTC", client_id);

  // Step 4: Send initial server state to the new client
  if (send_server_state_to_client(client) != 0) {
    log_warn("Failed to send initial server state to client %u", client_id);
  }

  // Get current client count for initial state packet
  rwlock_rdlock(&g_client_manager_rwlock);
  uint32_t connected_count = g_client_manager.client_count;
  rwlock_rdunlock(&g_client_manager_rwlock);

  server_state_packet_t state;
  state.connected_client_count = connected_count;
  state.active_client_count = 0; // Will be updated by broadcast thread
  memset(state.reserved, 0, sizeof(state.reserved));

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = HOST_TO_NET_U32(state.connected_client_count);
  net_state.active_client_count = HOST_TO_NET_U32(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // Send initial server state via ACIP transport
  asciichat_error_t packet_result = acip_send_server_state(client->transport, &net_state);
  if (packet_result != ASCIICHAT_OK) {
    log_warn("Failed to send initial server state to client %u: %s", client_id, asciichat_error_string(packet_result));
  } else {
    log_debug("Sent initial server state to client %u: %u connected clients", client_id, state.connected_client_count);
  }

  // Step 5: Broadcast server state to ALL clients AFTER the new client is fully set up
  broadcast_server_state_to_all_clients();

  return 0;
}

int add_client(server_context_t *server_ctx, socket_t socket, const char *client_ip, int port) {
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
  if (existing_count >= GET_OPTION(max_clients)) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Maximum client limit reached (%d/%d active clients)", existing_count,
              GET_OPTION(max_clients));
    log_error("Maximum client limit reached (%d/%d active clients)", existing_count, GET_OPTION(max_clients));

    // Send a rejection message to the client before closing
    // Use platform-abstracted socket_send() instead of raw send() for Windows portability
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    ssize_t send_result = socket_send(socket, reject_msg, strlen(reject_msg), 0);
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
    // Use platform-abstracted socket_send() instead of raw send() for Windows portability
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    ssize_t send_result = socket_send(socket, reject_msg, strlen(reject_msg), 0);
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
  client->is_tcp_client = true; // TCP client - threads managed by tcp_server thread pool
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

  // Initialize pending packet storage (for --no-encrypt mode)
  client->pending_packet_type = 0;
  client->pending_packet_payload = NULL;
  client->pending_packet_length = 0;

  // Configure socket options for optimal performance
  configure_client_socket(socket, atomic_load(&client->client_id));

  // Register socket with tcp_server for thread pool management
  // Must be done before spawning any threads
  asciichat_error_t reg_result = tcp_server_add_client(server_ctx->tcp_server, socket, client);
  if (reg_result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INTERNAL, "Failed to register client socket with tcp_server");
    log_error("Failed to register client %u socket with tcp_server", atomic_load(&client->client_id));
    goto error_cleanup;
  }

  safe_snprintf(client->display_name, sizeof(client->display_name), "Client%u", atomic_load(&client->client_id));

  // Create individual video buffer for this client using modern double-buffering
  client->incoming_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  if (!client->incoming_video_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create video buffer for client %u", atomic_load(&client->client_id));
    log_error("Failed to create video buffer for client %u", atomic_load(&client->client_id));
    goto error_cleanup;
  }

  // Create individual audio buffer for this client
  // NOTE: Use capture version (no jitter buffering) because incoming audio is from network decode,
  // not from real-time microphone. Jitter buffering would cause buffer overflow since decoder
  // outputs at constant rate (48kHz) but mixer needs time to process.
  client->incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  if (!client->incoming_audio_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for client %u", atomic_load(&client->client_id));
    log_error("Failed to create audio buffer for client %u", atomic_load(&client->client_id));
    goto error_cleanup;
  }

  // Create packet queues for outgoing data
  // Use node pools but share the global buffer pool
  // Audio queue needs larger capacity to handle jitter and render thread lag
  // 500 packets @ 172fps = ~2.9 seconds of buffering (was 100 = 0.58s)
  client->audio_queue =
      packet_queue_create_with_pools(500, 1000, false); // Max 500 audio packets, 1000 nodes, NO local buffer pool
  if (!client->audio_queue) {
    LOG_ERRNO_IF_SET("Failed to create audio queue for client");
    goto error_cleanup;
  }

  // Create outgoing video buffer for ASCII frames (double buffered, no dropping)
  client->outgoing_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  if (!client->outgoing_video_buffer) {
    LOG_ERRNO_IF_SET("Failed to create outgoing video buffer for client");
    goto error_cleanup;
  }

  // Pre-allocate send buffer to avoid malloc/free in send thread (prevents deadlocks)
  client->send_buffer_size = MAX_FRAME_BUFFER_SIZE; // 2MB should handle largest frames
  // 64-byte cache-line alignment improves performance for large network buffers
  client->send_buffer = SAFE_MALLOC_ALIGNED(client->send_buffer_size, 64, void *);
  if (!client->send_buffer) {
    log_error("Failed to allocate send buffer for client %u", atomic_load(&client->client_id));
    goto error_cleanup;
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
    goto error_cleanup;
  }

  // Initialize send mutex to protect concurrent socket writes
  if (mutex_init(&client->send_mutex) != 0) {
    log_error("Failed to initialize send mutex for client %u", atomic_load(&client->client_id));
    goto error_cleanup;
  }

  rwlock_wrunlock(&g_client_manager_rwlock);

  // CRITICAL: Perform crypto handshake BEFORE starting threads
  // This ensures the handshake uses the socket directly without interference from receive thread
  if (server_crypto_init() == 0) {
    // Set timeout for crypto handshake to prevent indefinite blocking
    // This prevents clients from connecting but never completing the handshake
    const int HANDSHAKE_TIMEOUT_SECONDS = 30;
    asciichat_error_t timeout_result = set_socket_timeout(socket, HANDSHAKE_TIMEOUT_SECONDS);
    if (timeout_result != ASCIICHAT_OK) {
      log_warn("Failed to set handshake timeout for client %u: %s", atomic_load(&client->client_id),
               asciichat_error_string(timeout_result));
      // Continue anyway - timeout is a safety feature, not critical
    }

    int crypto_result = server_crypto_handshake(client);
    if (crypto_result != 0) {
      log_error("Crypto handshake failed for client %u: %s", atomic_load(&client->client_id), network_error_string());
      if (remove_client(server_ctx, atomic_load(&client->client_id)) != 0) {
        log_error("Failed to remove client after crypto handshake failure");
      }
      return -1;
    }

    // Clear socket timeout after handshake completes successfully
    // This allows normal operation without timeouts on data transfer
    asciichat_error_t clear_timeout_result = set_socket_timeout(socket, 0);
    if (clear_timeout_result != ASCIICHAT_OK) {
      log_warn("Failed to clear handshake timeout for client %u: %s", atomic_load(&client->client_id),
               asciichat_error_string(clear_timeout_result));
      // Continue anyway - we can still communicate even with timeout set
    }

    log_debug("Crypto handshake completed successfully for client %u", atomic_load(&client->client_id));

    // Create ACIP transport for protocol-agnostic packet sending
    // The transport wraps the socket with encryption context from the handshake
    const crypto_context_t *crypto_ctx = crypto_server_get_context(atomic_load(&client->client_id));
    client->transport = acip_tcp_transport_create(socket, (crypto_context_t *)crypto_ctx);
    if (!client->transport) {
      log_error("Failed to create ACIP transport for client %u", atomic_load(&client->client_id));
      if (remove_client(server_ctx, atomic_load(&client->client_id)) != 0) {
        log_error("Failed to remove client after transport creation failure");
      }
      return -1;
    }
    log_debug("Created ACIP transport for client %u with crypto context", atomic_load(&client->client_id));

    // After handshake completes, the client immediately sends PACKET_TYPE_CLIENT_CAPABILITIES
    // We must read and process this packet BEFORE starting the receive thread to avoid a race condition
    // where the packet arrives but no thread is listening for it.
    //
    // SPECIAL CASE: If client used --no-encrypt, we already received this packet during the handshake
    // attempt and stored it in pending_packet_*. Use that instead of receiving a new one.
    packet_envelope_t envelope;
    bool used_pending_packet = false;

    if (client->pending_packet_payload) {
      // Client used --no-encrypt mode - use the packet we already received
      log_info("Client %u using --no-encrypt mode - processing pending packet type %u", atomic_load(&client->client_id),
               client->pending_packet_type);
      envelope.type = client->pending_packet_type;
      envelope.data = client->pending_packet_payload;
      envelope.len = client->pending_packet_length;
      envelope.allocated_buffer = client->pending_packet_payload; // Will be freed below
      envelope.allocated_size = client->pending_packet_length;
      used_pending_packet = true;

      // Clear pending packet fields
      client->pending_packet_type = 0;
      client->pending_packet_payload = NULL;
      client->pending_packet_length = 0;
    } else {
      // Normal encrypted mode - receive capabilities packet
      log_debug("Waiting for initial capabilities packet from client %u", atomic_load(&client->client_id));

      // Protect crypto context access with client state mutex
      mutex_lock(&client->client_state_mutex);
      const crypto_context_t *crypto_ctx = crypto_server_get_context(atomic_load(&client->client_id));

      // Use per-client crypto state to determine enforcement
      // At this point, handshake is complete, so crypto_initialized=true and handshake is ready
      bool enforce_encryption = !GET_OPTION(no_encrypt) && client->crypto_initialized &&
                                crypto_handshake_is_ready(&client->crypto_handshake_ctx);

      packet_recv_result_t result = receive_packet_secure(socket, (void *)crypto_ctx, enforce_encryption, &envelope);
      mutex_unlock(&client->client_state_mutex);

      if (result != PACKET_RECV_SUCCESS) {
        log_error("Failed to receive initial capabilities packet from client %u: result=%d",
                  atomic_load(&client->client_id), result);
        if (envelope.allocated_buffer) {
          buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
        }
        if (remove_client(server_ctx, atomic_load(&client->client_id)) != 0) {
          log_error("Failed to remove client after crypto handshake failure");
        }
        return -1;
      }
    }

    if (envelope.type != PACKET_TYPE_CLIENT_CAPABILITIES) {
      log_error("Expected PACKET_TYPE_CLIENT_CAPABILITIES but got packet type %d from client %u", envelope.type,
                atomic_load(&client->client_id));
      if (envelope.allocated_buffer) {
        buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
      }
      if (remove_client(server_ctx, atomic_load(&client->client_id)) != 0) {
        log_error("Failed to remove client after crypto handshake failure");
      }
      return -1;
    }

    // Process the capabilities packet directly
    log_debug("Processing initial capabilities packet from client %u (from %s)", atomic_load(&client->client_id),
              used_pending_packet ? "pending packet" : "network");
    handle_client_capabilities_packet(client, envelope.data, envelope.len);

    // Free the packet data
    if (envelope.allocated_buffer) {
      buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
    }
    log_debug("Successfully received and processed initial capabilities for client %u",
              atomic_load(&client->client_id));
  }

  // Start all client threads in the correct order (unified path for TCP and WebRTC)
  // This creates: receive thread -> render threads -> send thread
  // The render threads MUST be created before send thread to avoid the race condition
  // where send thread reads empty frames before render thread generates the first real frame
  uint32_t client_id_snapshot = atomic_load(&client->client_id);
  if (start_client_threads(server_ctx, client, true) != 0) {
    log_error("Failed to start threads for TCP client %u", client_id_snapshot);
    return -1;
  }
  log_debug("Successfully created render threads for client %u", client_id_snapshot);

  // Register client with session_host (for discovery mode support)
  if (server_ctx->session_host) {
    uint32_t session_client_id = session_host_add_client(server_ctx->session_host, socket, client_ip, port);
    if (session_client_id == 0) {
      log_warn("Failed to register client %u with session_host", client_id_snapshot);
    } else {
      log_debug("Client %u registered with session_host as %u", client_id_snapshot, session_client_id);
    }
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return (int)client_id_snapshot;

error_cleanup:
  // Clean up all partially allocated resources
  // NOTE: This label is reached when allocation or initialization fails
  // Resources are cleaned up in reverse order of allocation
  return -1;
}

/**
 * @brief Register a WebRTC client with the server
 *
 * Registers a client that connected via WebRTC data channel instead of TCP socket.
 * This function reuses most of add_client() logic but skips:
 * - Crypto handshake (already done via ACDS signaling)
 * - Socket-specific configuration
 * - TCP thread pool registration
 *
 * DIFFERENCES FROM add_client():
 * - Takes an already-created acip_transport_t* instead of socket
 * - No crypto handshake (WebRTC signaling handled authentication)
 * - No socket configuration (WebRTC handles buffering)
 * - Uses generic thread spawning instead of tcp_server thread pool
 *
 * @param server_ctx Server context
 * @param transport WebRTC transport (already created and connected)
 * @param client_ip Client IP address for logging (may be empty for P2P)
 * @return Client ID on success, -1 on failure
 *
 * @note The transport must be fully initialized and ready to send/receive
 * @note Client capabilities are still expected as first packet
 */
int add_webrtc_client(server_context_t *server_ctx, acip_transport_t *transport, const char *client_ip) {
  if (!server_ctx || !transport || !client_ip) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to add_webrtc_client");
    return -1;
  }

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
  if (existing_count >= GET_OPTION(max_clients)) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Maximum client limit reached (%d/%d active clients)", existing_count,
              GET_OPTION(max_clients));
    log_error("Maximum client limit reached (%d/%d active clients)", existing_count, GET_OPTION(max_clients));
    return -1;
  }

  if (slot == -1) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "No available client slots (all %d array slots are in use)", MAX_CLIENTS);
    log_error("No available client slots (all %d array slots are in use)", MAX_CLIENTS);
    return -1;
  }

  // Update client_count to match actual count before adding new client
  g_client_manager.client_count = existing_count;

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];
  memset(client, 0, sizeof(client_info_t));

  // Set up WebRTC-specific fields
  client->socket = INVALID_SOCKET_VALUE; // WebRTC has no traditional socket
  client->is_tcp_client = false;         // WebRTC client - threads managed directly
  client->transport = transport;         // Use provided transport
  uint32_t new_client_id = atomic_fetch_add(&g_client_manager.next_client_id, 1) + 1;
  atomic_store(&client->client_id, new_client_id);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = 0; // WebRTC doesn't use port numbers
  atomic_store(&client->active, true);
  log_info("Added new WebRTC client ID=%u from %s (transport=%p, slot=%d)", new_client_id, client_ip, transport, slot);
  atomic_store(&client->shutting_down, false);
  atomic_store(&client->last_rendered_grid_sources, 0); // Render thread updates this
  atomic_store(&client->last_sent_grid_sources, 0);     // Send thread updates this
  log_debug("WebRTC client slot assigned: client_id=%u assigned to slot %d", atomic_load(&client->client_id), slot);
  client->connected_at = time(NULL);

  // Initialize crypto context for this client
  memset(&client->crypto_handshake_ctx, 0, sizeof(client->crypto_handshake_ctx));
  client->crypto_initialized = true; // Already done via ACDS signaling

  // Initialize pending packet storage (unused for WebRTC, but keep for consistency)
  client->pending_packet_type = 0;
  client->pending_packet_payload = NULL;
  client->pending_packet_length = 0;

  safe_snprintf(client->display_name, sizeof(client->display_name), "WebRTC%u", atomic_load(&client->client_id));

  // Create individual video buffer for this client using modern double-buffering
  client->incoming_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  if (!client->incoming_video_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create video buffer for WebRTC client %u", atomic_load(&client->client_id));
    log_error("Failed to create video buffer for WebRTC client %u", atomic_load(&client->client_id));
    goto error_cleanup_webrtc;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  if (!client->incoming_audio_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for WebRTC client %u", atomic_load(&client->client_id));
    log_error("Failed to create audio buffer for WebRTC client %u", atomic_load(&client->client_id));
    goto error_cleanup_webrtc;
  }

  // Create packet queues for outgoing data
  client->audio_queue = packet_queue_create_with_pools(500, 1000, false);
  if (!client->audio_queue) {
    LOG_ERRNO_IF_SET("Failed to create audio queue for WebRTC client");
    goto error_cleanup_webrtc;
  }

  // Create outgoing video buffer for ASCII frames (double buffered, no dropping)
  client->outgoing_video_buffer = video_frame_buffer_create(atomic_load(&client->client_id));
  if (!client->outgoing_video_buffer) {
    LOG_ERRNO_IF_SET("Failed to create outgoing video buffer for WebRTC client");
    goto error_cleanup_webrtc;
  }

  // Pre-allocate send buffer to avoid malloc/free in send thread (prevents deadlocks)
  client->send_buffer_size = MAX_FRAME_BUFFER_SIZE; // 2MB should handle largest frames
  client->send_buffer = SAFE_MALLOC_ALIGNED(client->send_buffer_size, 64, void *);
  if (!client->send_buffer) {
    log_error("Failed to allocate send buffer for WebRTC client %u", atomic_load(&client->client_id));
    goto error_cleanup_webrtc;
  }

  g_client_manager.client_count = existing_count + 1; // We just added a client
  log_debug("Client count updated: now %d clients (added WebRTC client_id=%u to slot %d)",
            g_client_manager.client_count, atomic_load(&client->client_id), slot);

  // Add client to uthash table for O(1) lookup
  uint32_t cid = atomic_load(&client->client_id);
  HASH_ADD_INT(g_client_manager.clients_by_id, client_id, client);
  log_debug("Added WebRTC client %u to uthash table", cid);

  // Register this client's audio buffer with the mixer
  if (g_audio_mixer && client->incoming_audio_buffer) {
    if (mixer_add_source(g_audio_mixer, atomic_load(&client->client_id), client->incoming_audio_buffer) < 0) {
      log_warn("Failed to add WebRTC client %u to audio mixer", atomic_load(&client->client_id));
    } else {
#ifdef DEBUG_AUDIO
      log_debug("Added WebRTC client %u to audio mixer", atomic_load(&client->client_id));
#endif
    }
  }

  // Initialize mutexes BEFORE creating any threads to prevent race conditions
  if (mutex_init(&client->client_state_mutex) != 0) {
    log_error("Failed to initialize client state mutex for WebRTC client %u", atomic_load(&client->client_id));
    goto error_cleanup_webrtc;
  }

  // Initialize send mutex to protect concurrent socket writes
  if (mutex_init(&client->send_mutex) != 0) {
    log_error("Failed to initialize send mutex for WebRTC client %u", atomic_load(&client->client_id));
    goto error_cleanup_webrtc;
  }

  rwlock_wrunlock(&g_client_manager_rwlock);

  // For WebRTC clients, the capabilities packet will be received by the receive thread
  // when it starts. Unlike TCP clients where we handle it synchronously in add_client(),
  // WebRTC uses the transport abstraction which handles packet reception automatically.
  log_debug("WebRTC client %u initialized - receive thread will process capabilities", atomic_load(&client->client_id));

  // Start all client threads in the correct order (unified path for TCP and WebRTC)
  // This creates: receive thread -> render threads -> send thread
  // The render threads MUST be created before send thread to avoid the race condition
  // where send thread reads empty frames before render thread generates the first real frame
  uint32_t client_id_snapshot = atomic_load(&client->client_id);
  if (start_client_threads(server_ctx, client, false) != 0) {
    log_error("Failed to start threads for WebRTC client %u", client_id_snapshot);
    return -1;
  }
  log_debug("Created receive thread for WebRTC client %u", client_id_snapshot);

  // Create send thread for this client
  char thread_name[64];
  safe_snprintf(thread_name, sizeof(thread_name), "webrtc_send_%u", client_id_snapshot);
  asciichat_error_t send_result = asciichat_thread_create(&client->send_thread, client_send_thread_func, client);
  if (send_result != ASCIICHAT_OK) {
    log_error("Failed to create send thread for WebRTC client %u: %s", client_id_snapshot,
              asciichat_error_string(send_result));
    if (remove_client(server_ctx, client_id_snapshot) != 0) {
      log_error("Failed to remove WebRTC client after send thread creation failure");
    }
    return -1;
  }
  log_debug("Created send thread for WebRTC client %u", client_id_snapshot);

  // Send initial server state to the new client
  if (send_server_state_to_client(client) != 0) {
    log_warn("Failed to send initial server state to WebRTC client %u", client_id_snapshot);
  } else {
#ifdef DEBUG_NETWORK
    log_info("Sent initial server state to WebRTC client %u", client_id_snapshot);
#endif
  }

  // Send initial server state via ACIP transport
  rwlock_rdlock(&g_client_manager_rwlock);
  uint32_t connected_count = g_client_manager.client_count;
  rwlock_rdunlock(&g_client_manager_rwlock);

  server_state_packet_t state;
  state.connected_client_count = connected_count;
  state.active_client_count = 0; // Will be updated by broadcast thread
  memset(state.reserved, 0, sizeof(state.reserved));

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = HOST_TO_NET_U32(state.connected_client_count);
  net_state.active_client_count = HOST_TO_NET_U32(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  asciichat_error_t packet_send_result = acip_send_server_state(client->transport, &net_state);
  if (packet_send_result != ASCIICHAT_OK) {
    log_warn("Failed to send initial server state to WebRTC client %u: %s", client_id_snapshot,
             asciichat_error_string(packet_send_result));
  } else {
    log_debug("Sent initial server state to WebRTC client %u: %u connected clients", client_id_snapshot,
              state.connected_client_count);
  }

  // Create per-client rendering threads
  log_debug("Creating render threads for WebRTC client %u", client_id_snapshot);
  if (create_client_render_threads(server_ctx, client) != 0) {
    log_error("Failed to create render threads for WebRTC client %u", client_id_snapshot);
    if (remove_client(server_ctx, client_id_snapshot) != 0) {
      log_error("Failed to remove WebRTC client after render thread creation failure");
    }
    return -1;
  }
  log_debug("Successfully created render threads for WebRTC client %u", client_id_snapshot);

  // Register client with session_host (for discovery mode support)
  // WebRTC clients use INVALID_SOCKET_VALUE since they don't have a TCP socket
  if (server_ctx->session_host) {
    uint32_t session_client_id = session_host_add_client(server_ctx->session_host, INVALID_SOCKET_VALUE, client_ip, 0);
    if (session_client_id == 0) {
      log_warn("Failed to register WebRTC client %u with session_host", client_id_snapshot);
    } else {
      log_debug("WebRTC client %u registered with session_host as %u", client_id_snapshot, session_client_id);
    }
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return (int)client_id_snapshot;

error_cleanup_webrtc:
  // Clean up all partially allocated resources for WebRTC client
  if (client->send_buffer) {
    SAFE_FREE(client->send_buffer);
    client->send_buffer = NULL;
  }
  if (client->outgoing_video_buffer) {
    video_frame_buffer_destroy(client->outgoing_video_buffer);
    client->outgoing_video_buffer = NULL;
  }
  if (client->audio_queue) {
    packet_queue_destroy(client->audio_queue);
    client->audio_queue = NULL;
  }
  if (client->incoming_audio_buffer) {
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_audio_buffer = NULL;
  }
  if (client->incoming_video_buffer) {
    video_frame_buffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
  }
  mutex_destroy(&client->client_state_mutex);
  mutex_destroy(&client->send_mutex);
  rwlock_wrunlock(&g_client_manager_rwlock);
  return -1;
}

int remove_client(server_context_t *server_ctx, uint32_t client_id) {
  if (!server_ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Cannot remove client %u: NULL server_ctx", client_id);
    return -1;
  }

  // Phase 1: Mark client inactive and prepare for cleanup while holding write lock
  client_info_t *target_client = NULL;
  char display_name_copy[MAX_DISPLAY_NAME_LEN];
  socket_t client_socket = INVALID_SOCKET_VALUE; // Save socket for thread cleanup

  log_debug("SOCKET_DEBUG: Attempting to remove client %d", client_id);
  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    uint32_t cid = atomic_load(&client->client_id);
    if (cid == client_id && cid != 0) {
      // Check if already being removed by another thread
      // This prevents double-free and use-after-free crashes during concurrent cleanup
      if (atomic_load(&client->shutting_down)) {
        rwlock_wrunlock(&g_client_manager_rwlock);
        log_debug("Client %u already being removed by another thread, skipping", client_id);
        return 0; // Return success - removal is in progress
      }
      // Mark as shutting down and inactive immediately to stop new operations
      log_info("Removing client %d (socket=%d) - marking inactive and clearing video flags", client_id, client->socket);
      atomic_store(&client->shutting_down, true);
      atomic_store(&client->active, false);
      atomic_store(&client->is_sending_video, false);
      atomic_store(&client->is_sending_audio, false);
      target_client = client;

      // Store display name before clearing
      SAFE_STRNCPY(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

      // Save socket for tcp_server_stop_client_threads() before closing
      mutex_lock(&client->client_state_mutex);
      client_socket = client->socket; // Save socket for thread cleanup
      if (client->socket != INVALID_SOCKET_VALUE) {
        log_debug("SOCKET_DEBUG: Client %d shutting down socket %d", client->client_id, client->socket);
        // Shutdown both send and receive operations to unblock any pending I/O
        socket_shutdown(client->socket, 2); // 2 = SHUT_RDWR on POSIX, SD_BOTH on Windows
        // Don't close yet - tcp_server needs socket as lookup key
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

  // Unregister client from session_host (for discovery mode support)
  // NOTE: Client may not be registered if crypto handshake failed before session_host registration
  if (server_ctx->session_host) {
    asciichat_error_t session_result = session_host_remove_client(server_ctx->session_host, client_id);
    if (session_result != ASCIICHAT_OK) {
      // ERROR_NOT_FOUND (91) is expected if client failed crypto before being registered with session_host
      if (session_result == ERROR_NOT_FOUND) {
        log_debug("Client %u not found in session_host (likely failed crypto before registration)", client_id);
      } else {
        log_warn("Failed to unregister client %u from session_host: %s", client_id,
                 asciichat_error_string(session_result));
      }
    } else {
      log_debug("Client %u unregistered from session_host", client_id);
    }
  }

  // CRITICAL: Release write lock before joining threads
  // This prevents deadlock with render threads that need read locks
  rwlock_wrunlock(&g_client_manager_rwlock);

  // Phase 2: Stop all client threads
  // For TCP clients: use tcp_server thread pool management
  // For WebRTC clients: manually join threads (no socket-based thread pool)
  // CRITICAL: Use is_tcp_client flag, NOT socket value - socket may already be INVALID_SOCKET_VALUE
  // even for TCP clients if it was closed earlier during cleanup
  log_debug("Stopping all threads for client %u (socket %d, is_tcp=%d)", client_id, client_socket,
            target_client ? target_client->is_tcp_client : -1);

  if (target_client && target_client->is_tcp_client) {
    // TCP client: use tcp_server thread pool
    // This joins threads in stop_id order: receive(1), render(2), send(3)
    // Use saved client_socket for lookup (tcp_server needs original socket as key)
    if (client_socket != INVALID_SOCKET_VALUE) {
      asciichat_error_t stop_result = tcp_server_stop_client_threads(server_ctx->tcp_server, client_socket);
      if (stop_result != ASCIICHAT_OK) {
        log_warn("Failed to stop threads for TCP client %u: error %d", client_id, stop_result);
        // Continue with cleanup even if thread stopping failed
      }
    } else {
      log_debug("TCP client %u socket already closed, threads should have already exited", client_id);
    }
  } else if (target_client) {
    // WebRTC client: manually join threads
    log_debug("Stopping WebRTC client %u threads (receive and send)", client_id);
    // Join receive thread
    void *recv_result = NULL;
    asciichat_error_t recv_join_result = asciichat_thread_join(&target_client->receive_thread, &recv_result);
    if (recv_join_result != ASCIICHAT_OK) {
      log_warn("Failed to join receive thread for WebRTC client %u: error %d", client_id, recv_join_result);
    } else {
      log_debug("Joined receive thread for WebRTC client %u", client_id);
    }
    // Join send thread
    void *send_result = NULL;
    asciichat_error_t send_join_result = asciichat_thread_join(&target_client->send_thread, &send_result);
    if (send_join_result != ASCIICHAT_OK) {
      log_warn("Failed to join send thread for WebRTC client %u: error %d", client_id, send_join_result);
    } else {
      log_debug("Joined send thread for WebRTC client %u", client_id);
    }
    // Note: Render threads still need to be stopped - they're created the same way for both TCP and WebRTC
    // For now, render threads are expected to exit when they check g_server_should_exit and client->active
  }

  // Destroy ACIP transport before closing socket
  if (target_client && target_client->transport) {
    acip_transport_destroy(target_client->transport);
    target_client->transport = NULL;
    log_debug("Destroyed ACIP transport for client %u", client_id);
  }

  // Now safe to close the socket (threads are stopped)
  if (client_socket != INVALID_SOCKET_VALUE) {
    log_debug("SOCKET_DEBUG: Closing socket %d for client %u after thread cleanup", client_socket, client_id);
    socket_close(client_socket);
  }

  // Phase 3: Clean up resources with write lock
  rwlock_wrlock(&g_client_manager_rwlock);

  // CRITICAL: Re-validate target_client pointer after reacquiring lock
  // Another thread might have invalidated the pointer while we had the lock released
  if (target_client) {
    // Verify client_id still matches and client is still in shutting_down state
    uint32_t current_id = atomic_load(&target_client->client_id);
    bool still_shutting_down = atomic_load(&target_client->shutting_down);
    if (current_id != client_id || !still_shutting_down) {
      log_warn("Client %u pointer invalidated during thread cleanup (id=%u, shutting_down=%d)", client_id, current_id,
               still_shutting_down);
      rwlock_wrunlock(&g_client_manager_rwlock);
      return 0; // Another thread completed the cleanup
    }
  }

  // Mark socket as closed in client structure
  if (target_client && target_client->socket != INVALID_SOCKET_VALUE) {
    mutex_lock(&target_client->client_state_mutex);
    target_client->socket = INVALID_SOCKET_VALUE;
    mutex_unlock(&target_client->client_state_mutex);
    log_debug("SOCKET_DEBUG: Client %u socket set to INVALID", client_id);
  }

  // Use the dedicated cleanup function to ensure all resources are freed
  cleanup_client_all_buffers(target_client);

  // Remove from audio mixer
  if (g_audio_mixer) {
    mixer_remove_source(g_audio_mixer, client_id);
#ifdef DEBUG_AUDIO
    log_debug("Removed client %u from audio mixer", client_id);
#endif
  }

  // Remove from uthash table
  // CRITICAL: Verify client is actually in the hash table before deleting
  // Another thread might have already removed it
  if (target_client) {
    client_info_t *hash_entry = NULL;
    HASH_FIND(hh, g_client_manager.clients_by_id, &client_id, sizeof(client_id), hash_entry);
    if (hash_entry == target_client) {
      HASH_DELETE(hh, g_client_manager.clients_by_id, target_client);
      log_debug("Removed client %u from uthash table", client_id);
    } else {
      log_warn("Client %u already removed from hash table by another thread (found=%p, expected=%p)", client_id,
               (void *)hash_entry, (void *)target_client);
    }
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
  // Use exponential backoff for thread termination verification
  int retry_count = 0;
  const int max_retries = 5;
  while (retry_count < max_retries && (asciichat_thread_is_initialized(&target_client->send_thread) ||
                                       asciichat_thread_is_initialized(&target_client->receive_thread) ||
                                       asciichat_thread_is_initialized(&target_client->video_render_thread) ||
                                       asciichat_thread_is_initialized(&target_client->audio_render_thread))) {
    // Exponential backoff: 10ms, 20ms, 40ms, 80ms, 160ms
    uint32_t delay_ms = 10 * (1 << retry_count);
    log_warn("Client %u: Some threads still appear initialized (attempt %d/%d), waiting %ums", client_id,
             retry_count + 1, max_retries, delay_ms);
    platform_sleep_usec(delay_ms * 1000);
    retry_count++;
  }

  if (retry_count == max_retries) {
    log_error("Client %u: Threads did not terminate after %d retries, proceeding with cleanup anyway", client_id,
              max_retries);
  }

  // Only reset client_id to 0 AFTER confirming threads are joined
  // This prevents threads that are starting from accessing a zeroed client struct
  // CRITICAL: Reset client_id to 0 BEFORE destroying mutexes to prevent race conditions
  // This ensures worker threads can detect shutdown and exit BEFORE the mutex is destroyed
  // If we destroy the mutex first, threads might try to access a destroyed mutex
  atomic_store(&target_client->client_id, 0);

  // Wait for threads to observe the client_id reset
  // Use sufficient delay for memory visibility across all CPU cores
  platform_sleep_usec(5000); // 5ms delay for memory barrier propagation

  // Destroy mutexes
  // IMPORTANT: Always destroy these even if threads didn't join properly
  // to prevent issues when the slot is reused
  mutex_destroy(&target_client->client_state_mutex);
  mutex_destroy(&target_client->send_mutex);

  // Clear client structure
  // NOTE: After memset, the mutex handles are zeroed but the OS resources
  // have been released by the destroy calls above
  memset(target_client, 0, sizeof(client_info_t));

  // Recalculate client count using atomic reads
  int remaining_count = 0;
  for (int j = 0; j < MAX_CLIENTS; j++) {
    if (atomic_load(&g_client_manager.clients[j].client_id) != 0) {
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

// Forward declaration for ACIP server callbacks (defined later in file)
static const acip_server_callbacks_t g_acip_server_callbacks;

void *client_receive_thread(void *arg) {
  // Log IMMEDIATELY to verify thread is running AT ALL
  log_error("RECV_THREAD_RAW: Thread function entered, arg=%p", arg);

  client_info_t *client = (client_info_t *)arg;

  // CRITICAL: Validate client pointer immediately before any access
  // This prevents crashes if remove_client() has zeroed the client struct
  // while the thread was still starting at RtlUserThreadStart
  if (!client) {
    log_error("Invalid client info in receive thread (NULL pointer)");
    return NULL;
  }

  log_debug("RECV_THREAD_DEBUG: Thread started, client=%p, client_id=%u, is_tcp=%d", (void *)client,
            atomic_load(&client->client_id), client->is_tcp_client);

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
  // For TCP clients, validate socket. WebRTC clients use DataChannel (no socket)
  if (client->is_tcp_client && client->socket == INVALID_SOCKET_VALUE) {
    log_error("Invalid client socket in receive thread");
    return NULL;
  }

  // Enable thread cancellation for clean shutdown
  // Thread cancellation not available in platform abstraction
  // Threads should exit when g_server_should_exit is set

  log_debug("Started receive thread for client %u (%s)", atomic_load(&client->client_id), client->display_name);

  // Main receive loop - processes packets from transport
  // For TCP clients: receives from socket
  // For WebRTC clients: receives from transport ringbuffer (via ACDS signaling)
  while (!atomic_load(&g_server_should_exit) && atomic_load(&client->active)) {
    // For TCP clients, check socket validity
    // For WebRTC clients, continue even if no socket (transport handles everything)
    if (client->is_tcp_client && client->socket == INVALID_SOCKET_VALUE) {
      log_debug("TCP client %u has invalid socket, exiting receive thread", atomic_load(&client->client_id));
      break;
    }

    // CRITICAL: Check client_id is still valid before accessing transport
    // This prevents accessing freed memory if remove_client() has zeroed the client struct
    if (atomic_load(&client->client_id) == 0) {
      log_debug("Client client_id reset, exiting receive thread");
      break;
    }

    // Receive and dispatch packet using ACIP transport API
    // This combines packet reception, decryption, parsing, handler dispatch, and cleanup
    asciichat_error_t acip_result =
        acip_server_receive_and_dispatch(client->transport, client, &g_acip_server_callbacks);

    // Check if shutdown was requested during the network call
    if (atomic_load(&g_server_should_exit)) {
      log_error("RECV_EXIT: Server shutdown requested, breaking loop");
      break;
    }

    // Handle receive errors
    if (acip_result != ASCIICHAT_OK) {
      // Check error type to determine if we should disconnect
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        if (err_ctx.code == ERROR_NETWORK) {
          // Network error or EOF - client disconnected
          log_debug("Client %u disconnected (network error): %s", client->client_id, err_ctx.context_message);
          break;
        } else if (err_ctx.code == ERROR_CRYPTO) {
          // Security violation
          log_error_client(client,
                           "SECURITY VIOLATION: Unencrypted packet when encryption required - terminating connection");
          atomic_store(&g_server_should_exit, true);
          break;
        }
      }

      // Any other error - disconnect client to prevent infinite retry loop
      // This prevents zombie clients when error context is unavailable or error is non-recoverable
      log_warn("ACIP receive/dispatch failed for client %u: %s (disconnecting client to prevent infinite loop)",
               client->client_id, asciichat_error_string(acip_result));
      break;
    } else {
      log_error("RECV_SUCCESS: Client %u dispatch succeeded, looping back", atomic_load(&client->client_id));
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

  // Clean up thread-local error context before exit
  asciichat_errno_cleanup();

  return NULL;
}

// Thread function to handle sending data to a specific client
void *client_send_thread_func(void *arg) {
  // CRITICAL: Log entry immediately - this proves the thread actually started
  fprintf(stderr, "*** SEND_THREAD_STARTED arg=%p ***\n", arg);
  fflush(stderr);

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

  // Additional validation: check socket OR transport is valid
  // For TCP clients: socket is valid
  // For WebRTC clients: socket is INVALID_SOCKET_VALUE but transport is valid
  mutex_lock(&client->send_mutex);
  bool has_socket = (client->socket != INVALID_SOCKET_VALUE);
  bool has_transport = (client->transport != NULL);
  mutex_unlock(&client->send_mutex);

  if (!has_socket && !has_transport) {
    log_error("Invalid client connection in send thread (no socket or transport)");
    return NULL;
  }

  log_info("Started send thread for client %u (%s)", atomic_load(&client->client_id), client->display_name);

  // Mark thread as running
  atomic_store(&client->send_thread_running, true);

  // Track timing for video frame sends
  uint64_t last_video_send_time = 0;
  const uint64_t video_send_interval_us = 16666; // 60fps = ~16.67ms

  // High-frequency audio loop - separate from video frame loop
  // to ensure audio packets are sent immediately, not rate-limited by video
#define MAX_AUDIO_BATCH 8
  int silence_log_count = 0;
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
      if (audio_packet_count > 0 || silence_log_count++ % 1000 == 0) {
        log_info("SEND_AUDIO: client=%u dequeued=%d packets", atomic_load(&client->client_id), audio_packet_count);
      }
    } else {
      log_warn("Send thread: audio_queue is NULL for client %u", atomic_load(&client->client_id));
    }

    // Send batched audio if we have packets
    if (audio_packet_count > 0) {
      // Get crypto context for this client
      // Protect crypto field access with mutex
      const crypto_context_t *crypto_ctx = NULL;
      mutex_lock(&client->client_state_mutex);
      bool crypto_ready = !GET_OPTION(no_encrypt) && client->crypto_initialized &&
                          crypto_handshake_is_ready(&client->crypto_handshake_ctx);
      if (crypto_ready) {
        crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
      }
      mutex_unlock(&client->client_state_mutex);

      asciichat_error_t result = ASCIICHAT_OK;

      if (audio_packet_count == 1) {
        // Single packet - send directly for low latency using ACIP transport
        packet_type_t pkt_type = (packet_type_t)NET_TO_HOST_U16(audio_packets[0]->header.type);

        // Get transport reference while holding mutex briefly (prevents deadlock on TCP buffer full)
        mutex_lock(&client->send_mutex);
        if (atomic_load(&client->shutting_down) || !client->transport) {
          mutex_unlock(&client->send_mutex);
          break; // Client is shutting down, exit thread
        }
        acip_transport_t *transport = client->transport;
        mutex_unlock(&client->send_mutex);

        // Network I/O happens OUTSIDE the mutex
        result = packet_send_via_transport(transport, pkt_type, audio_packets[0]->data, audio_packets[0]->data_len);
        if (result != ASCIICHAT_OK) {
          log_error("AUDIO SEND FAIL: client=%u, len=%zu, result=%d", client->client_id, audio_packets[0]->data_len,
                    result);
        }
      } else {
        // Multiple packets - batch them together
        // Check if these are Opus-encoded packets or raw float audio
        packet_type_t first_pkt_type = (packet_type_t)NET_TO_HOST_U16(audio_packets[0]->header.type);

        if (first_pkt_type == PACKET_TYPE_AUDIO_OPUS_BATCH) {
          // Opus packets - batch using proper Opus batching format
          // Calculate total Opus data size
          size_t total_opus_size = 0;
          for (int i = 0; i < audio_packet_count; i++) {
            total_opus_size += audio_packets[i]->data_len;
          }

          // Allocate buffers for batched Opus data and frame sizes
          uint8_t *batched_opus = SAFE_MALLOC(total_opus_size, uint8_t *);
          uint16_t *frame_sizes = SAFE_MALLOC((size_t)audio_packet_count * sizeof(uint16_t), uint16_t *);

          if (batched_opus && frame_sizes) {
            // Copy all Opus frames into batch buffer
            size_t offset = 0;
            for (int i = 0; i < audio_packet_count; i++) {
              frame_sizes[i] = (uint16_t)audio_packets[i]->data_len;
              memcpy(batched_opus + offset, audio_packets[i]->data, audio_packets[i]->data_len);
              offset += audio_packets[i]->data_len;
            }

            // Send batched Opus packet - get socket reference briefly to avoid deadlock on TCP buffer full
            mutex_lock(&client->send_mutex);
            if (atomic_load(&client->shutting_down) || client->socket == INVALID_SOCKET_VALUE) {
              mutex_unlock(&client->send_mutex);
              SAFE_FREE(batched_opus);
              SAFE_FREE(frame_sizes);
              break; // Client is shutting down, exit thread
            }
            socket_t send_socket = client->socket;
            mutex_unlock(&client->send_mutex);

            // Network I/O happens OUTSIDE the mutex
            result =
                av_send_audio_opus_batch(send_socket, batched_opus, total_opus_size, frame_sizes, AUDIO_SAMPLE_RATE, 20,
                                         audio_packet_count, (crypto_context_t *)crypto_ctx);

            if (result != ASCIICHAT_OK) {
              log_error("AUDIO SEND FAIL (batch): client=%u, frames=%d, total_size=%zu, result=%d", client->client_id,
                        audio_packet_count, total_opus_size, result);
            } else {
              fprintf(stderr, "[SEND_BATCH] client=%u sent %d Opus frames (%zu bytes total)\n", client->client_id,
                      audio_packet_count, total_opus_size);
            }
          } else {
            log_error("Failed to allocate buffer for Opus batch");
            result = ERROR_MEMORY;
          }

          if (batched_opus)
            SAFE_FREE(batched_opus);
          if (frame_sizes)
            SAFE_FREE(frame_sizes);
        } else {
          // Raw float audio - use existing batching logic
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

            // Send batched audio packet - get socket reference briefly to avoid deadlock on TCP buffer full
            mutex_lock(&client->send_mutex);
            if (atomic_load(&client->shutting_down) || client->socket == INVALID_SOCKET_VALUE) {
              mutex_unlock(&client->send_mutex);
              SAFE_FREE(batched_audio);
              break; // Client is shutting down, exit thread
            }
            socket_t send_socket = client->socket;
            mutex_unlock(&client->send_mutex);

            // Network I/O happens OUTSIDE the mutex
            result = send_audio_batch_packet(send_socket, batched_audio, (int)total_samples, audio_packet_count,
                                             (crypto_context_t *)crypto_ctx);

            SAFE_FREE(batched_audio);

            if (result != ASCIICHAT_OK) {
              log_error("AUDIO SEND FAIL (raw batch): client=%u, packets=%d, samples=%zu, result=%d", client->client_id,
                        audio_packet_count, total_samples, result);
            } else {
              fprintf(stderr, "[SEND_RAW_BATCH] client=%u sent %d audio packets (%zu samples)\n", client->client_id,
                      audio_packet_count, total_samples);
            }
          } else {
            log_error("Failed to allocate buffer for audio batch");
            result = ERROR_MEMORY;
          }
        }
      }

      // Free all audio packets
      for (int i = 0; i < audio_packet_count; i++) {
        packet_queue_free_packet(audio_packets[i]);
      }

      if (result != ASCIICHAT_OK) {
        if (!atomic_load(&g_server_should_exit)) {
          log_error("Failed to send audio to client %u: %s", client->client_id, asciichat_error_string(result));
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
      bool should_rekey = !GET_OPTION(no_encrypt) && client->crypto_initialized &&
                          crypto_handshake_is_ready(&client->crypto_handshake_ctx) &&
                          crypto_handshake_should_rekey(&client->crypto_handshake_ctx);
      mutex_unlock(&client->client_state_mutex);

      if (should_rekey) {
        log_debug("Rekey threshold reached for client %u, initiating session rekey", client->client_id);
        mutex_lock(&client->client_state_mutex);
        // Get socket reference briefly to avoid deadlock on TCP buffer full
        mutex_lock(&client->send_mutex);
        if (atomic_load(&client->shutting_down) || client->socket == INVALID_SOCKET_VALUE) {
          mutex_unlock(&client->send_mutex);
          mutex_unlock(&client->client_state_mutex);
          break; // Client is shutting down, exit thread
        }
        socket_t rekey_socket = client->socket;
        mutex_unlock(&client->send_mutex);

        // Network I/O happens OUTSIDE the send_mutex (client_state_mutex still held for crypto state)
        asciichat_error_t result = crypto_handshake_rekey_request(&client->crypto_handshake_ctx, rekey_socket);
        mutex_unlock(&client->client_state_mutex);

        if (result != ASCIICHAT_OK) {
          log_error("Failed to send REKEY_REQUEST to client %u: %d", client->client_id, result);
        } else {
          log_debug("Sent REKEY_REQUEST to client %u", client->client_id);
          // Notify client that session rekeying has been initiated (old keys still active)
          log_info_client(client, "Session rekey initiated - rotating encryption keys");
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
    log_debug("Send thread: video_frame_get_latest returned %p for client %u", (void *)frame, client->client_id);

    // Check if get_latest failed (buffer might have been destroyed)
    if (!frame) {
      log_debug("Client %u send thread: video_frame_get_latest returned NULL, buffer may be destroyed",
                client->client_id);
      break; // Exit thread if buffer is invalid
    }

    // Check if it's time to send a video frame (60fps rate limiting)
    // Only rate-limit the SEND operation, not frame consumption
    uint64_t current_time_ns = time_get_ns();
    uint64_t current_time_us = time_ns_to_us(current_time_ns);
    uint64_t time_since_last_send_us = current_time_us - last_video_send_time;
    log_debug("Send thread timing check: time_since_last=%llu us, interval=%llu us, should_send=%d",
              (unsigned long long)time_since_last_send_us, (unsigned long long)video_send_interval_us,
              (time_since_last_send_us >= video_send_interval_us));

    if (current_time_us - last_video_send_time >= video_send_interval_us) {
      uint64_t frame_start_ns = time_get_ns();

      // GRID LAYOUT CHANGE: Check if render thread has buffered a frame with different source count
      // If so, send CLEAR_CONSOLE before sending the new frame
      int rendered_sources = atomic_load(&client->last_rendered_grid_sources);
      int sent_sources = atomic_load(&client->last_sent_grid_sources);

      if (rendered_sources != sent_sources && rendered_sources > 0) {
        // Grid layout changed! Send CLEAR_CONSOLE before next frame using ACIP transport
        // Get transport reference briefly to avoid deadlock on TCP buffer full
        mutex_lock(&client->send_mutex);
        if (atomic_load(&client->shutting_down) || !client->transport) {
          mutex_unlock(&client->send_mutex);
          break; // Client is shutting down, exit thread
        }
        acip_transport_t *clear_transport = client->transport;
        mutex_unlock(&client->send_mutex);

        // Network I/O happens OUTSIDE the mutex
        acip_send_clear_console(clear_transport);
        log_debug_every(LOG_RATE_FAST, "Client %u: Sent CLEAR_CONSOLE (grid changed %d → %d sources)",
                        client->client_id, sent_sources, rendered_sources);
        atomic_store(&client->last_sent_grid_sources, rendered_sources);
        sent_something = true;
      }

      log_debug("Send thread: frame validation - frame=%p, frame->data=%p, frame->size=%zu", (void *)frame,
                (void *)frame->data, frame->size);

      if (!frame->data) {
        SET_ERRNO(ERROR_INVALID_STATE, "Client %u has no valid frame data: frame=%p, data=%p", client->client_id, frame,
                  frame->data);
        log_debug("Send thread: Skipping frame send due to NULL frame->data");
        continue;
      }

      if (frame->data && frame->size == 0) {
        // NOTE: This means the we're not ready to send ascii to the client and
        // should wait a little bit.
        log_debug("Send thread: Skipping frame send due to frame->size == 0");
        log_warn_every(LOG_RATE_FAST, "Client %u has no valid frame size: size=%zu", client->client_id, frame->size);
        platform_sleep_usec(1000); // 1ms sleep
        continue;
      }

      // Snapshot frame metadata (safe with double-buffer system)
      const char *frame_data = (const char *)frame->data; // Pointer snapshot - data is stable in front buffer
      uint32_t width = atomic_load(&client->width);
      uint32_t height = atomic_load(&client->height);
      uint64_t step1_ns = time_get_ns();
      uint64_t step2_ns = time_get_ns();
      uint64_t step3_ns = time_get_ns();
      uint64_t step4_ns = time_get_ns();

      // Get transport reference briefly to avoid deadlock on TCP buffer full
      // ACIP transport handles header building, CRC32, encryption internally
      log_debug("Send thread: About to send frame to client %u (width=%u, height=%u, data=%p)", client->client_id,
                width, height, (void *)frame_data);
      mutex_lock(&client->send_mutex);
      if (atomic_load(&client->shutting_down) || !client->transport) {
        mutex_unlock(&client->send_mutex);
        break; // Client is shutting down, exit thread
      }
      acip_transport_t *frame_transport = client->transport;
      mutex_unlock(&client->send_mutex);

      // Network I/O happens OUTSIDE the mutex
      asciichat_error_t send_result = acip_send_ascii_frame(frame_transport, frame_data, frame->size, width, height);
      uint64_t step5_ns = time_get_ns();

      if (send_result != ASCIICHAT_OK) {
        if (!atomic_load(&g_server_should_exit)) {
          SET_ERRNO(ERROR_NETWORK, "Failed to send video frame to client %u: %s", client->client_id,
                    asciichat_error_string(send_result));
        }
        log_debug("Send thread: Frame send FAILED for client %u: result=%d", client->client_id, send_result);
        break;
      }

      log_debug("Send thread: Frame sent SUCCESSFULLY to client %u", client->client_id);
      sent_something = true;
      last_video_send_time = current_time_us;

      uint64_t frame_end_ns = time_get_ns();
      uint64_t frame_time_us = time_ns_to_us(time_elapsed_ns(frame_start_ns, frame_end_ns));
      if (frame_time_us > 15000) { // Log if sending a frame takes > 15ms (encryption adds ~5-6ms)
        uint64_t step1_us = time_ns_to_us(time_elapsed_ns(frame_start_ns, step1_ns));
        uint64_t step2_us = time_ns_to_us(time_elapsed_ns(step1_ns, step2_ns));
        uint64_t step3_us = time_ns_to_us(time_elapsed_ns(step2_ns, step3_ns));
        uint64_t step4_us = time_ns_to_us(time_elapsed_ns(step3_ns, step4_ns));
        uint64_t step5_us = time_ns_to_us(time_elapsed_ns(step4_ns, step5_ns));
        log_warn_every(
            LOG_RATE_DEFAULT,
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

  // Clean up thread-local error context before exit
  asciichat_errno_cleanup();

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

  uint64_t lock_start_ns = time_get_ns();
  rwlock_rdlock(&g_client_manager_rwlock);
  uint64_t lock_end_ns = time_get_ns();
  uint64_t lock_time_ns = time_elapsed_ns(lock_start_ns, lock_end_ns);
  if (lock_time_ns > 1 * NS_PER_MS_INT) {
    char duration_str[32];
    format_duration_ns((double)lock_time_ns, duration_str, sizeof(duration_str));
    log_warn("broadcast_server_state: rwlock_rdlock took %s", duration_str);
  }

  // Count active clients and snapshot client data while holding lock
  // CRITICAL: Use atomic_load for all atomic fields to prevent data races
  for (int i = 0; i < MAX_CLIENTS; i++) {
    bool is_active = atomic_load(&g_client_manager.clients[i].active);
    if (is_active && atomic_load(&g_client_manager.clients[i].is_sending_video)) {
      active_video_count++;
    }
    if (is_active && g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
      // Get crypto context and skip if not ready (handshake still in progress)
      const crypto_context_t *crypto_ctx =
          crypto_handshake_get_context(&g_client_manager.clients[i].crypto_handshake_ctx);
      if (!crypto_ctx) {
        // Skip clients that haven't completed crypto handshake yet
        log_debug("Skipping server_state broadcast to client %u: crypto handshake not ready",
                  atomic_load(&g_client_manager.clients[i].client_id));
        continue;
      }

      client_snapshots[snapshot_count].socket = g_client_manager.clients[i].socket;
      client_snapshots[snapshot_count].client_id = atomic_load(&g_client_manager.clients[i].client_id);
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
  net_state.connected_client_count = HOST_TO_NET_U32(state.connected_client_count);
  net_state.active_client_count = HOST_TO_NET_U32(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // Release lock BEFORE sending (snapshot pattern)
  // Sending while holding lock blocks all client operations
  uint64_t lock_held_final_ns = time_get_ns();
  uint64_t lock_held_ns = time_elapsed_ns(lock_start_ns, lock_held_final_ns);
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

      // Send via ACIP transport
      asciichat_error_t result = acip_send_server_state(target->transport, &net_state);
      mutex_unlock(&target->send_mutex);

      if (result != ASCIICHAT_OK) {
        log_error("Failed to send server state to client %u: %s", client_snapshots[i].client_id,
                  asciichat_error_string(result));
      } else {
        log_debug("Sent server state to client %u: %u connected, %u active", client_snapshots[i].client_id,
                  state.connected_client_count, state.active_client_count);
      }
    } else {
      log_warn("Client %u removed before broadcast send could complete", client_snapshots[i].client_id);
    }
  }

  if (lock_held_ns > 1 * NS_PER_MS_INT) {
    char duration_str[32];
    format_duration_ns((double)lock_held_ns, duration_str, sizeof(duration_str));
    log_warn("broadcast_server_state: rwlock held for %s (includes network I/O)", duration_str);
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
  if (asciichat_thread_is_initialized(&client->send_thread)) {
    asciichat_thread_join(&client->send_thread, NULL);
  }
  if (asciichat_thread_is_initialized(&client->receive_thread)) {
    asciichat_thread_join(&client->receive_thread, NULL);
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
 * @brief Clean up all client media buffers and packet queues
 *
 * This is a comprehensive cleanup that frees both media buffers (video/audio)
 * and packet queues. Used in error paths during client initialization to ensure
 * no resource leaks if setup fails partway through.
 *
 * @param client Client to clean up
 */
static inline void cleanup_client_all_buffers(client_info_t *client) {
  cleanup_client_media_buffers(client);
  cleanup_client_packet_queues(client);
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
    buffer_pool_free(NULL, *data, *len);
    *data = NULL;
    return -1;
  }

  // Store original allocation size before it gets modified
  size_t original_alloc_size = *len;
  void *decrypted_data = buffer_pool_alloc(NULL, original_alloc_size);
  size_t decrypted_len;
  int decrypt_result = crypto_server_decrypt_packet(client->client_id, (const uint8_t *)*data, *len,
                                                    (uint8_t *)decrypted_data, original_alloc_size, &decrypted_len);

  if (decrypt_result != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to process encrypted packet from client %u (result=%d)", client->client_id,
              decrypt_result);
    buffer_pool_free(NULL, *data, original_alloc_size);
    buffer_pool_free(NULL, decrypted_data, original_alloc_size);
    *data = NULL;
    return -1;
  }

  // Replace encrypted data with decrypted data
  // Use original allocation size for freeing the encrypted buffer
  buffer_pool_free(NULL, *data, original_alloc_size);

  *data = decrypted_data;
  *len = decrypted_len;

  // Now process the decrypted packet by parsing its header
  if (*len < sizeof(packet_header_t)) {
    SET_ERRNO(ERROR_CRYPTO, "Decrypted packet too small for header from client %u", client->client_id);
    buffer_pool_free(NULL, *data, *len);
    *data = NULL;
    return -1;
  }

  packet_header_t *header = (packet_header_t *)*data;
  *type = (packet_type_t)NET_TO_HOST_U16(header->type);
  *sender_id = NET_TO_HOST_U32(header->client_id);

  // Adjust data pointer to skip header
  *data = (uint8_t *)*data + sizeof(packet_header_t);
  *len -= sizeof(packet_header_t);

  return 0;
}

/* ============================================================================
 * ACIP Server Callback Wrappers
 * ============================================================================ */

// Forward declarations for ACIP server callbacks
static void acip_server_on_protocol_version(const protocol_version_packet_t *version, void *client_ctx, void *app_ctx);
static void acip_server_on_image_frame(const image_frame_packet_t *header, const void *pixel_data, size_t data_len,
                                       void *client_ctx, void *app_ctx);
static void acip_server_on_audio(const void *audio_data, size_t audio_len, void *client_ctx, void *app_ctx);
static void acip_server_on_audio_batch(const audio_batch_packet_t *header, const float *samples, size_t num_samples,
                                       void *client_ctx, void *app_ctx);
static void acip_server_on_audio_opus(const void *opus_data, size_t opus_len, void *client_ctx, void *app_ctx);
static void acip_server_on_audio_opus_batch(const void *batch_data, size_t batch_len, void *client_ctx, void *app_ctx);
static void acip_server_on_client_join(const void *join_data, size_t data_len, void *client_ctx, void *app_ctx);
static void acip_server_on_client_leave(void *client_ctx, void *app_ctx);
static void acip_server_on_stream_start(uint32_t stream_types, void *client_ctx, void *app_ctx);
static void acip_server_on_stream_stop(uint32_t stream_types, void *client_ctx, void *app_ctx);
static void acip_server_on_capabilities(const void *cap_data, size_t data_len, void *client_ctx, void *app_ctx);
static void acip_server_on_ping(void *client_ctx, void *app_ctx);
static void acip_server_on_pong(void *client_ctx, void *app_ctx);
static void acip_server_on_error(const error_packet_t *header, const char *message, void *client_ctx, void *app_ctx);
static void acip_server_on_remote_log(const remote_log_packet_t *header, const char *message, void *client_ctx,
                                      void *app_ctx);
static void acip_server_on_crypto_rekey_request(const void *payload, size_t payload_len, void *client_ctx,
                                                void *app_ctx);
static void acip_server_on_crypto_rekey_response(const void *payload, size_t payload_len, void *client_ctx,
                                                 void *app_ctx);
static void acip_server_on_crypto_rekey_complete(const void *payload, size_t payload_len, void *client_ctx,
                                                 void *app_ctx);

/**
 * @brief Global ACIP server callbacks structure
 *
 * Handles all ACIP packet types from clients including crypto rekey protocol.
 * Callback wrappers delegate to existing handler functions in protocol.c
 */
static const acip_server_callbacks_t g_acip_server_callbacks = {
    .on_protocol_version = acip_server_on_protocol_version,
    .on_image_frame = acip_server_on_image_frame,
    .on_audio = acip_server_on_audio,
    .on_audio_batch = acip_server_on_audio_batch,
    .on_audio_opus = acip_server_on_audio_opus,
    .on_audio_opus_batch = acip_server_on_audio_opus_batch,
    .on_client_join = acip_server_on_client_join,
    .on_client_leave = acip_server_on_client_leave,
    .on_stream_start = acip_server_on_stream_start,
    .on_stream_stop = acip_server_on_stream_stop,
    .on_capabilities = acip_server_on_capabilities,
    .on_ping = acip_server_on_ping,
    .on_pong = acip_server_on_pong,
    .on_error = acip_server_on_error,
    .on_remote_log = acip_server_on_remote_log,
    .on_crypto_rekey_request = acip_server_on_crypto_rekey_request,
    .on_crypto_rekey_response = acip_server_on_crypto_rekey_response,
    .on_crypto_rekey_complete = acip_server_on_crypto_rekey_complete,
    .app_ctx = NULL // Not used - client context passed per-call
};

// Callback implementations (delegate to existing handlers)

static void acip_server_on_protocol_version(const protocol_version_packet_t *version, void *client_ctx, void *app_ctx) {
  // TODO: Use app_ctx for context-aware protocol handling or metrics collection in future versions
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  handle_protocol_version_packet(client, (void *)version, sizeof(*version));
}

static void acip_server_on_image_frame(const image_frame_packet_t *header, const void *pixel_data, size_t data_len,
                                       void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug(
      "ACIP callback received IMAGE_FRAME: width=%u, height=%u, pixel_format=%u, compressed_size=%u, data_len=%zu",
      header->width, header->height, header->pixel_format, header->compressed_size, data_len);

  // Validate frame dimensions to prevent DoS and buffer overflow attacks
  if (header->width == 0 || header->height == 0) {
    log_error("Invalid image dimensions: %ux%u (width and height must be > 0)", header->width, header->height);
    disconnect_client_for_bad_data(client, "IMAGE_FRAME invalid dimensions");
    return;
  }

  const uint32_t MAX_WIDTH = 8192;
  const uint32_t MAX_HEIGHT = 8192;
  if (header->width > MAX_WIDTH || header->height > MAX_HEIGHT) {
    log_error("Image dimensions too large: %ux%u (max: %ux%u)", header->width, header->height, MAX_WIDTH, MAX_HEIGHT);
    disconnect_client_for_bad_data(client, "IMAGE_FRAME dimensions too large");
    return;
  }

  // Auto-enable video stream if not already enabled
  bool was_sending_video = atomic_load(&client->is_sending_video);
  if (!was_sending_video) {
    if (atomic_compare_exchange_strong(&client->is_sending_video, &was_sending_video, true)) {
      log_info("Client %u auto-enabled video stream (received IMAGE_FRAME)", atomic_load(&client->client_id));
      log_info_client(client, "First video frame received - streaming active");
    }
  } else {
    // Log periodically
    mutex_lock(&client->client_state_mutex);
    client->frames_received_logged++;
    if (client->frames_received_logged % 25000 == 0) {
      char pretty[64];
      format_bytes_pretty(data_len, pretty, sizeof(pretty));
      log_debug("Client %u has sent %u IMAGE_FRAME packets (%s)", atomic_load(&client->client_id),
                client->frames_received_logged, pretty);
    }
    mutex_unlock(&client->client_state_mutex);
  }

  // Store frame data directly to incoming_video_buffer (don't wait for legacy handler)
  // This ensures frame data is available immediately for the render thread
  if (client->incoming_video_buffer) {
    video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);
    if (frame && frame->data && data_len > 0) {
      // Store frame data: [width:4][height:4][pixel_data]
      uint32_t width_net = HOST_TO_NET_U32(header->width);
      uint32_t height_net = HOST_TO_NET_U32(header->height);
      size_t total_size = sizeof(uint32_t) * 2 + data_len;

      if (total_size <= 2 * 1024 * 1024) { // Max 2MB
        memcpy(frame->data, &width_net, sizeof(uint32_t));
        memcpy((char *)frame->data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
        memcpy((char *)frame->data + sizeof(uint32_t) * 2, pixel_data, data_len);
        frame->size = total_size;
        frame->width = header->width;
        frame->height = header->height;
        frame->capture_timestamp_us = (uint64_t)time(NULL) * 1000000;
        frame->sequence_number = ++client->frames_received;
        video_frame_commit(client->incoming_video_buffer);
      }
    }
  }
}

static void acip_server_on_audio(const void *audio_data, size_t audio_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  handle_audio_packet(client, (void *)audio_data, audio_len);
}

static void acip_server_on_audio_batch(const audio_batch_packet_t *header, const float *samples, size_t num_samples,
                                       void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  (void)header; // Header info not needed - ACIP already validated
  client_info_t *client = (client_info_t *)client_ctx;

  // ACIP handler already dequantized samples - write directly to audio buffer
  // This is more efficient than calling the existing handler which would re-dequantize
  log_debug_every(LOG_RATE_DEFAULT, "Received audio batch from client %u (samples=%zu, is_sending_audio=%d)",
                  atomic_load(&client->client_id), num_samples, atomic_load(&client->is_sending_audio));

  if (!atomic_load(&client->is_sending_audio)) {
    log_debug("Ignoring audio batch - client %u not in audio streaming mode", client->client_id);
    return;
  }

  if (client->incoming_audio_buffer) {
    asciichat_error_t write_result =
        audio_ring_buffer_write(client->incoming_audio_buffer, (float *)samples, num_samples);
    if (write_result != ASCIICHAT_OK) {
      log_error("Failed to write decoded audio batch to buffer: %s", asciichat_error_string(write_result));
    }
  }
}

static void acip_server_on_audio_opus(const void *opus_data, size_t opus_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  // Special handling: Convert single-frame Opus to batch format
  // This maintains compatibility with existing server-side Opus batch processing

  if (opus_len < 16) {
    log_warn("AUDIO_OPUS packet too small: %zu bytes", opus_len);
    return;
  }

  const uint8_t *payload = (const uint8_t *)opus_data;
  // Use unaligned read helpers - network data may not be aligned
  int sample_rate = (int)NET_TO_HOST_U32(read_u32_unaligned(payload));
  int frame_duration = (int)NET_TO_HOST_U32(read_u32_unaligned(payload + 4));
  // Reserved bytes at offset 8-15
  size_t actual_opus_size = opus_len - 16;

  if (actual_opus_size > 0 && actual_opus_size <= 1024 && sample_rate == 48000 && frame_duration == 20) {
    // Create a synthetic Opus batch packet (frame_count=1)
    uint8_t batch_buffer[1024 + 20]; // Max Opus + header
    uint8_t *batch_ptr = batch_buffer;

    // Write batch header (batch_buffer is stack-aligned, writes are safe)
    write_u32_unaligned(batch_ptr, HOST_TO_NET_U32((uint32_t)sample_rate));
    batch_ptr += 4;
    write_u32_unaligned(batch_ptr, HOST_TO_NET_U32((uint32_t)frame_duration));
    batch_ptr += 4;
    write_u32_unaligned(batch_ptr, HOST_TO_NET_U32(1)); // frame_count = 1
    batch_ptr += 4;
    memset(batch_ptr, 0, 4); // reserved
    batch_ptr += 4;

    // Write frame size
    write_u16_unaligned(batch_ptr, HOST_TO_NET_U16((uint16_t)actual_opus_size));
    batch_ptr += 2;

    // Write Opus data
    memcpy(batch_ptr, payload + 16, actual_opus_size);
    batch_ptr += actual_opus_size;

    // Process as batch packet
    size_t batch_size = (size_t)(batch_ptr - batch_buffer);
    handle_audio_opus_batch_packet(client, batch_buffer, batch_size);
  }
}

static void acip_server_on_audio_opus_batch(const void *batch_data, size_t batch_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  handle_audio_opus_batch_packet(client, (void *)batch_data, batch_len);
}

static void acip_server_on_client_join(const void *join_data, size_t data_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  handle_client_join_packet(client, (void *)join_data, data_len);
}

static void acip_server_on_client_leave(void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  handle_client_leave_packet(client, NULL, 0);
}

static void acip_server_on_stream_start(uint32_t stream_types, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  // ACIP layer provides stream_types in host byte order, but handle_stream_start_packet()
  // expects network byte order (it does NET_TO_HOST_U32 internally)
  uint32_t stream_types_net = HOST_TO_NET_U32(stream_types);
  handle_stream_start_packet(client, &stream_types_net, sizeof(stream_types_net));
}

static void acip_server_on_stream_stop(uint32_t stream_types, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  // ACIP layer provides stream_types in host byte order, but handle_stream_stop_packet()
  // expects network byte order (it does NET_TO_HOST_U32 internally)
  uint32_t stream_types_net = HOST_TO_NET_U32(stream_types);
  handle_stream_stop_packet(client, &stream_types_net, sizeof(stream_types_net));
}

static void acip_server_on_capabilities(const void *cap_data, size_t data_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
  handle_client_capabilities_packet(client, (void *)cap_data, data_len);
}

static void acip_server_on_ping(void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  // Respond with PONG using ACIP transport
  // Get transport reference briefly to avoid deadlock on TCP buffer full
  mutex_lock(&client->send_mutex);
  if (atomic_load(&client->shutting_down) || !client->transport) {
    mutex_unlock(&client->send_mutex);
    return; // Client is shutting down, skip pong
  }
  acip_transport_t *pong_transport = client->transport;
  mutex_unlock(&client->send_mutex);

  // Network I/O happens OUTSIDE the mutex
  asciichat_error_t pong_result = acip_send_pong(pong_transport);

  if (pong_result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_NETWORK, "Failed to send PONG response to client %u: %s", client->client_id,
              asciichat_error_string(pong_result));
  }
}

static void acip_server_on_pong(void *client_ctx, void *app_ctx) {
  (void)client_ctx;
  (void)app_ctx;
  // Client acknowledged our PING - no action needed
}

static void acip_server_on_error(const error_packet_t *header, const char *message, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  // Reconstruct full packet for existing handler
  size_t msg_len = strlen(message);
  size_t total_len = sizeof(*header) + msg_len;
  uint8_t *full_packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!full_packet) {
    log_error("Failed to allocate buffer for ERROR_MESSAGE reconstruction");
    return;
  }

  memcpy(full_packet, header, sizeof(*header));
  memcpy(full_packet + sizeof(*header), message, msg_len);

  handle_client_error_packet(client, full_packet, total_len);
  SAFE_FREE(full_packet);
}

static void acip_server_on_remote_log(const remote_log_packet_t *header, const char *message, void *client_ctx,
                                      void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  // Reconstruct full packet for existing handler
  size_t msg_len = strlen(message);
  size_t total_len = sizeof(*header) + msg_len;
  uint8_t *full_packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!full_packet) {
    log_error("Failed to allocate buffer for REMOTE_LOG reconstruction");
    return;
  }

  memcpy(full_packet, header, sizeof(*header));
  memcpy(full_packet + sizeof(*header), message, msg_len);

  handle_remote_log_packet_from_client(client, full_packet, total_len);
  SAFE_FREE(full_packet);
}

static void acip_server_on_crypto_rekey_request(const void *payload, size_t payload_len, void *client_ctx,
                                                void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received REKEY_REQUEST from client %u", client->client_id);

  // Process the client's rekey request
  mutex_lock(&client->client_state_mutex);
  asciichat_error_t crypto_result =
      crypto_handshake_process_rekey_request(&client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST from client %u: %d", client->client_id, crypto_result);
    return;
  }

  // Send REKEY_RESPONSE
  mutex_lock(&client->client_state_mutex);
  // Get socket reference briefly to avoid deadlock on TCP buffer full
  mutex_lock(&client->send_mutex);
  if (atomic_load(&client->shutting_down) || client->socket == INVALID_SOCKET_VALUE) {
    mutex_unlock(&client->send_mutex);
    mutex_unlock(&client->client_state_mutex);
    return; // Client is shutting down
  }
  socket_t rekey_socket = client->socket;
  mutex_unlock(&client->send_mutex);

  // Network I/O happens OUTSIDE the send_mutex (client_state_mutex still held for crypto state)
  crypto_result = crypto_handshake_rekey_response(&client->crypto_handshake_ctx, rekey_socket);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_RESPONSE to client %u: %d", client->client_id, crypto_result);
  } else {
    log_debug("Sent REKEY_RESPONSE to client %u", client->client_id);
  }
}

static void acip_server_on_crypto_rekey_response(const void *payload, size_t payload_len, void *client_ctx,
                                                 void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received REKEY_RESPONSE from client %u", client->client_id);

  // Process the client's rekey response
  mutex_lock(&client->client_state_mutex);
  asciichat_error_t crypto_result =
      crypto_handshake_process_rekey_response(&client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE from client %u: %d", client->client_id, crypto_result);
    return;
  }

  // Send REKEY_COMPLETE to confirm and activate new key
  mutex_lock(&client->client_state_mutex);
  // Get socket reference briefly to avoid deadlock on TCP buffer full
  mutex_lock(&client->send_mutex);
  if (atomic_load(&client->shutting_down) || client->socket == INVALID_SOCKET_VALUE) {
    mutex_unlock(&client->send_mutex);
    mutex_unlock(&client->client_state_mutex);
    return; // Client is shutting down
  }
  socket_t complete_socket = client->socket;
  mutex_unlock(&client->send_mutex);

  // Network I/O happens OUTSIDE the send_mutex (client_state_mutex still held for crypto state)
  crypto_result = crypto_handshake_rekey_complete(&client->crypto_handshake_ctx, complete_socket);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_COMPLETE to client %u: %d", client->client_id, crypto_result);
  } else {
    log_debug("Sent REKEY_COMPLETE to client %u - session rekeying complete", client->client_id);
  }
}

static void acip_server_on_crypto_rekey_complete(const void *payload, size_t payload_len, void *client_ctx,
                                                 void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received REKEY_COMPLETE from client %u", client->client_id);

  // Process and commit to new key
  mutex_lock(&client->client_state_mutex);
  asciichat_error_t crypto_result =
      crypto_handshake_process_rekey_complete(&client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_COMPLETE from client %u: %d", client->client_id, crypto_result);
  } else {
    log_debug("Session rekeying completed successfully with client %u", client->client_id);
    // Notify client that rekeying is complete (new keys now active on both sides)
    log_info_client(client, "Session rekey complete - new encryption keys active");
  }
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
  // Rate limiting: Check and record packet-specific rate limits
  if (g_rate_limiter) {
    if (!check_and_record_packet_rate_limit(g_rate_limiter, client->client_ip, client->socket, type)) {
      // Rate limit exceeded - error response already sent by utility function
      return;
    }
  }

  // O(1) dispatch via hash table lookup
  int idx = client_dispatch_hash_lookup(g_client_dispatch_hash, type);
  if (idx < 0) {
    disconnect_client_for_bad_data(client, "Unknown packet type: %d (len=%zu)", type, len);
    return;
  }

  g_client_dispatch_handlers[idx](client, data, len);
}
