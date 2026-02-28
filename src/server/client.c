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
 * Synchronization Patterns:
 * =========================
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
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/discovery/nouns.h>
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
#include <ascii-chat/uthash.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/log.h>

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

/**
 * @brief Hash table entry for client packet dispatch
 */
typedef struct {
  packet_type_t key;   ///< Packet type (0 = empty slot)
  uint8_t handler_idx; ///< Handler index (0-based)
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
    [8]  = {PACKET_TYPE_CLIENT_CAPABILITIES,   8},   // hash(5000)=8
    [9]  = {PACKET_TYPE_PING,                  9},   // hash(5001)=9
    [10] = {PACKET_TYPE_PONG,                  10},  // hash(5002)=10
    [11] = {PACKET_TYPE_CLIENT_JOIN,           4},   // hash(5003)=11
    [12] = {PACKET_TYPE_CLIENT_LEAVE,          5},   // hash(5004)=12
    [13] = {PACKET_TYPE_STREAM_START,          6},   // hash(5005)=13
    [14] = {PACKET_TYPE_STREAM_STOP,           7},   // hash(5006)=14
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
  const char *client_id = client ? client->client_id : "unknown";

  if (parse_result != ASCIICHAT_OK) {
    log_warn("Failed to parse error packet from client %s: %s", client_id, asciichat_error_string(parse_result));
    return;
  }

  log_error("Client %s reported error %d (%s): %s", client_id, reported_error, asciichat_error_string(reported_error),
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
void *client_dispatch_thread(void *arg);          ///< Async dispatch thread for WebRTC clients
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
client_info_t *find_client_by_id(const char *client_id) {
  if (!client_id || client_id[0] == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client ID");
    return NULL;
  }

  // Protect uthash lookup with read lock to prevent concurrent access issues
  rwlock_rdlock(&g_client_manager_rwlock);

  client_info_t *result = NULL;
  HASH_FIND_STR(g_client_manager.clients_by_id, client_id, result);

  rwlock_rdunlock(&g_client_manager_rwlock);

  if (!result) {
    log_warn("Client not found for ID %s", client_id);
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
static void configure_client_socket(socket_t socket, const char *client_id) {
  // Enable TCP keepalive to detect dead connections
  asciichat_error_t keepalive_result = set_socket_keepalive(socket);
  if (keepalive_result != ASCIICHAT_OK) {
    log_warn("Failed to set socket keepalive for client %s: %s", client_id, asciichat_error_string(keepalive_result));
  }

  // Set socket buffer sizes for large data transmission
  const int SOCKET_SEND_BUFFER_SIZE = 1024 * 1024; // 1MB send buffer
  const int SOCKET_RECV_BUFFER_SIZE = 1024 * 1024; // 1MB receive buffer

  if (socket_setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &SOCKET_SEND_BUFFER_SIZE, sizeof(SOCKET_SEND_BUFFER_SIZE)) < 0) {
    log_warn("Failed to set send buffer size for client %s: %s", client_id, network_error_string());
  }

  if (socket_setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &SOCKET_RECV_BUFFER_SIZE, sizeof(SOCKET_RECV_BUFFER_SIZE)) < 0) {
    log_warn("Failed to set receive buffer size for client %s: %s", client_id, network_error_string());
  }

  // Enable TCP_NODELAY to reduce latency for large packets (disables Nagle algorithm)
  const int TCP_NODELAY_VALUE = 1;
  if (socket_setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &TCP_NODELAY_VALUE, sizeof(TCP_NODELAY_VALUE)) < 0) {
    log_warn("Failed to set TCP_NODELAY for client %s: %s", client_id, network_error_string());
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

  const char *client_id = client->client_id;
  log_info("★ START_CLIENT_THREADS: client_id=%s is_tcp=%d (about to create %s threads)", client_id, is_tcp,
           is_tcp ? "TCP" : "WebRTC/WebSocket");
  char thread_name[64];
  asciichat_error_t result;

  // Step 0: Send initial server state BEFORE spawning receive thread
  // This prevents use-after-free if the receive thread immediately errors and calls remove_client()
  if (send_server_state_to_client(client) != 0) {
    log_warn("Failed to send initial server state to client %s", client_id);
  }

  // Get current client count for state packet
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
    log_warn("Failed to send initial server state to client %s: %s", client_id, asciichat_error_string(packet_result));
  } else {
    log_debug("Sent initial server state to client %s: %u connected clients", client_id, state.connected_client_count);
  }

  // Step 1: Create receive thread
  if (is_tcp) {
    safe_snprintf(thread_name, sizeof(thread_name), "receive_%s", client->client_id);
    result =
        tcp_server_spawn_thread(server_ctx->tcp_server, client->socket, client_receive_thread, client, 1, thread_name);
  } else {
    safe_snprintf(thread_name, sizeof(thread_name), "receive_%s", client->client_id);
    log_debug("THREAD_CREATE: WebRTC client %s", client->client_id);
    log_debug("  client=%p, func=%p, &receive_thread=%p", (void *)client, (void *)client_receive_thread,
              (void *)&client->receive_thread);
    log_debug("  Pre-create: receive_thread value=%p", (void *)(uintptr_t)client->receive_thread);
    result = asciichat_thread_create(&client->receive_thread, thread_name, client_receive_thread, client);
    log_debug("  Post-create: result=%d, receive_thread value=%p", result, (void *)(uintptr_t)client->receive_thread);
  }

  if (result != ASCIICHAT_OK) {
    log_error("Failed to create receive thread for %s client %s: %s", is_tcp ? "TCP" : "WebRTC", client_id,
              asciichat_error_string(result));
    remove_client(server_ctx, client_id);
    return -1;
  }
  log_debug("Created receive thread for %s client %s", is_tcp ? "TCP" : "WebRTC", client_id);

  // Step 1b: Create async dispatch thread (processes queued packets)
  // This decouples receive from dispatch to prevent backpressure on the socket
  if (!is_tcp) { // Only for WebRTC/WebSocket clients that need async dispatch
    safe_snprintf(thread_name, sizeof(thread_name), "dispatch_%s", client->client_id);
    atomic_store(&client->dispatch_thread_running, true);
    result = asciichat_thread_create(&client->dispatch_thread, thread_name, client_dispatch_thread, client);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to create dispatch thread for client %s: %s", client->client_id,
                asciichat_error_string(result));
      remove_client(server_ctx, client->client_id);
      return -1;
    }
    log_debug("Created async dispatch thread for client %s", client->client_id);
  }

  // Step 2: Create render threads BEFORE send thread
  // This ensures the render threads generate the first frame before the send thread tries to read it
  log_info("★★★ CLIENT SETUP: About to create render threads for client %s", client->client_id);
  if (create_client_render_threads(server_ctx, client) != 0) {
    log_error("Failed to create render threads for client %s", client->client_id);
    remove_client(server_ctx, client->client_id);
    return -1;
  }
  log_info("★★★ CLIENT SETUP: Successfully created render threads for client %s", client->client_id);

  // Step 3: Create send thread AFTER render threads are running
  if (is_tcp) {
    safe_snprintf(thread_name, sizeof(thread_name), "send_%s", client->client_id);
    result = tcp_server_spawn_thread(server_ctx->tcp_server, client->socket, client_send_thread_func, client, 3,
                                     thread_name);
  } else {
    safe_snprintf(thread_name, sizeof(thread_name), "send_%s", client->client_id);
    result = asciichat_thread_create(&client->send_thread, thread_name, client_send_thread_func, client);
  }

  if (result != ASCIICHAT_OK) {
    log_error("Failed to create send thread for %s client %s: %s", is_tcp ? "TCP" : "WebRTC", client_id,
              asciichat_error_string(result));
    remove_client(server_ctx, client_id);
    return -1;
  }
  log_debug("Created send thread for %s client %s", is_tcp ? "TCP" : "WebRTC", client_id);

  // Step 4: Broadcast server state to ALL clients AFTER the new client is fully set up
  broadcast_server_state_to_all_clients();

  return 0;
}

client_info_t *add_client(server_context_t *server_ctx, socket_t socket, const char *client_ip, int port) {
  // Find empty slot WITHOUT holding the global lock
  // We'll re-verify under lock after allocations complete
  int slot = -1;
  int existing_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (slot == -1 && g_client_manager.clients[i].client_id[0] == '\0') {
      slot = i; // Take first available slot
    }
    if (g_client_manager.clients[i].client_id[0] != '\0' && atomic_load(&g_client_manager.clients[i].active)) {
      existing_count++;
    }
  }

  // Quick pre-check before expensive allocations
  if (existing_count >= GET_OPTION(max_clients) || slot == -1) {
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    ssize_t send_result = socket_send(socket, reject_msg, strlen(reject_msg), 0);
    if (send_result < 0) {
      log_warn("Failed to send rejection message to client: %s", SAFE_STRERROR(errno));
    }
    return NULL;
  }

  // NOW acquire the lock early for generating unique client name
  rwlock_wrlock(&g_client_manager_rwlock);

  // Generate unique client ID (noun only, without counter or port)
  char new_client_id[MAX_CLIENT_ID_LEN];
  if (generate_client_id(new_client_id, sizeof(new_client_id)) != 0) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    log_error("Failed to generate unique client ID");
    return NULL;
  }

  rwlock_wrunlock(&g_client_manager_rwlock);

  // DO EXPENSIVE ALLOCATIONS OUTSIDE THE LOCK
  // This prevents blocking frame processing during client initialization
  video_frame_buffer_t *incoming_video_buffer = video_frame_buffer_create(new_client_id);
  if (!incoming_video_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create video buffer for client %s", new_client_id);
    log_error("Failed to create video buffer for client %s", new_client_id);
    return NULL;
  }

  audio_ring_buffer_t *incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  if (!incoming_audio_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for client %s", new_client_id);
    log_error("Failed to create audio buffer for client %s", new_client_id);
    video_frame_buffer_destroy(incoming_video_buffer);
    return NULL;
  }

  packet_queue_t *audio_queue = packet_queue_create_with_pools(500, 1000, false);
  if (!audio_queue) {
    LOG_ERRNO_IF_SET("Failed to create audio queue for client");
    audio_ring_buffer_destroy(incoming_audio_buffer);
    video_frame_buffer_destroy(incoming_video_buffer);
    return NULL;
  }

  video_frame_buffer_t *outgoing_video_buffer = video_frame_buffer_create(new_client_id);
  if (!outgoing_video_buffer) {
    LOG_ERRNO_IF_SET("Failed to create outgoing video buffer for client");
    packet_queue_destroy(audio_queue);
    audio_ring_buffer_destroy(incoming_audio_buffer);
    video_frame_buffer_destroy(incoming_video_buffer);
    return NULL;
  }

  void *send_buffer = SAFE_MALLOC_ALIGNED(MAX_FRAME_BUFFER_SIZE, 64, void *);
  if (!send_buffer) {
    log_error("Failed to allocate send buffer for client %s", new_client_id);
    video_frame_buffer_destroy(outgoing_video_buffer);
    packet_queue_destroy(audio_queue);
    audio_ring_buffer_destroy(incoming_audio_buffer);
    video_frame_buffer_destroy(incoming_video_buffer);
    return NULL;
  }

  // NOW acquire the lock for the critical section: slot assignment + registration
  rwlock_wrlock(&g_client_manager_rwlock);

  // Re-check slot availability under lock (another thread might have taken it)
  if (g_client_manager.clients[slot].client_id[0] != '\0') {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SAFE_FREE(send_buffer);
    video_frame_buffer_destroy(outgoing_video_buffer);
    packet_queue_destroy(audio_queue);
    audio_ring_buffer_destroy(incoming_audio_buffer);
    video_frame_buffer_destroy(incoming_video_buffer);

    const char *reject_msg = "SERVER_FULL: Slot reassigned, try again\n";
    socket_send(socket, reject_msg, strlen(reject_msg), 0);
    return NULL;
  }

  // Now we have exclusive access to the slot - do the actual registration
  client_info_t *client = &g_client_manager.clients[slot];
  memset(client, 0, sizeof(client_info_t));

  client->socket = socket;
  client->is_tcp_client = true;
  SAFE_STRNCPY(client->client_id, new_client_id, sizeof(client->client_id) - 1);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  atomic_store(&client->active, true);
  client->server_ctx = server_ctx; // Store server context for cleanup
  atomic_store(&client->shutting_down, false);
  atomic_store(&client->last_rendered_grid_sources, 0);
  atomic_store(&client->last_sent_grid_sources, 0);
  client->connected_at = time(NULL);

  memset(&client->crypto_handshake_ctx, 0, sizeof(client->crypto_handshake_ctx));
  client->crypto_initialized = false;

  client->pending_packet_type = 0;
  client->pending_packet_payload = NULL;
  client->pending_packet_length = 0;

  // Assign pre-allocated buffers
  client->incoming_video_buffer = incoming_video_buffer;
  client->incoming_audio_buffer = incoming_audio_buffer;
  client->audio_queue = audio_queue;
  client->outgoing_video_buffer = outgoing_video_buffer;
  client->send_buffer = send_buffer;
  client->send_buffer_size = MAX_FRAME_BUFFER_SIZE;

  // Generate unique noun-based client name with transport type and port
  // Note: We're holding the write lock here, so it's safe to iterate existing clients
  if (generate_client_name(client->display_name, sizeof(client->display_name), g_client_manager.clients_by_id, port,
                           true /* is_tcp */) != 0) {
    // Fallback to numeric name if generation fails
    safe_snprintf(client->display_name, sizeof(client->display_name), "client_%u (tcp:%d)", new_client_id, port);
  }

  // Register client with named debug system using the generated name
  NAMED_REGISTER_CLIENT(client, client->display_name);

  log_info("Added new client %s from %s:%d (socket=%d, slot=%d)", new_client_id, client_ip, port, socket, slot);
  log_debug("Client slot assigned: client_id=%s assigned to slot %d, socket=%d", new_client_id, slot, socket);

  // Register socket with tcp_server
  asciichat_error_t reg_result = tcp_server_add_client(server_ctx->tcp_server, socket, client);
  if (reg_result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INTERNAL, "Failed to register client socket with tcp_server");
    log_error("Failed to register client %s socket with tcp_server", new_client_id);
    // Don't unlock here - error_cleanup will do it
    goto error_cleanup;
  }

  g_client_manager.client_count++;
  log_debug("Client count updated: now %d clients (added client_id=%s to slot %d)", g_client_manager.client_count,
            new_client_id, slot);

  HASH_ADD_STR(g_client_manager.clients_by_id, client_id, client);
  log_debug("Added client %s to uthash table", new_client_id);

  rwlock_wrunlock(&g_client_manager_rwlock);

  // Configure socket OUTSIDE lock
  configure_client_socket(socket, new_client_id);

  // Initialize mutexes OUTSIDE lock
  if (mutex_init(&client->client_state_mutex, "client_state") != 0) {
    log_error("Failed to initialize client state mutex for client %s", new_client_id);
    remove_client(server_ctx, new_client_id);
    return NULL;
  }

  if (mutex_init(&client->send_mutex, "client_send") != 0) {
    log_error("Failed to initialize send mutex for client %s", new_client_id);
    remove_client(server_ctx, new_client_id);
    return NULL;
  }

  // Register with audio mixer OUTSIDE lock
  if (g_audio_mixer && client->incoming_audio_buffer) {
    if (mixer_add_source(g_audio_mixer, new_client_id, client->incoming_audio_buffer) < 0) {
      log_warn("Failed to add client %s to audio mixer", new_client_id);
    } else {
#ifdef DEBUG_AUDIO
      log_debug("Added client %s to audio mixer", new_client_id);
#endif
    }
  }

  // Perform crypto handshake before starting threads.
  // This ensures the handshake uses the socket directly without interference from receive thread.
  if (server_crypto_init() == 0) {
    // Set timeout for crypto handshake to prevent indefinite blocking
    // This prevents clients from connecting but never completing the handshake
    const uint64_t HANDSHAKE_TIMEOUT_NS = 30ULL * NS_PER_SEC_INT;
    asciichat_error_t timeout_result = set_socket_timeout(socket, HANDSHAKE_TIMEOUT_NS);
    if (timeout_result != ASCIICHAT_OK) {
      log_warn("Failed to set handshake timeout for client %s: %s", new_client_id,
               asciichat_error_string(timeout_result));
      // Continue anyway - timeout is a safety feature, not critical
    }

    int crypto_result = server_crypto_handshake(client);
    if (crypto_result != 0) {
      log_error("Crypto handshake failed for client %s: %s", new_client_id, network_error_string());
      if (remove_client(server_ctx, new_client_id) != 0) {
        log_error("Failed to remove client after crypto handshake failure");
      }
      return NULL;
    }

    // Clear socket timeout after handshake completes successfully
    // This allows normal operation without timeouts on data transfer
    asciichat_error_t clear_timeout_result = set_socket_timeout(socket, 0);
    if (clear_timeout_result != ASCIICHAT_OK) {
      log_warn("Failed to clear handshake timeout for client %s: %s", new_client_id,
               asciichat_error_string(clear_timeout_result));
      // Continue anyway - we can still communicate even with timeout set
    }

    log_debug("Crypto handshake completed successfully for client %s", new_client_id);

    // Create ACIP transport for protocol-agnostic packet sending
    // The transport wraps the socket with encryption context from the handshake
    const crypto_context_t *crypto_ctx = crypto_server_get_context(new_client_id);
    client->transport = acip_tcp_transport_create(new_client_id, socket, (crypto_context_t *)crypto_ctx);
    if (!client->transport) {
      log_error("Failed to create ACIP transport for client %s", new_client_id);
      if (remove_client(server_ctx, new_client_id) != 0) {
        log_error("Failed to remove client after transport creation failure");
      }
      return NULL;
    }
    log_debug("Created ACIP transport for client %s with crypto context", new_client_id);

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
      log_info("Client %s using --no-encrypt mode - processing pending packet type %u", client->client_id,
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
      log_debug("Waiting for initial capabilities packet from client %s", client->client_id);

      // Protect crypto context access with client state mutex
      mutex_lock(&client->client_state_mutex);
      const crypto_context_t *crypto_ctx = crypto_server_get_context(client->client_id);

      // Use per-client crypto state to determine enforcement
      // At this point, handshake is complete, so crypto_initialized=true and handshake is ready
      bool enforce_encryption = !GET_OPTION(no_encrypt) && client->crypto_initialized &&
                                crypto_handshake_is_ready(&client->crypto_handshake_ctx);

      packet_recv_result_t result = receive_packet_secure(socket, (void *)crypto_ctx, enforce_encryption, &envelope);
      mutex_unlock(&client->client_state_mutex);

      if (result != PACKET_RECV_SUCCESS) {
        log_error("Failed to receive initial capabilities packet from client %s: result=%d", client->client_id, result);
        if (envelope.allocated_buffer) {
          buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
        }
        if (remove_client(server_ctx, client->client_id) != 0) {
          log_error("Failed to remove client after crypto handshake failure");
        }
        return NULL;
      }
    }

    if (envelope.type != PACKET_TYPE_CLIENT_CAPABILITIES) {
      log_error("Expected PACKET_TYPE_CLIENT_CAPABILITIES but got packet type %d from client %s", envelope.type,
                client->client_id);
      if (envelope.allocated_buffer) {
        buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
      }
      if (remove_client(server_ctx, client->client_id) != 0) {
        log_error("Failed to remove client after crypto handshake failure");
      }
      return NULL;
    }

    // Process the capabilities packet directly
    log_debug("Processing initial capabilities packet from client %s (from %s)", client->client_id,
              used_pending_packet ? "pending packet" : "network");
    handle_client_capabilities_packet(client, envelope.data, envelope.len);

    // Free the packet data
    if (envelope.allocated_buffer) {
      buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
    }
    log_debug("Successfully received and processed initial capabilities for client %s", client->client_id);
  }

  // Start all client threads in the correct order (unified path for TCP and WebRTC)
  // This creates: receive thread -> render threads -> send thread
  // The render threads MUST be created before send thread to avoid the race condition
  // where send thread reads empty frames before render thread generates the first real frame
  const char *client_id_snapshot = client->client_id;
  if (start_client_threads(server_ctx, client, true) != 0) {
    log_error("Failed to start threads for TCP client %s", client_id_snapshot);
    // Client is already in hash table - use remove_client for proper cleanup
    remove_client(server_ctx, client_id_snapshot);
    return NULL;
  }
  log_debug("Successfully created render threads for client %s", client_id_snapshot);

  // Register client with session_host (for discovery mode support)
  if (server_ctx->session_host) {
    client->session_client_id = session_host_add_client(server_ctx->session_host, socket, client_ip, port);
    if (client->session_client_id == 0) {
      log_warn("Failed to register client %s with session_host", client_id_snapshot);
    } else {
      log_debug("Client %s registered with session_host as %u", client_id_snapshot, client->session_client_id);
    }
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return client;

error_cleanup:
  // Clean up all partially allocated resources
  // NOTE: This label is reached when allocation or initialization fails BEFORE
  // the client is added to the hash table. Don't call remove_client() here.
  cleanup_client_all_buffers(client);
  rwlock_wrunlock(&g_client_manager_rwlock);
  return NULL;
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
 * @return Pointer to client_info_t on success, NULL on failure
 *
 * @note The transport must be fully initialized and ready to send/receive
 * @note Client capabilities are still expected as first packet
 */
client_info_t *add_webrtc_client(server_context_t *server_ctx, acip_transport_t *transport, const char *client_ip,
                                 bool start_threads) {
  if (!server_ctx || !transport || !client_ip) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to add_webrtc_client");
    return NULL;
  }

  rwlock_wrlock(&g_client_manager_rwlock);

  // Find empty slot - this is the authoritative check
  int slot = -1;
  int existing_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (slot == -1 && g_client_manager.clients[i].client_id[0] == '\0') {
      slot = i; // Take first available slot
    }
    // Count only active clients
    if (g_client_manager.clients[i].client_id[0] != '\0' && atomic_load(&g_client_manager.clients[i].active)) {
      existing_count++;
    }
  }

  // Check if we've hit the configured max-clients limit (not the array size)
  if (existing_count >= GET_OPTION(max_clients)) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Maximum client limit reached (%d/%d active clients)", existing_count,
              GET_OPTION(max_clients));
    log_error("Maximum client limit reached (%d/%d active clients)", existing_count, GET_OPTION(max_clients));
    return NULL;
  }

  if (slot == -1) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "No available client slots (all %d array slots are in use)", MAX_CLIENTS);
    log_error("No available client slots (all %d array slots are in use)", MAX_CLIENTS);
    return NULL;
  }

  // Update client_count to match actual count before adding new client
  g_client_manager.client_count = existing_count;

  // Generate unique client ID for WebRTC client (noun only, without counter or port)
  char new_client_id[MAX_CLIENT_ID_LEN];
  if (generate_client_id(new_client_id, sizeof(new_client_id)) != 0) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    log_error("Failed to generate unique client ID for WebRTC client");
    return NULL;
  }

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];

  // Free any existing buffers from previous client in this slot
  if (client->incoming_video_buffer) {
    video_frame_buffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
  }
  if (client->outgoing_video_buffer) {
    video_frame_buffer_destroy(client->outgoing_video_buffer);
    client->outgoing_video_buffer = NULL;
  }
  if (client->incoming_audio_buffer) {
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_audio_buffer = NULL;
  }
  if (client->send_buffer) {
    SAFE_FREE(client->send_buffer);
    client->send_buffer = NULL;
  }

  memset(client, 0, sizeof(client_info_t));

  // Set up WebRTC-specific fields
  client->socket = INVALID_SOCKET_VALUE; // WebRTC has no traditional socket
  client->is_tcp_client = false;         // WebRTC client - threads managed directly
  client->transport = transport;         // Use provided transport
  SAFE_STRNCPY(client->client_id, new_client_id, sizeof(client->client_id) - 1);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = 0; // WebRTC doesn't use port numbers
  atomic_store(&client->active, true);
  client->server_ctx = server_ctx; // Store server context for receive thread cleanup
  log_info("Added new WebRTC client %s from %s (transport=%p, slot=%d)", new_client_id, client_ip, transport, slot);
  atomic_store(&client->shutting_down, false);
  atomic_store(&client->last_rendered_grid_sources, 0); // Render thread updates this
  atomic_store(&client->last_sent_grid_sources, 0);     // Send thread updates this
  log_debug("WebRTC client slot assigned: client_id=%s assigned to slot %d", client->client_id, slot);
  client->connected_at = time(NULL);

  // Initialize crypto context for this client
  memset(&client->crypto_handshake_ctx, 0, sizeof(client->crypto_handshake_ctx));
  log_debug("[ADD_WEBRTC_CLIENT] Setting crypto_initialized=false (will be set to true after handshake completes)");
  // Do NOT set crypto_initialized = true here! The handshake must complete first.
  // For WebSocket clients: websocket_client_handler will perform the handshake
  // For WebRTC clients: ACDS signaling may have already done crypto (future enhancement)
  client->crypto_initialized = false;

  // Initialize pending packet storage (unused for WebRTC, but keep for consistency)
  client->pending_packet_type = 0;
  client->pending_packet_payload = NULL;
  client->pending_packet_length = 0;

  // Use noun-based client name for display_name as well
  SAFE_STRNCPY(client->display_name, new_client_id, sizeof(client->display_name) - 1);

  // Create individual video buffer for this client using modern double-buffering
  client->incoming_video_buffer = video_frame_buffer_create(new_client_id);
  if (!client->incoming_video_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create video buffer for WebRTC client %s", new_client_id);
    log_error("Failed to create video buffer for WebRTC client %s", new_client_id);
    goto error_cleanup_webrtc;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  if (!client->incoming_audio_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for WebRTC client %s", new_client_id);
    log_error("Failed to create audio buffer for WebRTC client %s", new_client_id);
    goto error_cleanup_webrtc;
  }

  // Create packet queues for outgoing data
  client->audio_queue = packet_queue_create_with_pools(500, 1000, false);
  if (!client->audio_queue) {
    LOG_ERRNO_IF_SET("Failed to create audio queue for WebRTC client");
    goto error_cleanup_webrtc;
  }

  // Create packet queue for async dispatch: received packets waiting to be processed
  // Capacity of 100 packets allows buffering of ~10-50 frames (depending on fragmentation)
  client->received_packet_queue = packet_queue_create_with_pools(100, 500, false);
  if (!client->received_packet_queue) {
    LOG_ERRNO_IF_SET("Failed to create received packet queue for client");
    goto error_cleanup_webrtc;
  }

  // Create outgoing video buffer for ASCII frames (double buffered, no dropping)
  client->outgoing_video_buffer = video_frame_buffer_create(new_client_id);
  if (!client->outgoing_video_buffer) {
    LOG_ERRNO_IF_SET("Failed to create outgoing video buffer for WebRTC client");
    goto error_cleanup_webrtc;
  }

  // Pre-allocate send buffer to avoid malloc/free in send thread (prevents deadlocks)
  client->send_buffer_size = MAX_FRAME_BUFFER_SIZE; // 2MB should handle largest frames
  client->send_buffer = SAFE_MALLOC_ALIGNED(client->send_buffer_size, 64, void *);
  if (!client->send_buffer) {
    log_error("Failed to allocate send buffer for WebRTC client %s", new_client_id);
    goto error_cleanup_webrtc;
  }

  g_client_manager.client_count = existing_count + 1; // We just added a client
  log_debug("Client count updated: now %d clients (added WebRTC client_id=%s to slot %d)",
            g_client_manager.client_count, new_client_id, slot);

  // Add client to uthash table for O(1) lookup
  HASH_ADD_STR(g_client_manager.clients_by_id, client_id, client);
  log_debug("Added WebRTC client %s to uthash table", new_client_id);

  // Release the write lock IMMEDIATELY after adding to hash table
  // All subsequent operations (mutex init, mixer registration) don't need global client manager lock
  rwlock_wrunlock(&g_client_manager_rwlock);

  // Register this client's audio buffer with the mixer
  if (g_audio_mixer && client->incoming_audio_buffer) {
    if (mixer_add_source(g_audio_mixer, new_client_id, client->incoming_audio_buffer) < 0) {
      log_warn("Failed to add WebRTC client %s to audio mixer", new_client_id);
    } else {
#ifdef DEBUG_AUDIO
      log_debug("Added WebRTC client %s to audio mixer", new_client_id);
#endif
    }
  }

  // Initialize mutexes BEFORE creating any threads to prevent race conditions
  if (mutex_init(&client->client_state_mutex, "client_state") != 0) {
    log_error("Failed to initialize client state mutex for WebRTC client %s", new_client_id);
    // Client is already in hash table - use remove_client for proper cleanup
    remove_client(server_ctx, new_client_id);
    return NULL;
  }

  // Initialize send mutex to protect concurrent socket writes
  if (mutex_init(&client->send_mutex, "client_send") != 0) {
    log_error("Failed to initialize send mutex for WebRTC client %s", new_client_id);
    // Client is already in hash table - use remove_client for proper cleanup
    remove_client(server_ctx, client->client_id);
    return NULL;
  }

  // For WebRTC clients, the capabilities packet will be received by the receive thread
  // when it starts. Unlike TCP clients where we handle it synchronously in add_client(),
  // WebRTC uses the transport abstraction which handles packet reception automatically.
  log_debug("WebRTC client %s initialized - receive thread will process capabilities", new_client_id);

  // Conditionally start threads based on caller preference
  // WebSocket handler passes start_threads=false to defer thread startup until after crypto init
  // This ensures receive thread doesn't try to process packets before crypto context is ready
  if (start_threads) {
    log_debug("[ADD_WEBRTC_CLIENT] Starting client threads (receive, render) for client %s...", new_client_id);
    if (start_client_threads(server_ctx, client, false) != 0) {
      log_error("Failed to start threads for WebRTC client %s", new_client_id);
      return NULL;
    }
    log_debug("Created receive thread for WebRTC client %s", new_client_id);
    log_debug("[ADD_WEBRTC_CLIENT] Receive thread started - thread will now be processing packets", new_client_id);
  } else {
    log_debug("[ADD_WEBRTC_CLIENT] Deferring thread startup for client %s (caller will start after crypto init)",
              new_client_id);
  }

  // Send initial server state to the new client
  if (send_server_state_to_client(client) != 0) {
    log_warn("Failed to send initial server state to WebRTC client %s", new_client_id);
  } else {
#ifdef DEBUG_NETWORK
    log_info("Sent initial server state to WebRTC client %s", new_client_id);
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
    log_warn("Failed to send initial server state to WebRTC client %s: %s", new_client_id,
             asciichat_error_string(packet_send_result));
  } else {
    log_debug("Sent initial server state to WebRTC client %s: %u connected clients", new_client_id,
              state.connected_client_count);
  }

  // Register client with session_host (for discovery mode support)
  // WebRTC clients use INVALID_SOCKET_VALUE since they don't have a TCP socket
  if (server_ctx->session_host) {
    client->session_client_id = session_host_add_client(server_ctx->session_host, INVALID_SOCKET_VALUE, client_ip, 0);
    if (client->session_client_id == 0) {
      log_warn("Failed to register WebRTC client %s with session_host", new_client_id);
    } else {
      log_debug("WebRTC client %s registered with session_host as %u", new_client_id, client->session_client_id);
    }
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return client;

error_cleanup_webrtc:
  // Clean up all partially allocated resources for WebRTC client
  // NOTE: This label is reached when allocation or initialization fails BEFORE
  // the client is added to the hash table. Don't call remove_client() here.
  cleanup_client_all_buffers(client);
  rwlock_wrunlock(&g_client_manager_rwlock);
  return NULL;
}

int remove_client(server_context_t *server_ctx, const char *client_id) {
  if (!server_ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Cannot remove client %s: NULL server_ctx", client_id);
    return -1;
  }

  // Phase 1: Mark client inactive and prepare for cleanup while holding write lock
  client_info_t *target_client = NULL;
  char display_name_copy[MAX_DISPLAY_NAME_LEN];
  socket_t client_socket = INVALID_SOCKET_VALUE; // Save socket for thread cleanup

  log_debug("SOCKET_DEBUG: Attempting to remove client %s", client_id);
  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (strcmp(client->client_id, client_id) == 0 && client->client_id[0] != '\0') {
      // Check if already being removed by another thread
      // This prevents double-free and use-after-free crashes during concurrent cleanup
      if (atomic_load(&client->shutting_down)) {
        rwlock_wrunlock(&g_client_manager_rwlock);
        log_debug("Client %s already being removed by another thread, skipping", client_id);
        return 0; // Return success - removal is in progress
      }
      // Mark as shutting down and inactive immediately to stop new operations
      log_debug("Setting active=false in remove_client (client_id=%s, socket=%d)", client_id, client->socket);
      log_info("Removing client %s (socket=%d) - marking inactive and clearing video flags", client_id, client->socket);
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
      // NOTE: Do NOT call socket_shutdown() here - it sends FIN to peer and breaks the connection
      // The threads will exit naturally when they check shutting_down/active flags or hit errors
      mutex_unlock(&client->client_state_mutex);

      // Shutdown packet queues to unblock send thread
      if (client->audio_queue) {
        packet_queue_stop(client->audio_queue);
      }
      // Video now uses double buffer, no queue to shutdown

      break;
    }
  }

  // If client not found, unlock and return
  if (!target_client) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    log_warn("Cannot remove client %s: not found", client_id);
    return -1;
  }

  // Unregister client from session_host (for discovery mode support)
  // NOTE: Client may not be registered if crypto handshake failed before session_host registration
  if (server_ctx->session_host && target_client->session_client_id != 0) {
    asciichat_error_t session_result =
        session_host_remove_client(server_ctx->session_host, target_client->session_client_id);
    if (session_result != ASCIICHAT_OK) {
      // ERROR_NOT_FOUND (91) is expected if client failed crypto before being registered with session_host
      if (session_result == ERROR_NOT_FOUND) {
        log_debug("Client %s not found in session_host (likely failed crypto before registration)", client_id);
      } else {
        log_warn("Failed to unregister client %s from session_host: %s", client_id,
                 asciichat_error_string(session_result));
      }
    } else {
      log_debug("Client %s unregistered from session_host", client_id);
    }
  }

  // Release write lock before joining threads.
  // This prevents deadlock with render threads that need read locks.
  rwlock_wrunlock(&g_client_manager_rwlock);

  // Phase 2: Stop all client threads
  // For TCP clients: use tcp_server thread pool management
  // For WebRTC clients: manually join threads (no socket-based thread pool)
  // Use is_tcp_client flag, not socket value - socket may already be INVALID_SOCKET_VALUE
  // even for TCP clients if it was closed earlier during cleanup.
  log_debug("Stopping all threads for client %s (socket %d, is_tcp=%d)", client_id, client_socket,
            target_client ? target_client->is_tcp_client : -1);

  if (target_client && target_client->is_tcp_client) {
    // TCP client: use tcp_server thread pool
    // This joins threads in stop_id order: receive(1), render(2), send(3)
    // Use saved client_socket for lookup (tcp_server needs original socket as key)
    if (client_socket != INVALID_SOCKET_VALUE) {
      asciichat_error_t stop_result = tcp_server_stop_client_threads(server_ctx->tcp_server, client_socket);
      if (stop_result != ASCIICHAT_OK) {
        log_warn("Failed to stop threads for TCP client %s: error %d", client_id, stop_result);
        // Continue with cleanup even if thread stopping failed
      }
    } else {
      log_debug("TCP client %s socket already closed, threads should have already exited", client_id);
    }
  } else if (target_client) {
    // WebRTC client: manually join threads
    log_debug("Stopping WebRTC client %s threads (receive and send)", client_id);

    // Join receive thread (but skip if called from the receive thread itself to avoid deadlock)
    thread_id_t current_thread_id = asciichat_thread_self();
    if (asciichat_thread_equal(current_thread_id, target_client->receive_thread_id)) {
      log_debug("remove_client() called from receive thread for client %s, skipping self-join", client_id);
    } else {
      void *recv_result = NULL;
      asciichat_error_t recv_join_result = asciichat_thread_join(&target_client->receive_thread, &recv_result);
      if (recv_join_result != ASCIICHAT_OK) {
        log_warn("Failed to join receive thread for WebRTC client %s: error %d", client_id, recv_join_result);
      } else {
        log_debug("Joined receive thread for WebRTC client %s", client_id);
      }
    }

    // Join dispatch thread (BEFORE destroying packet queues)
    if (asciichat_thread_is_initialized(&target_client->dispatch_thread)) {
      atomic_store(&target_client->dispatch_thread_running, false);
      void *dispatch_result = NULL;
      asciichat_error_t dispatch_join_result = asciichat_thread_join(&target_client->dispatch_thread, &dispatch_result);
      if (dispatch_join_result != ASCIICHAT_OK) {
        log_warn("Failed to join dispatch thread for WebRTC client %s: error %d", client_id, dispatch_join_result);
      } else {
        log_debug("Joined dispatch thread for WebRTC client %s", client_id);
      }
    }

    // Join send thread
    void *send_result = NULL;
    asciichat_error_t send_join_result = asciichat_thread_join(&target_client->send_thread, &send_result);
    if (send_join_result != ASCIICHAT_OK) {
      log_warn("Failed to join send thread for WebRTC client %s: error %d", client_id, send_join_result);
    } else {
      log_debug("Joined send thread for WebRTC client %s", client_id);
    }
    // Note: Render threads still need to be stopped - they're created the same way for both TCP and WebRTC
    // For now, render threads are expected to exit when they check g_server_should_exit and client->active
  }

  // Destroy ACIP transport before closing socket
  // For WebSocket clients: LWS_CALLBACK_CLOSED already closed and destroyed the transport
  // Trying to destroy it again causes heap-use-after-free since LWS callbacks might still fire
  // For TCP clients: transport is ours to clean up
  if (target_client && target_client->transport && target_client->is_tcp_client) {
    acip_transport_destroy(target_client->transport);
    target_client->transport = NULL;
    log_debug("Destroyed ACIP transport for TCP client %s", client_id);
  } else if (target_client && target_client->transport && !target_client->is_tcp_client) {
    // WebSocket client - just NULL it out, LWS_CALLBACK_CLOSED already destroyed it
    target_client->transport = NULL;
    log_debug("Skipped transport destruction for WebSocket client %s (LWS already destroyed)", client_id);
  }

  // Now safe to close the socket (threads are stopped)
  if (client_socket != INVALID_SOCKET_VALUE) {
    log_debug("SOCKET_DEBUG: Closing socket %d for client %s after thread cleanup", client_socket, client_id);
    socket_close(client_socket);
  }

  // Phase 3: Clean up resources with write lock
  rwlock_wrlock(&g_client_manager_rwlock);

  // Re-validate target_client pointer after reacquiring lock.
  // Another thread might have invalidated the pointer while we had the lock released.
  if (target_client) {
    // Verify client_id still matches and client is still in shutting_down state
    bool still_shutting_down = atomic_load(&target_client->shutting_down);
    if (strcmp(target_client->client_id, client_id) != 0 || !still_shutting_down) {
      log_warn("Client %s pointer invalidated during thread cleanup (id=%s, shutting_down=%d)", client_id,
               target_client->client_id, still_shutting_down);
      rwlock_wrunlock(&g_client_manager_rwlock);
      return 0; // Another thread completed the cleanup
    }
  }

  // Mark socket as closed in client structure
  if (target_client && target_client->socket != INVALID_SOCKET_VALUE) {
    mutex_lock(&target_client->client_state_mutex);
    target_client->socket = INVALID_SOCKET_VALUE;
    mutex_unlock(&target_client->client_state_mutex);
    log_debug("SOCKET_DEBUG: Client %s socket set to INVALID", target_client->client_id);
  }

  // Use the dedicated cleanup function to ensure all resources are freed
  cleanup_client_all_buffers(target_client);

  // Remove from audio mixer
  if (g_audio_mixer) {
    mixer_remove_source(g_audio_mixer, client_id);
#ifdef DEBUG_AUDIO
    log_debug("Removed client %s from audio mixer", client_id);
#endif
  }

  // Remove from uthash table
  // Verify client is actually in the hash table before deleting.
  // Another thread might have already removed it.
  if (target_client) {
    client_info_t *hash_entry = NULL;
    HASH_FIND_STR(g_client_manager.clients_by_id, target_client->client_id, hash_entry);
    if (hash_entry == target_client) {
      HASH_DELETE(hh, g_client_manager.clients_by_id, target_client);
      log_debug("Removed client %s from uthash table", client_id);
    } else {
      log_warn("Client %s already removed from hash table by another thread (found=%p, expected=%p)", client_id,
               (void *)hash_entry, (void *)target_client);
    }
  } else {
    log_warn("Failed to remove client %s from hash table (client not found)", client_id);
  }

  // Cleanup crypto context for this client
  if (target_client->crypto_initialized) {
    crypto_handshake_destroy(&target_client->crypto_handshake_ctx);
    target_client->crypto_initialized = false;
    log_debug("Crypto context cleaned up for client %s", client_id);
  }

  // Verify all threads have actually exited before resetting client_id.
  // Threads that are still starting (at RtlUserThreadStart) haven't checked client_id yet.
  // We must ensure threads are fully joined before zeroing the client struct.
  // Use exponential backoff for thread termination verification
  int retry_count = 0;
  const int max_retries = 5;
  while (retry_count < max_retries && (asciichat_thread_is_initialized(&target_client->send_thread) ||
                                       asciichat_thread_is_initialized(&target_client->receive_thread) ||
                                       asciichat_thread_is_initialized(&target_client->video_render_thread) ||
                                       asciichat_thread_is_initialized(&target_client->audio_render_thread))) {
    // Exponential backoff: 10ms, 20ms, 40ms, 80ms, 160ms
    uint32_t delay_ms = 10 * (1 << retry_count);
    log_warn("Client %s: Some threads still appear initialized (attempt %d/%d), waiting %ums", client_id,
             retry_count + 1, max_retries, delay_ms);
    platform_sleep_us(delay_ms * 1000);
    retry_count++;
  }

  if (retry_count == max_retries) {
    log_error("Client %s: Threads did not terminate after %d retries, proceeding with cleanup anyway", client_id,
              max_retries);
  }

  // Only reset client_id to 0 AFTER confirming threads are joined
  // This prevents threads that are starting from accessing a zeroed client struct
  // Reset client_id to NULL before destroying mutexes to prevent race conditions.
  // This ensures worker threads can detect shutdown and exit before the mutex is destroyed.
  // If we destroy the mutex first, threads might try to access a destroyed mutex.
  target_client->client_id[0] = '\0'; // Clear the client_id string

  // Wait for threads to observe the client_id reset
  // Use sufficient delay for memory visibility across all CPU cores
  platform_sleep_us(5 * US_PER_MS_INT); // 5ms delay for memory barrier propagation

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
    if (g_client_manager.clients[j].client_id[0] != '\0') {
      remaining_count++;
    }
  }
  g_client_manager.client_count = remaining_count;

  log_debug("Client removed: client_id=%s (%s) removed, remaining clients: %d", client_id, display_name_copy,
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

/**
 * @brief Async dispatch thread - processes queued received packets
 *
 * Decouples receive from dispatch to prevent backpressure on the network socket.
 * Received packets are queued by the receive thread, and processed asynchronously here.
 * This allows the receive thread to keep accepting packets while dispatch processes them.
 */
void *client_dispatch_thread(void *arg) {
  client_info_t *client = (client_info_t *)arg;

  if (!client) {
    log_error("Invalid client info in dispatch thread (NULL pointer)");
    return NULL;
  }

  const char *client_id = client->client_id;
  log_info("DISPATCH_THREAD: Started for client %s", client_id);

  uint64_t dispatch_loop_count = 0;
  uint64_t last_dequeue_attempt = time_get_ns();

  while (!atomic_load(&g_server_should_exit) && atomic_load(&client->dispatch_thread_running)) {
    dispatch_loop_count++;
    // Try to dequeue next packet (non-blocking)
    // Use try_dequeue to avoid blocking - allows checking exit flag frequently
    uint64_t dequeue_start = time_get_ns();
    queued_packet_t *queued_pkt = packet_queue_try_dequeue(client->received_packet_queue);
    uint64_t dequeue_end = time_get_ns();

    if (!queued_pkt) {
      // Queue was empty, sleep briefly to avoid busy-waiting
      log_dev_every(5 * US_PER_MS_INT, "🔄 DISPATCH_LOOP[%llu]: Queue empty after %.1fμs, sleeping 10ms",
                    (unsigned long long)dispatch_loop_count, (dequeue_end - dequeue_start) / 1000.0);
      usleep(10 * US_PER_MS_INT); // 10ms sleep
      last_dequeue_attempt = dequeue_end;
      continue;
    }

    // Frame received! Log it immediately
    char dequeue_elapsed_str[32];
    time_pretty(dequeue_end - dequeue_start, -1, dequeue_elapsed_str, sizeof(dequeue_elapsed_str));
    log_info("✅ DISPATCH_THREAD[%u] DEQUEUED packet: %zu bytes (dequeue took %s)", client_id, queued_pkt->data_len,
             dequeue_elapsed_str);

    // Process the dequeued packet
    // The queued packet contains the complete ACIP packet (header + payload) from websocket_recv()
    const packet_header_t *header = (const packet_header_t *)queued_pkt->data;
    size_t total_len = queued_pkt->data_len;
    uint8_t *payload = (uint8_t *)header + sizeof(packet_header_t);
    size_t payload_len = 0;

    log_info("🔍 DISPATCH_THREAD[%u]: Processing %zu byte packet (header size=%zu)", client_id, total_len,
             sizeof(packet_header_t));

    if (total_len < sizeof(packet_header_t)) {
      log_error("🔴 DISPATCH_THREAD[%u]: Packet too small (%zu < %zu), DROPPING", client_id, total_len,
                sizeof(packet_header_t));
      packet_queue_free_packet(queued_pkt);
      continue;
    }

    {
      packet_type_t packet_type = (packet_type_t)NET_TO_HOST_U16(header->type);
      payload_len = NET_TO_HOST_U32(header->length);

      log_info("🎯 DISPATCH_THREAD[%u]: Packet type=%d, payload_len=%u, total_len=%zu", client_id, packet_type,
               payload_len, total_len);

      // Handle PACKET_TYPE_ENCRYPTED from WebSocket clients that encrypt at application layer
      // This mirrors the decryption logic in acip_server_receive_and_dispatch()
      if (packet_type == PACKET_TYPE_ENCRYPTED && client->transport && client->transport->crypto_ctx) {
        log_info("🔐 DISPATCH_THREAD[%u]: Decrypting PACKET_TYPE_ENCRYPTED", client_id);

        uint8_t *ciphertext = payload;
        size_t ciphertext_len = payload_len;

        // Decrypt to get inner plaintext packet (header + payload)
        size_t plaintext_size = ciphertext_len + 1024;
        uint8_t *plaintext = SAFE_MALLOC(plaintext_size, uint8_t *);
        if (!plaintext) {
          log_error("🔴 DISPATCH_THREAD[%u]: Failed to allocate plaintext buffer for decryption", client_id);
          packet_queue_free_packet(queued_pkt);
          continue;
        }

        size_t plaintext_len;
        crypto_result_t crypto_result = crypto_decrypt(client->transport->crypto_ctx, ciphertext, ciphertext_len,
                                                       plaintext, plaintext_size, &plaintext_len);

        if (crypto_result != CRYPTO_OK) {
          log_error("🔴 DISPATCH_THREAD[%u]: Failed to decrypt packet: %s", client_id,
                    crypto_result_to_string(crypto_result));
          SAFE_FREE(plaintext);
          packet_queue_free_packet(queued_pkt);
          continue;
        }

        if (plaintext_len < sizeof(packet_header_t)) {
          log_error("🔴 DISPATCH_THREAD[%u]: Decrypted packet too small: %zu < %zu", client_id, plaintext_len,
                    sizeof(packet_header_t));
          SAFE_FREE(plaintext);
          packet_queue_free_packet(queued_pkt);
          continue;
        }

        // Parse the inner (decrypted) header
        const packet_header_t *inner_header = (const packet_header_t *)plaintext;
        packet_type = (packet_type_t)NET_TO_HOST_U16(inner_header->type);
        payload_len = NET_TO_HOST_U32(inner_header->length);
        payload = plaintext + sizeof(packet_header_t);

        log_info("🔐 DISPATCH_THREAD[%u]: Decrypted inner packet type=%d, payload_len=%u", client_id, packet_type,
                 payload_len);

        // Dispatch the decrypted packet
        if (client->transport) {
          log_info("🎯 DISPATCH_THREAD[%u]: Calling acip_handle_server_packet(type=%d, payload_len=%u)", client_id,
                   packet_type, payload_len);
          asciichat_error_t dispatch_result = acip_handle_server_packet(client->transport, packet_type, payload,
                                                                        payload_len, client, &g_acip_server_callbacks);

          if (dispatch_result != ASCIICHAT_OK) {
            log_error("🔴 DISPATCH_THREAD[%u]: Handler failed for decrypted packet type=%d: %s", client_id, packet_type,
                      asciichat_error_string(dispatch_result));
          } else {
            log_info("✅ DISPATCH_THREAD[%u]: Successfully dispatched decrypted packet type=%d", client_id,
                     packet_type);
          }
        } else {
          log_error("🔴 DISPATCH_THREAD[%u]: Cannot dispatch decrypted packet - transport is NULL", client_id);
        }

        // Free the decrypted buffer
        SAFE_FREE(plaintext);
      } else {
        // Not encrypted or no crypto context - dispatch as-is
        if (client->transport) {
          log_info("🎯 DISPATCH_THREAD[%u]: Calling acip_handle_server_packet(type=%d, payload_len=%u)", client_id,
                   packet_type, payload_len);
          asciichat_error_t dispatch_result = acip_handle_server_packet(client->transport, packet_type, payload,
                                                                        payload_len, client, &g_acip_server_callbacks);

          if (dispatch_result != ASCIICHAT_OK) {
            log_error("🔴 DISPATCH_THREAD[%u]: Handler failed for packet type=%d: %s", client_id, packet_type,
                      asciichat_error_string(dispatch_result));
          } else {
            log_info("✅ DISPATCH_THREAD[%u]: Successfully dispatched packet type=%d", client_id, packet_type);
          }
        } else {
          log_error("🔴 DISPATCH_THREAD[%u]: Cannot dispatch packet - transport is NULL", client_id);
        }
      }
    }

    // Free the queued packet
    packet_queue_free_packet(queued_pkt);
  }

  log_info("DISPATCH_THREAD: Exiting for client %s", client_id);
  return NULL;
}

void *client_receive_thread(void *arg) {
  // Log thread startup
  log_debug("RECV_THREAD: Thread function entered, arg=%p", arg);

  client_info_t *client = (client_info_t *)arg;

  // Validate client pointer immediately before any access.
  // This prevents crashes if remove_client() has zeroed the client struct
  // while the thread was still starting at RtlUserThreadStart.
  if (!client) {
    log_error("Invalid client info in receive thread (NULL pointer)");
    return NULL;
  }

  // Save this thread's ID so remove_client() can detect self-joins
  client->receive_thread_id = asciichat_thread_self();

  log_debug("RECV_THREAD_DEBUG: Thread started, client=%p, client_id=%s, is_tcp=%d", (void *)client, client->client_id,
            client->is_tcp_client);

  if (atomic_load(&client->protocol_disconnect_requested)) {
    log_debug("Receive thread for client %s exiting before start (protocol disconnect requested)", client->client_id);
    return NULL;
  }

  // Check if client_id is empty (client struct has been zeroed by remove_client)
  // This must be checked BEFORE accessing any client fields
  if (client->client_id[0] == 0) {
    log_debug("Receive thread: client_id is empty, client struct may have been zeroed, exiting");
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

  log_debug("Started receive thread for client %s (%s)", client->client_id, client->display_name);

  // Main receive loop - processes packets from transport
  // For TCP clients: receives from socket
  // For WebRTC clients: receives from transport ringbuffer (via ACDS signaling)

  log_info("RECV_THREAD_LOOP_START: client_id=%s, is_tcp=%d, transport=%p, active=%d", client->client_id,
           client->is_tcp_client, (void *)client->transport, atomic_load(&client->active));

  while (!atomic_load(&g_server_should_exit) && atomic_load(&client->active)) {
    // For TCP clients, check socket validity
    // For WebRTC clients, continue even if no socket (transport handles everything)
    if (client->is_tcp_client && client->socket == INVALID_SOCKET_VALUE) {
      log_debug("TCP client %s has invalid socket, exiting receive thread", client->client_id);
      break;
    }

    // Check client_id is still valid before accessing transport.
    // This prevents accessing freed memory if remove_client() has zeroed the client struct.
    if (client->client_id[0] == 0) {
      log_debug("Client client_id reset, exiting receive thread");
      break;
    }

    // Receive packet (without dispatching) - decouple from dispatch for async processing
    // For WebRTC clients with async dispatch, we queue packets instead of processing immediately
    // This prevents backpressure on the network socket

    if (client->is_tcp_client) {
      // TCP clients: use original synchronous dispatch
      asciichat_error_t acip_result =
          acip_server_receive_and_dispatch(client->transport, client, &g_acip_server_callbacks);

      // Check if shutdown was requested during the network call
      if (atomic_load(&g_server_should_exit)) {
        log_debug("RECV_EXIT: Server shutdown requested, breaking loop");
        break;
      }

      // Handle receive errors
      if (acip_result != ASCIICHAT_OK) {
        asciichat_error_context_t err_ctx;
        if (HAS_ERRNO(&err_ctx)) {
          log_error("🔴 ACIP error for client %s: code=%u msg=%s", client->client_id, err_ctx.code,
                    err_ctx.context_message);
          if (err_ctx.code == ERROR_NETWORK) {
            log_debug("Client %s disconnected (network error): %s", client->client_id, err_ctx.context_message);
            break;
          } else if (err_ctx.code == ERROR_CRYPTO) {
            log_error_client(
                client, "SECURITY VIOLATION: Unencrypted packet when encryption required - terminating connection");
            atomic_store(&g_server_should_exit, true);
            break;
          }
        }
        log_warn("ACIP error for TCP client %s: %s (disconnecting)", client->client_id,
                 asciichat_error_string(acip_result));
        break;
      }
    } else {
      // WebRTC/WebSocket clients: async dispatch - receive packet and queue for async processing
      void *packet_data = NULL;
      void *allocated_buffer = NULL;
      size_t packet_len = 0;

      // Snapshot transport pointer to prevent use-after-free
      // Transport can be destroyed by WebSocket callback while we're receiving
      acip_transport_t *transport_snapshot = NULL;
      mutex_lock(&client->send_mutex);
      if (!client->transport || atomic_load(&client->shutting_down)) {
        mutex_unlock(&client->send_mutex);
        log_debug("RECV_THREAD[%s]: Transport destroyed or client shutting down, exiting receive loop",
                  client->client_id);
        break;
      }
      transport_snapshot = client->transport;
      mutex_unlock(&client->send_mutex);

      log_debug("🔍 RECV_THREAD[%s]: About to call transport->recv() (transport=%p)", client->client_id,
                (void *)transport_snapshot);

      asciichat_error_t recv_result =
          transport_snapshot->methods->recv(transport_snapshot, &packet_data, &packet_len, &allocated_buffer);

      log_info("🔍 RECV_THREAD[%s]: transport->recv() returned result=%d, packet_len=%zu, allocated_buffer=%p",
               client->client_id, recv_result, packet_len, allocated_buffer);

      if (recv_result != ASCIICHAT_OK) {
        asciichat_error_context_t err_ctx;
        if (HAS_ERRNO(&err_ctx)) {
          // Check for reassembly timeout (fragments arriving slowly)
          // This is NOT a connection failure - safe to retry
          if ((err_ctx.code == ERROR_NETWORK) && err_ctx.context_message &&
              strstr(err_ctx.context_message, "reassembly timeout")) {
            // Fragments are arriving slowly - this is normal, retry without disconnecting
            log_dev_every(100000, "Client %s: fragment reassembly timeout, retrying in 10ms", client->client_id);
            platform_sleep_ms(10); // Sleep 10ms to allow fragments to arrive
            continue;              // Retry without disconnecting
          }

          if (err_ctx.code == ERROR_NETWORK) {
            log_debug("Client %s disconnected (network error): %s", client->client_id, err_ctx.context_message);
            break;
          }
        }
        log_warn("Receive failed for WebRTC client %s: %s (disconnecting)", client->client_id,
                 asciichat_error_string(recv_result));
        break;
      }

      // Validate received packet before queueing
      if (packet_len < sizeof(packet_header_t)) {
        log_warn("RECV_THREAD[%s]: Received packet too small (%zu < %zu), dropping", client->client_id, packet_len,
                 sizeof(packet_header_t));
        if (allocated_buffer) {
          buffer_pool_free(NULL, allocated_buffer, packet_len);
        }
        continue;
      }

      // Queue the received packet for async dispatch
      // This prevents the receive thread from blocking on dispatch
      log_info("✅ RECV_THREAD[%s]: Queuing %zu byte packet for async dispatch", client->client_id, packet_len);

      // Extract packet type from the header to preserve it when queueing
      const packet_header_t *pkt_header = (const packet_header_t *)allocated_buffer;
      packet_type_t pkt_type = (packet_type_t)NET_TO_HOST_U16(pkt_header->type);
      uint32_t payload_len = NET_TO_HOST_U32(pkt_header->length);

      log_info("🔍 RECV_THREAD[%s]: Packet header: type=%d, payload_len=%u, total_len=%zu", client->client_id, pkt_type,
               payload_len, packet_len);

      // Build a complete packet to queue (header + payload)
      // The entire buffer (allocated_buffer) contains the full packet
      // copy_data=false because we want to transfer ownership to the queue
      uint32_t client_id_hash = fnv1a_hash_string(client->client_id);
      int enqueue_result = packet_queue_enqueue(client->received_packet_queue, pkt_type, allocated_buffer, packet_len,
                                                client_id_hash, false);

      if (enqueue_result < 0) {
        log_error("🔴 RECV_THREAD[%s]: Failed to queue received packet (queue full?) - DROPPING FRAME",
                  client->client_id);
        if (allocated_buffer) {
          buffer_pool_free(NULL, allocated_buffer, packet_len);
        }
      } else {
        log_info("✅ RECV_THREAD[%s]: Successfully queued packet (type=%d, len=%zu)", client->client_id, pkt_type,
                 packet_len);
      }
    }
  }

  // Mark client as inactive and stop all threads
  // Must stop render threads when client disconnects.
  // OPTIMIZED: Use atomic operations for thread control flags (lock-free)
  const char *client_id_snapshot = client->client_id;
  log_debug("Setting active=false in receive_thread_fn (client_id=%s, exiting receive loop)", client_id_snapshot);
  atomic_store(&client->active, false);
  atomic_store(&client->send_thread_running, false);
  atomic_store(&client->video_render_thread_running, false);
  atomic_store(&client->audio_render_thread_running, false);

  // Call remove_client() to trigger cleanup
  // Safe to call from receive thread now: remove_client() detects self-join via thread IDs
  // and skips the receive thread join when called from the receive thread itself
  log_debug("Receive thread for client %s calling remove_client() for cleanup", client_id_snapshot);
  server_context_t *server_ctx = (server_context_t *)client->server_ctx;
  if (server_ctx) {
    if (remove_client(server_ctx, client_id_snapshot) != 0) {
      log_warn("Failed to remove client %s from receive thread cleanup", client_id_snapshot);
    }
  } else {
    log_error("Receive thread for client %s: server_ctx is NULL, cannot call remove_client()", client_id_snapshot);
  }

  log_debug("Receive thread for client %s terminated", client_id_snapshot);

  // Clean up thread-local error context before exit
  asciichat_errno_destroy();

  return NULL;
}

// Thread function to handle sending data to a specific client
void *client_send_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;

  // Validate client pointer immediately before any access.
  // This prevents crashes if remove_client() has zeroed the client struct
  // while the thread was still starting at RtlUserThreadStart.
  if (!client) {
    log_error("Invalid client info in send thread (NULL pointer)");
    return NULL;
  }

  // Check if client_id is empty (client struct has been zeroed by remove_client)
  // This must be checked BEFORE accessing any client fields
  if (client->client_id[0] == 0) {
    log_debug_every(100 * US_PER_MS_INT,
                    "Send thread: client_id is empty, client struct may have been zeroed, exiting");
    return NULL;
  }

  // Additional validation: check socket OR transport is valid
  // For TCP clients: socket is valid
  // For WebRTC clients: socket is INVALID_SOCKET_VALUE but transport is valid
  mutex_lock(&client->send_mutex);
  bool has_socket = (client->socket != INVALID_SOCKET_VALUE);
  bool has_transport = (client->transport != NULL);
  mutex_unlock(&client->send_mutex);

  log_info("SEND_THREAD_VALIDATION: client_id=%s socket_valid=%d transport_valid=%d transport_ptr=%p",
           client->client_id, has_socket, has_transport, (void *)client->transport);

  if (!has_socket && !has_transport) {
    log_error("Invalid client connection in send thread (no socket or transport)");
    return NULL;
  }

  log_info("Started send thread for client %s (%s)", client->client_id, client->display_name);

  // Mark thread as running
  atomic_store(&client->send_thread_running, true);

  log_info("SEND_THREAD_LOOP_START: client_id=%s active=%d shutting_down=%d running=%d", client->client_id,
           atomic_load(&client->active), atomic_load(&client->shutting_down),
           atomic_load(&client->send_thread_running));

  // Track timing for video frame sends
  uint64_t last_video_send_time = 0;
  const uint64_t video_send_interval_us = 16666; // 60fps = ~16.67ms

  // High-frequency audio loop - separate from video frame loop
  // to ensure audio packets are sent immediately, not rate-limited by video
#define MAX_AUDIO_BATCH 8
  int loop_iteration_count = 0;
  while (!atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down) && atomic_load(&client->active) &&
         atomic_load(&client->send_thread_running)) {
    loop_iteration_count++;
    bool sent_something = false;
    uint64_t loop_start_ns = time_get_ns();
    log_info_every(5000 * US_PER_MS_INT, "[SEND_LOOP_%d] START: client=%s", loop_iteration_count, client->client_id);

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
      if (audio_packet_count > 0) {
        log_dev_every(4500 * US_PER_MS_INT, "SEND_AUDIO: client=%s dequeued=%d packets", client->client_id,
                      audio_packet_count);
      }
    } else {
      log_warn("Send thread: audio_queue is NULL for client %s", client->client_id);
    }

    // Send batched audio if we have packets
    if (audio_packet_count > 0) {
      // Protect crypto field access with mutex
      mutex_lock(&client->client_state_mutex);
      bool crypto_ready = !GET_OPTION(no_encrypt) && client->crypto_initialized &&
                          crypto_handshake_is_ready(&client->crypto_handshake_ctx);
      mutex_unlock(&client->client_state_mutex);
      (void)crypto_ready; // Currently unused - kept for potential future encryption support

      asciichat_error_t result = ASCIICHAT_OK;

      if (audio_packet_count == 1) {
        // Single packet - send directly for low latency using ACIP transport
        packet_type_t pkt_type = (packet_type_t)NET_TO_HOST_U16(audio_packets[0]->header.type);

        // Get transport reference while holding mutex briefly (prevents deadlock on TCP buffer full)
        mutex_lock(&client->send_mutex);
        if (atomic_load(&client->shutting_down) || !client->transport) {
          mutex_unlock(&client->send_mutex);
          log_warn("BREAK_AUDIO_SINGLE: client_id=%s shutting_down=%d transport=%p", client->client_id,
                   atomic_load(&client->shutting_down), (void *)client->transport);
          break; // Client is shutting down, exit thread
        }
        acip_transport_t *transport = client->transport;
        mutex_unlock(&client->send_mutex);

        // Network I/O happens OUTSIDE the mutex
        uint32_t client_id_hash = fnv1a_hash_string(client->client_id);
        result = packet_send_via_transport(transport, pkt_type, audio_packets[0]->data, audio_packets[0]->data_len,
                                           client_id_hash);
        if (result != ASCIICHAT_OK) {
          log_error("AUDIO SEND FAIL: client=%s, len=%zu, result=%d", client->client_id, audio_packets[0]->data_len,
                    result);
        }
      } else {
        // Multiple packets - batch them together and send via transport (works for all client types)
        packet_type_t first_pkt_type = (packet_type_t)NET_TO_HOST_U16(audio_packets[0]->header.type);

        // Get transport reference
        mutex_lock(&client->send_mutex);
        if (atomic_load(&client->shutting_down) || !client->transport) {
          mutex_unlock(&client->send_mutex);
          log_warn("BREAK_AUDIO_BATCH: client_id=%s shutting_down=%d transport=%p", client->client_id,
                   atomic_load(&client->shutting_down), (void *)client->transport);
          result = ERROR_NETWORK;
        } else {
          acip_transport_t *transport = client->transport;
          mutex_unlock(&client->send_mutex);

          if (first_pkt_type == PACKET_TYPE_AUDIO_OPUS_BATCH) {
            // Opus packets - batch and send via transport
            size_t total_opus_size = 0;
            for (int i = 0; i < audio_packet_count; i++) {
              total_opus_size += audio_packets[i]->data_len;
            }

            uint8_t *batched_opus = SAFE_MALLOC(total_opus_size, uint8_t *);
            uint16_t *frame_sizes = SAFE_MALLOC((size_t)audio_packet_count * sizeof(uint16_t), uint16_t *);

            if (batched_opus && frame_sizes) {
              size_t offset = 0;
              for (int i = 0; i < audio_packet_count; i++) {
                frame_sizes[i] = (uint16_t)audio_packets[i]->data_len;
                memcpy(batched_opus + offset, audio_packets[i]->data, audio_packets[i]->data_len);
                offset += audio_packets[i]->data_len;
              }
              result = acip_send_audio_opus_batch(transport, batched_opus, total_opus_size, frame_sizes,
                                                  (uint32_t)audio_packet_count, AUDIO_SAMPLE_RATE, 20);
              if (result != ASCIICHAT_OK) {
                log_error("AUDIO SEND FAIL (opus batch): client=%u, frames=%d, total_size=%zu, result=%d",
                          client->client_id, audio_packet_count, total_opus_size, result);
              }
            } else {
              log_error("Failed to allocate buffer for Opus batch");
              result = ERROR_MEMORY;
            }
            SAFE_FREE(batched_opus);
            SAFE_FREE(frame_sizes);
          } else {
            // Raw float audio - batch and send via transport
            size_t total_samples = 0;
            for (int i = 0; i < audio_packet_count; i++) {
              total_samples += audio_packets[i]->data_len / sizeof(float);
            }

            float *batched_audio = SAFE_MALLOC(total_samples * sizeof(float), float *);
            if (batched_audio) {
              size_t offset = 0;
              for (int i = 0; i < audio_packet_count; i++) {
                size_t packet_samples = audio_packets[i]->data_len / sizeof(float);
                memcpy(batched_audio + offset, audio_packets[i]->data, audio_packets[i]->data_len);
                offset += packet_samples;
              }
              result = acip_send_audio_batch(transport, batched_audio, (uint32_t)total_samples, AUDIO_SAMPLE_RATE);
              if (result != ASCIICHAT_OK) {
                log_error("AUDIO SEND FAIL (raw batch): client=%u, packets=%d, samples=%zu, result=%d",
                          client->client_id, audio_packet_count, total_samples, result);
              }
            } else {
              log_error("Failed to allocate buffer for audio batch");
              result = ERROR_MEMORY;
            }
            SAFE_FREE(batched_audio);
          }
        }
      }

      // Free all audio packets
      for (int i = 0; i < audio_packet_count; i++) {
        packet_queue_free_packet(audio_packets[i]);
      }

      if (result != ASCIICHAT_OK) {
        if (!atomic_load(&g_server_should_exit)) {
          log_error("Failed to send audio to client %s: %s", client->client_id, asciichat_error_string(result));
        }
        // Network errors corrupt the TCP stream - must disconnect immediately
        if (result == ERROR_NETWORK) {
          log_error("CLOSING_CLIENT_ON_NETWORK_ERROR: client_id=%s due to audio send failure (corrupted stream)",
                    client->client_id);
          break; // Exit send loop to trigger cleanup
        }
        log_warn("SKIP_AUDIO_ERROR: client_id=%s result=%d (continuing to send video)", client->client_id, result);
        // Continue sending video for non-network errors (audio is optional for browser clients)
      }

      sent_something = true;
      uint64_t audio_done_ns = time_get_ns();
      char audio_elapsed_str[32];
      time_pretty(audio_done_ns - loop_start_ns, -1, audio_elapsed_str, sizeof(audio_elapsed_str));
      log_info_every(5000 * US_PER_MS_INT, "[SEND_LOOP_%d] AUDIO_SENT: took %s", loop_iteration_count,
                     audio_elapsed_str);

      // Small sleep to let more audio packets queue (helps batching efficiency)
      if (audio_packet_count > 0) {
        platform_sleep_us(100); // 0.1ms - minimal delay
      }
    } else {
      // No audio packets - brief sleep to avoid busy-looping, then check for other tasks
      log_info_every(5000 * US_PER_MS_INT, "[SEND_LOOP_%d] NO_AUDIO: sleeping 1ms", loop_iteration_count);
      platform_sleep_us(1 * US_PER_MS_INT); // 1ms - enough for audio render thread to queue more packets

      // Check if session rekeying should be triggered
      mutex_lock(&client->client_state_mutex);
      bool should_rekey = !GET_OPTION(no_encrypt) && client->crypto_initialized &&
                          crypto_handshake_is_ready(&client->crypto_handshake_ctx) &&
                          crypto_handshake_should_rekey(&client->crypto_handshake_ctx);
      mutex_unlock(&client->client_state_mutex);

      if (should_rekey) {
        log_debug("Rekey threshold reached for client %s, initiating session rekey", client->client_id);
        mutex_lock(&client->client_state_mutex);
        // Get socket reference briefly to avoid deadlock on TCP buffer full
        mutex_lock(&client->send_mutex);
        if (atomic_load(&client->shutting_down) || client->socket == INVALID_SOCKET_VALUE) {
          mutex_unlock(&client->send_mutex);
          mutex_unlock(&client->client_state_mutex);
          log_warn("BREAK_REKEY: client_id=%s shutting_down=%d socket=%d", client->client_id,
                   atomic_load(&client->shutting_down), (int)client->socket);
          break; // Client is shutting down, exit thread
        }
        socket_t rekey_socket = client->socket;
        mutex_unlock(&client->send_mutex);

        // Network I/O happens OUTSIDE the send_mutex (client_state_mutex still held for crypto state)
        asciichat_error_t result = crypto_handshake_rekey_request(&client->crypto_handshake_ctx, rekey_socket);
        mutex_unlock(&client->client_state_mutex);

        if (result != ASCIICHAT_OK) {
          log_error("Failed to send REKEY_REQUEST to client %s: %d", client->client_id, result);
        } else {
          log_debug("Sent REKEY_REQUEST to client %s", client->client_id);
          // Notify client that session rekeying has been initiated (old keys still active)
          log_info_client(client, "Session rekey initiated - rotating encryption keys");
        }
      }
    }

    // Always consume frames from the buffer to prevent accumulation
    // Rate-limit the actual sending, but always mark frames as consumed
    uint64_t video_check_ns = time_get_ns();
    if (!client->outgoing_video_buffer) {
      // Buffer has been destroyed (client is shutting down).
      // Exit cleanly instead of looping forever trying to access freed memory.
      log_warn("⚠️  Send thread exiting: outgoing_video_buffer is NULL for client %s (client shutting down?)",
               client->client_id);
      break;
    }

    // Get latest frame from double buffer (lock-free operation)
    // This marks the frame as consumed even if we don't send it yet
    const video_frame_t *frame = video_frame_get_latest(client->outgoing_video_buffer);
    uint64_t frame_get_ns = time_get_ns();
    char frame_get_elapsed_str[32];
    time_pretty(frame_get_ns - video_check_ns, -1, frame_get_elapsed_str, sizeof(frame_get_elapsed_str));
    log_info_every(5000 * US_PER_MS_INT, "[SEND_LOOP_%d] VIDEO_GET_FRAME: took %s, frame=%p", loop_iteration_count,
                   frame_get_elapsed_str, (void *)frame);
    log_dev_every(4500 * US_PER_MS_INT, "Send thread: video_frame_get_latest returned %p for client %s", (void *)frame,
                  client->client_id);

    // Check if get_latest failed (buffer might have been destroyed)
    if (!frame) {
      log_warn("⚠️  Send thread exiting: video_frame_get_latest returned NULL for client %s (buffer destroyed?)",
               client->client_id);
      break; // Exit thread if buffer is invalid
    }

    // Check if it's time to send a video frame (60fps rate limiting)
    // Only rate-limit the SEND operation, not frame consumption
    uint64_t current_time_ns = time_get_ns();
    uint64_t current_time_us = time_ns_to_us(current_time_ns);
    uint64_t time_since_last_send_us = current_time_us - last_video_send_time;
    log_dev_every(4500 * US_PER_MS_INT,
                  "Send thread timing check: time_since_last=%llu us, interval=%llu us, should_send=%d",
                  (unsigned long long)time_since_last_send_us, (unsigned long long)video_send_interval_us,
                  (time_since_last_send_us >= video_send_interval_us));

    if (current_time_us - last_video_send_time >= video_send_interval_us) {
      log_info_every(5000 * US_PER_MS_INT, "✓ SEND_TIME_READY: client_id=%s time_since=%llu interval=%llu",
                     client->client_id, (unsigned long long)time_since_last_send_us,
                     (unsigned long long)video_send_interval_us);
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
          log_warn("BREAK_CLEAR_CONSOLE: client_id=%s shutting_down=%d transport=%p", client->client_id,
                   atomic_load(&client->shutting_down), (void *)client->transport);
          break; // Client is shutting down, exit thread
        }
        acip_transport_t *clear_transport = client->transport;
        mutex_unlock(&client->send_mutex);

        // Network I/O happens OUTSIDE the mutex
        acip_send_clear_console(clear_transport);
        log_debug_every(LOG_RATE_FAST, "Client %s: Sent CLEAR_CONSOLE (grid changed %d → %d sources)",
                        client->client_id, sent_sources, rendered_sources);
        atomic_store(&client->last_sent_grid_sources, rendered_sources);
        sent_something = true;
      }

      log_dev_every(4500 * US_PER_MS_INT, "Send thread: frame validation - frame=%p, frame->data=%p, frame->size=%zu",
                    (void *)frame, (void *)frame->data, frame->size);

      if (!frame->data) {
        log_dev("✗ SKIP_NO_DATA: client_id=%s frame=%p data=%p", client->client_id, (void *)frame, (void *)frame->data);
        continue;
      }
      log_dev("✓ FRAME_DATA_OK: client_id=%s data=%p", client->client_id, (void *)frame->data);

      if (frame->data && frame->size == 0) {
        log_dev("✗ SKIP_ZERO_SIZE: client_id=%s size=%zu", client->client_id, frame->size);
        platform_sleep_us(1 * US_PER_MS_INT); // 1ms sleep
        continue;
      }
      log_dev("✓ FRAME_SIZE_OK: client_id=%s size=%zu", client->client_id, frame->size);

      // Snapshot frame metadata (safe with double-buffer system)
      const char *frame_data = (const char *)frame->data; // Pointer snapshot - data is stable in front buffer
      size_t frame_size = frame->size;                    // Size snapshot - prevent race condition with render thread
      uint32_t width = atomic_load(&client->width);
      uint32_t height = atomic_load(&client->height);
      uint64_t step1_ns = time_get_ns();
      uint64_t step2_ns = time_get_ns();
      uint64_t step3_ns = time_get_ns();
      uint64_t step4_ns = time_get_ns();

      // Check if crypto handshake is complete before sending (prevents sending to unauthenticated clients)
      mutex_lock(&client->client_state_mutex);
      bool crypto_ready = GET_OPTION(no_encrypt) ||
                          (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_handshake_ctx));
      mutex_unlock(&client->client_state_mutex);

      if (!crypto_ready) {
        log_dev("⚠️  SKIP_SEND_CRYPTO: client_id=%s crypto_initialized=%d no_encrypt=%d", client->client_id,
                client->crypto_initialized, GET_OPTION(no_encrypt));
        continue; // Skip this frame, will try again on next loop iteration
      }
      log_dev("✓ CRYPTO_READY: client_id=%s about to send frame", client->client_id);

      // Get transport reference briefly to avoid deadlock on TCP buffer full
      // ACIP transport handles header building, CRC32, encryption internally
      log_dev_every(4500 * US_PER_MS_INT,
                    "Send thread: About to send frame to client %s (width=%u, height=%u, size=%zu, data=%p)",
                    client->client_id, width, height, frame_size, (void *)frame_data);

      // Log first 32 bytes of frame data to verify we can access it
      if (frame_data && frame_size > 0) {
        log_info_every(5000 * US_PER_MS_INT,
                       "FRAME_DATA_HEX: client=%u first_bytes=[%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                       "%02x %02x %02x %02x %02x]",
                       client->client_id, ((uint8_t *)frame_data)[0], ((uint8_t *)frame_data)[1],
                       ((uint8_t *)frame_data)[2], ((uint8_t *)frame_data)[3], ((uint8_t *)frame_data)[4],
                       ((uint8_t *)frame_data)[5], ((uint8_t *)frame_data)[6], ((uint8_t *)frame_data)[7],
                       ((uint8_t *)frame_data)[8], ((uint8_t *)frame_data)[9], ((uint8_t *)frame_data)[10],
                       ((uint8_t *)frame_data)[11], ((uint8_t *)frame_data)[12], ((uint8_t *)frame_data)[13],
                       ((uint8_t *)frame_data)[14], ((uint8_t *)frame_data)[15]);
      }

      mutex_lock(&client->send_mutex);
      if (atomic_load(&client->shutting_down) || !client->transport) {
        mutex_unlock(&client->send_mutex);
        log_warn("BREAK_FRAME_SEND: client_id=%s shutting_down=%d transport=%p loop_iter=%d", client->client_id,
                 atomic_load(&client->shutting_down), (void *)client->transport, loop_iteration_count);
        break; // Client is shutting down, exit thread
      }
      acip_transport_t *frame_transport = client->transport;
      mutex_unlock(&client->send_mutex);

      // Network I/O happens OUTSIDE the mutex
      log_dev_every(4500 * US_PER_MS_INT, "SEND_ASCII_FRAME: client_id=%s size=%zu width=%u height=%u",
                    client->client_id, frame_size, width, height);
      uint64_t send_start_ns = time_get_ns();
      log_dev("[SEND_LOOP_%d] FRAME_SEND_START: size=%zu", loop_iteration_count, frame_size);
      asciichat_error_t send_result =
          acip_send_ascii_frame(frame_transport, frame_data, frame_size, width, height, client->client_id);
      uint64_t send_end_ns = time_get_ns();
      char send_elapsed_str[32];
      time_pretty(send_end_ns - send_start_ns, -1, send_elapsed_str, sizeof(send_elapsed_str));
      log_dev("[SEND_LOOP_%d] FRAME_SEND_END: took %s, result=%d", loop_iteration_count, send_elapsed_str, send_result);
      uint64_t step5_ns = time_get_ns();

      if (send_result != ASCIICHAT_OK) {
        if (!atomic_load(&g_server_should_exit)) {
          SET_ERRNO(ERROR_NETWORK, "Failed to send video frame to client %s: %s", client->client_id,
                    asciichat_error_string(send_result));
        }
        log_error("SEND_FRAME_FAILED: client_id=%s result=%d message=%s loop_iter=%d", client->client_id, send_result,
                  asciichat_error_string(send_result), loop_iteration_count);
        log_warn("BREAK_SEND_ERROR: client_id=%s (frame send failed)", client->client_id);
        break;
      }

      log_dev_every(4500 * US_PER_MS_INT, "SEND_FRAME_SUCCESS: client_id=%s size=%zu", client->client_id, frame_size);

      // Increment frame counter and log
      unsigned long frame_count = atomic_fetch_add(&client->frames_sent_count, 1) + 1;
      log_info("🎬 FRAME_SENT: client_id=%s frame_num=%lu size=%zu", client->client_id, frame_count, frame_size);

      sent_something = true;
      last_video_send_time = current_time_us;

      uint64_t frame_end_ns = time_get_ns();
      uint64_t frame_time_us = time_ns_to_us(time_elapsed_ns(frame_start_ns, frame_end_ns));
      if (frame_time_us > 15 * US_PER_MS_INT) { // Log if sending a frame takes > 15ms (encryption adds ~5-6ms)
        uint64_t step1_us = time_ns_to_us(time_elapsed_ns(frame_start_ns, step1_ns));
        uint64_t step2_us = time_ns_to_us(time_elapsed_ns(step1_ns, step2_ns));
        uint64_t step3_us = time_ns_to_us(time_elapsed_ns(step2_ns, step3_ns));
        uint64_t step4_us = time_ns_to_us(time_elapsed_ns(step3_ns, step4_ns));
        uint64_t step5_us = time_ns_to_us(time_elapsed_ns(step4_ns, step5_ns));
        log_warn_every(
            LOG_RATE_DEFAULT,
            "SEND_THREAD: Frame send took %.2fms for client %s | Snapshot: %.2fms | Memcpy: %.2fms | CRC32: %.2fms | "
            "Header: %.2fms | send_packet_secure: %.2fms",
            frame_time_us / 1000.0, client->client_id, step1_us / 1000.0, step2_us / 1000.0, step3_us / 1000.0,
            step4_us / 1000.0, step5_us / 1000.0);
      }
    }

    // If we didn't send anything, sleep briefly to prevent busy waiting
    if (!sent_something) {
      log_info_every(5000 * US_PER_MS_INT, "[SEND_LOOP_%d] IDLE_SLEEP: nothing sent", loop_iteration_count);
      platform_sleep_us(1 * US_PER_MS_INT); // 1ms sleep
    }
    uint64_t loop_end_ns = time_get_ns();
    uint64_t loop_elapsed_ns = loop_end_ns - loop_start_ns;
    if (loop_elapsed_ns > 10 * NS_PER_MS) {
      char loop_elapsed_str[32];
      time_pretty(loop_elapsed_ns, -1, loop_elapsed_str, sizeof(loop_elapsed_str));
      log_warn("[SEND_LOOP_%d] SLOW_ITERATION: took %s (client=%u)", loop_iteration_count, loop_elapsed_str,
               client->client_id);
    }
  }

  // Log why the send thread exited
  log_warn("Send thread exit conditions - client_id=%s g_server_should_exit=%d shutting_down=%d active=%d "
           "send_thread_running=%d",
           client->client_id, atomic_load(&g_server_should_exit), atomic_load(&client->shutting_down),
           atomic_load(&client->active), atomic_load(&client->send_thread_running));

  // Mark thread as stopped
  atomic_store(&client->send_thread_running, false);
  log_debug("Send thread for client %s terminated", client->client_id);

  // Clean up thread-local error context before exit
  asciichat_errno_destroy();

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
    char client_id[MAX_CLIENT_ID_LEN];
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
    time_pretty((uint64_t)((double)lock_time_ns), -1, duration_str, sizeof(duration_str));
    log_warn("broadcast_server_state: rwlock_rdlock took %s", duration_str);
  }

  // Count active clients and snapshot client data while holding lock
  // Use atomic_load for all atomic fields to prevent data races.
  for (int i = 0; i < MAX_CLIENTS; i++) {
    bool is_active = atomic_load(&g_client_manager.clients[i].active);
    if (is_active && atomic_load(&g_client_manager.clients[i].is_sending_video)) {
      active_video_count++;
    }
    if (is_active && g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
      // Skip clients that haven't completed crypto handshake yet
      // Check both crypto_initialized AND crypto context (defense in depth)
      if (!GET_OPTION(no_encrypt) && !g_client_manager.clients[i].crypto_initialized) {
        log_debug("Skipping server_state broadcast to client %s: crypto handshake not complete",
                  g_client_manager.clients[i].client_id);
        continue;
      }

      // Get crypto context (may be NULL if --no-encrypt)
      const crypto_context_t *crypto_ctx =
          crypto_handshake_get_context(&g_client_manager.clients[i].crypto_handshake_ctx);
      if (!GET_OPTION(no_encrypt) && !crypto_ctx) {
        // Skip clients without valid crypto context
        log_debug("Skipping server_state broadcast to client %s: no crypto context",
                  g_client_manager.clients[i].client_id);
        continue;
      }

      client_snapshots[snapshot_count].socket = g_client_manager.clients[i].socket;
      SAFE_STRNCPY(client_snapshots[snapshot_count].client_id, g_client_manager.clients[i].client_id,
                   sizeof(client_snapshots[snapshot_count].client_id) - 1);
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
    log_debug_every(5000 * US_PER_MS_INT,
                    "BROADCAST_DEBUG: Sending SERVER_STATE to client %s (socket %d) with crypto_ctx=%p",
                    client_snapshots[i].client_id, client_snapshots[i].socket, (void *)client_snapshots[i].crypto_ctx);

    // Protect socket write with per-client send_mutex.
    client_info_t *target = find_client_by_id(client_snapshots[i].client_id);
    if (target) {
      // IMPORTANT: Verify client_id matches expected value - prevents use-after-free
      // if client was removed and replaced with another client in same slot
      if (strcmp(target->client_id, client_snapshots[i].client_id) != 0) {
        log_warn("Client %s ID mismatch during broadcast (found %s), skipping send", client_snapshots[i].client_id,
                 target->client_id);
        continue;
      }

      mutex_lock(&target->send_mutex);

      // Double-check client_id again after acquiring mutex (stronger protection)
      if (strcmp(target->client_id, client_snapshots[i].client_id) != 0) {
        mutex_unlock(&target->send_mutex);
        log_warn("Client %s was removed during broadcast send (now %s), skipping", client_snapshots[i].client_id,
                 target->client_id);
        continue;
      }

      // Send via ACIP transport
      asciichat_error_t result = acip_send_server_state(target->transport, &net_state);
      mutex_unlock(&target->send_mutex);

      if (result != ASCIICHAT_OK) {
        log_error("Failed to send server state to client %s: %s", client_snapshots[i].client_id,
                  asciichat_error_string(result));
      } else {
        log_debug_every(5000 * US_PER_MS_INT, "Sent server state to client %s: %u connected, %u active",
                        client_snapshots[i].client_id, state.connected_client_count, state.active_client_count);
      }
    } else {
      log_warn("Client %s removed before broadcast send could complete", client_snapshots[i].client_id);
    }
  }

  if (lock_held_ns > 1 * NS_PER_MS_INT) {
    char duration_str[32];
    time_pretty((uint64_t)((double)lock_held_ns), -1, duration_str, sizeof(duration_str));
    log_warn("broadcast_server_state: rwlock held for %s (includes network I/O)", duration_str);
  }
}

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

/**
 * @brief Start threads for a WebRTC client after crypto initialization
 *
 * This is called by WebSocket handler after crypto handshake is initialized.
 * It ensures receive thread doesn't try to process packets before crypto context exists.
 *
 * @param server_ctx Server context
 * @param client_id Client ID to start threads for
 * @return 0 on success, -1 on failure
 */
int start_webrtc_client_threads(server_context_t *server_ctx, const char *client_id) {
  if (!server_ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Server context is NULL");
    return -1;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    SET_ERRNO(ERROR_NOT_FOUND, "Client %s not found", client_id);
    return -1;
  }

  log_debug("Starting threads for WebRTC client %s...", client_id);
  return start_client_threads(server_ctx, client, false);
}

void stop_client_threads(client_info_t *client) {
  if (!client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client is NULL");
    return;
  }

  // Signal threads to stop
  log_debug("Setting active=false in stop_client_threads (client_id=%s)", client->client_id);
  atomic_store(&client->active, false);
  atomic_store(&client->send_thread_running, false);

  // Wait for threads to finish
  if (asciichat_thread_is_initialized(&client->send_thread)) {
    asciichat_thread_join(&client->send_thread, NULL);
  }
  if (asciichat_thread_is_initialized(&client->receive_thread)) {
    asciichat_thread_join(&client->receive_thread, NULL);
  }
  // For async dispatch: stop dispatch thread if running
  if (asciichat_thread_is_initialized(&client->dispatch_thread)) {
    atomic_store(&client->dispatch_thread_running, false);
    asciichat_thread_join(&client->dispatch_thread, NULL);
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

  // Async dispatch: clean up received packet queue
  if (client->received_packet_queue) {
    packet_queue_destroy(client->received_packet_queue);
    client->received_packet_queue = NULL;
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
    log_error("Received encrypted packet but crypto not ready for client %s", client->client_id);
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
    SET_ERRNO(ERROR_CRYPTO, "Failed to process encrypted packet from client %s (result=%d)", client->client_id,
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
    SET_ERRNO(ERROR_CRYPTO, "Decrypted packet too small for header from client %s", client->client_id);
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
static void acip_server_on_crypto_key_exchange_resp(packet_type_t type, const void *payload, size_t payload_len,
                                                    void *client_ctx, void *app_ctx);
static void acip_server_on_crypto_auth_response(packet_type_t type, const void *payload, size_t payload_len,
                                                void *client_ctx, void *app_ctx);
static void acip_server_on_crypto_no_encryption(packet_type_t type, const void *payload, size_t payload_len,
                                                void *client_ctx, void *app_ctx);

/**
 * @brief Global ACIP server callbacks structure
 *
 * Handles all ACIP packet types from clients including crypto handshake and rekey protocols.
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
    .on_crypto_key_exchange_resp = acip_server_on_crypto_key_exchange_resp,
    .on_crypto_auth_response = acip_server_on_crypto_auth_response,
    .on_crypto_no_encryption = acip_server_on_crypto_no_encryption,
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
  uint64_t callback_start_ns = time_get_ns();
  client_info_t *client = (client_info_t *)client_ctx;

  log_info("CALLBACK_IMAGE_FRAME: client_id=%s, width=%u, height=%u, pixel_format=%u, compressed_size=%u, data_len=%zu",
           client->client_id, header->width, header->height, header->pixel_format, header->compressed_size, data_len);

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

  // Auto-set dimensions from IMAGE_FRAME if not already set (fallback for missing CLIENT_CAPABILITIES)
  // This ensures render thread can start even if CLIENT_CAPABILITIES was never sent
  if (atomic_load(&client->width) == 0 || atomic_load(&client->height) == 0) {
    atomic_store(&client->width, header->width);
    atomic_store(&client->height, header->height);
    log_info("Client %s: Auto-set dimensions from IMAGE_FRAME: %ux%u (CLIENT_CAPABILITIES not received)",
             client->client_id, header->width, header->height);
  }

  // Auto-enable video stream if not already enabled
  bool was_sending_video = atomic_load(&client->is_sending_video);
  if (!was_sending_video) {
    if (atomic_compare_exchange_strong(&client->is_sending_video, &was_sending_video, true)) {
      log_info("Client %s auto-enabled video stream (received IMAGE_FRAME)", client->client_id);
      log_info_client(client, "First video frame received - streaming active");
    }
  } else {
    // Log periodically
    mutex_lock(&client->client_state_mutex);
    client->frames_received_logged++;
    if (client->frames_received_logged % 25000 == 0) {
      char pretty[64];
      format_bytes_pretty(data_len, pretty, sizeof(pretty));
      log_debug("Client %s has sent %u IMAGE_FRAME packets (%s)", client->client_id, client->frames_received_logged,
                pretty);
    }
    mutex_unlock(&client->client_state_mutex);
  }

  // Compute hash of incoming pixel data to detect duplicates
  uint32_t incoming_pixel_hash = 0;
  for (size_t i = 0; i < data_len && i < 1000; i++) {
    incoming_pixel_hash = (uint32_t)((uint64_t)incoming_pixel_hash * 31 + ((unsigned char *)pixel_data)[i]);
  }

  // Per-client hash tracking to detect duplicate frames
  bool is_new_frame = (incoming_pixel_hash != client->last_received_frame_hash);

  // Inspect first few pixels of incoming frame
  uint32_t first_pixel_rgb = 0;
  if (data_len >= 3) {
    first_pixel_rgb = ((uint32_t)((unsigned char *)pixel_data)[0] << 16) |
                      ((uint32_t)((unsigned char *)pixel_data)[1] << 8) | (uint32_t)((unsigned char *)pixel_data)[2];
  }

  if (is_new_frame) {
    log_info("RECV_FRAME #%u NEW: Client %s dimensions=%ux%u pixel_size=%zu hash=0x%08x first_rgb=0x%06x (prev=0x%08x)",
             client->frames_received, client->client_id, header->width, header->height, data_len, incoming_pixel_hash,
             first_pixel_rgb, client->last_received_frame_hash);
    client->last_received_frame_hash = incoming_pixel_hash;
  } else {
    log_info("RECV_FRAME #%u DUP: Client %s dimensions=%ux%u pixel_size=%zu hash=0x%08x first_rgb=0x%06x",
             client->frames_received, client->client_id, header->width, header->height, data_len, incoming_pixel_hash,
             first_pixel_rgb);
  }

  // Store frame data to incoming_video_buffer
  // pixel_data points to websocket reassembly buffer which will be freed after callback returns,
  // so we must copy it immediately into the persistent frame buffer (which is pre-allocated to 2MB)
  if (client->incoming_video_buffer) {
    video_frame_buffer_t *vfb = client->incoming_video_buffer;
    log_info("VFB_DEBUG: vfb=%p, vfb->allocated_size=%zu, vfb->active=%d, back_buffer=%p, frames[0]=%p frames[1]=%p",
             (void *)vfb, vfb->allocated_buffer_size, vfb->active, (void *)vfb->back_buffer, (void *)&vfb->frames[0],
             (void *)&vfb->frames[1]);

    video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);
    if (frame && frame->data && data_len > 0) {
      // Debug: Check frame buffer capacity
      size_t buffer_capacity = client->incoming_video_buffer->allocated_buffer_size;
      size_t total_size = sizeof(uint32_t) * 2 + data_len;

      log_info("FRAME_STORAGE: client=%s, frame=%p, frame->data=%p, capacity=%zu, data_len=%zu, total=%zu",
               client->client_id, (void *)frame, (void *)frame->data, buffer_capacity, data_len, total_size);

      // Copy frame data: width (4B) + height (4B) + pixel data
      uint32_t width_net = HOST_TO_NET_U32(header->width);
      uint32_t height_net = HOST_TO_NET_U32(header->height);

      memcpy(frame->data, &width_net, sizeof(uint32_t));
      memcpy((char *)frame->data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
      memcpy((char *)frame->data + sizeof(uint32_t) * 2, pixel_data, data_len);

      frame->size = total_size;
      frame->width = header->width;
      frame->height = header->height;
      frame->capture_timestamp_ns = time_get_ns();
      frame->sequence_number = ++client->frames_received;

      video_frame_commit(client->incoming_video_buffer);
      log_debug("Frame committed: client %s, seq=%u, size=%zu", client->client_id, frame->sequence_number, total_size);
    } else {
      log_warn("FRAME_STORE_SKIP: client=%s, frame=%p, frame->data=%p, data_len=%zu", client->client_id, (void *)frame,
               frame ? (void *)frame->data : NULL, data_len);
    }
  } else {
    log_warn("NO_INCOMING_VIDEO_BUFFER: client=%s", client->client_id);
  }

  uint64_t callback_end_ns = time_get_ns();
  char cb_duration_str[32];
  time_pretty((uint64_t)((double)(callback_end_ns - callback_start_ns)), -1, cb_duration_str, sizeof(cb_duration_str));
  log_info("[WS_TIMING] on_image_frame callback took %s (data_len=%zu)", cb_duration_str, data_len);
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
  log_debug_every(LOG_RATE_DEFAULT, "Received audio batch from client %s (samples=%zu, is_sending_audio=%d)",
                  client->client_id, num_samples, atomic_load(&client->is_sending_audio));

  if (!atomic_load(&client->is_sending_audio)) {
    log_debug("Ignoring audio batch - client %s not in audio streaming mode", client->client_id);
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
    SET_ERRNO(ERROR_NETWORK, "Failed to send PONG response to client %s: %s", client->client_id,
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

  log_debug("Received REKEY_REQUEST from client %s", client->client_id);

  // Process the client's rekey request
  mutex_lock(&client->client_state_mutex);
  asciichat_error_t crypto_result =
      crypto_handshake_process_rekey_request(&client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST from client %s: %d", client->client_id, crypto_result);
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
    log_error("Failed to send REKEY_RESPONSE to client %s: %d", client->client_id, crypto_result);
  } else {
    log_debug("Sent REKEY_RESPONSE to client %s", client->client_id);
  }
}

static void acip_server_on_crypto_rekey_response(const void *payload, size_t payload_len, void *client_ctx,
                                                 void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received REKEY_RESPONSE from client %s", client->client_id);

  // Process the client's rekey response
  mutex_lock(&client->client_state_mutex);
  asciichat_error_t crypto_result =
      crypto_handshake_process_rekey_response(&client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE from client %s: %d", client->client_id, crypto_result);
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
    log_error("Failed to send REKEY_COMPLETE to client %s: %d", client->client_id, crypto_result);
  } else {
    log_debug("Sent REKEY_COMPLETE to client %s - session rekeying complete", client->client_id);
  }
}

static void acip_server_on_crypto_rekey_complete(const void *payload, size_t payload_len, void *client_ctx,
                                                 void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received REKEY_COMPLETE from client %s", client->client_id);

  // Process and commit to new key
  mutex_lock(&client->client_state_mutex);
  asciichat_error_t crypto_result =
      crypto_handshake_process_rekey_complete(&client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_COMPLETE from client %s: %d", client->client_id, crypto_result);
  } else {
    log_debug("Session rekeying completed successfully with client %s", client->client_id);
    // Notify client that rekeying is complete (new keys now active on both sides)
    log_info_client(client, "Session rekey complete - new encryption keys active");
  }
}

static void acip_server_on_crypto_key_exchange_resp(packet_type_t type, const void *payload, size_t payload_len,
                                                    void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received CRYPTO_KEY_EXCHANGE_RESP from client %s", client->client_id);

  // Call refactored handshake function from Phase 2
  asciichat_error_t result = crypto_handshake_server_auth_challenge(&client->crypto_handshake_ctx, client->transport,
                                                                    type, payload, payload_len);

  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake auth challenge failed for client %s", client->client_id);
    disconnect_client_for_bad_data(client, "Crypto handshake auth challenge failed");
  } else {
    // Check if handshake completed (no-auth flow) or if AUTH_CHALLENGE was sent (with-auth flow)
    if (client->crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_READY) {
      // No-auth flow: handshake complete, HANDSHAKE_COMPLETE was sent
      log_info("Crypto handshake completed successfully for client %s (no authentication)", client->client_id);
      client->crypto_initialized = true;
      client->transport->crypto_ctx = &client->crypto_handshake_ctx.crypto_ctx;
    } else {
      // With-auth flow: AUTH_CHALLENGE was sent, waiting for AUTH_RESPONSE
      log_debug("Sent AUTH_CHALLENGE to client %s", client->client_id);
    }
  }
}

static void acip_server_on_crypto_auth_response(packet_type_t type, const void *payload, size_t payload_len,
                                                void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;

  log_debug("Received CRYPTO_AUTH_RESPONSE from client %s", client->client_id);

  // Call refactored handshake function from Phase 2
  asciichat_error_t result =
      crypto_handshake_server_complete(&client->crypto_handshake_ctx, client->transport, type, payload, payload_len);

  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake complete failed for client %s", client->client_id);
    disconnect_client_for_bad_data(client, "Crypto handshake complete failed");
  } else {
    log_info("Crypto handshake completed successfully for client %s", client->client_id);
    log_error("[CRYPTO_SETUP] Setting crypto context for client %s: transport=%p, crypto_ctx=%p", client->client_id,
              (void *)client->transport, (void *)&client->crypto_handshake_ctx.crypto_ctx);
    client->crypto_initialized = true;
    client->transport->crypto_ctx = &client->crypto_handshake_ctx.crypto_ctx;
    log_error("[CRYPTO_SETUP] Crypto context SET: transport->crypto_ctx=%p", (void *)client->transport->crypto_ctx);
  }
}

static void acip_server_on_crypto_no_encryption(packet_type_t type, const void *payload, size_t payload_len,
                                                void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  (void)type;
  (void)payload;
  (void)payload_len;
  client_info_t *client = (client_info_t *)client_ctx;

  log_error("Client %s sent NO_ENCRYPTION - encryption mode mismatch", client->client_id);
  disconnect_client_for_bad_data(client, "Encryption mode mismatch - server requires encryption");
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
  if (type == 5000) { // CLIENT_CAPABILITIES
    log_debug("CLIENT: client_id=%s, data=%p, len=%zu", client->client_id, data, len);
  }

  // Rate limiting: Check and record packet-specific rate limits
  if (g_rate_limiter) {
    if (!check_and_record_packet_rate_limit(g_rate_limiter, client->client_ip, client->socket, type)) {
      // Rate limit exceeded - error response already sent by utility function
      return;
    }
  }

  // O(1) dispatch via hash table lookup
  int idx = client_dispatch_hash_lookup(g_client_dispatch_hash, type);
  if (type == 5000 || type == 3001) {
    log_error("DISPATCH_LOOKUP: type=%d, idx=%d (len=%zu)", type, idx, len);
  }
  if (idx < 0) {
    disconnect_client_for_bad_data(client, "Unknown packet type: %d (len=%zu)", type, len);
    return;
  }

  if (type == 5000 || type == 3001) {
    log_error("DISPATCH_HANDLER: type=%d, calling handler[%d]...", type, idx);
  }
  g_client_dispatch_handlers[idx](client, data, len);
  if (type == 5000 || type == 3001) {
    log_error("DISPATCH_HANDLER: type=%d, handler returned", type);
  }
}
