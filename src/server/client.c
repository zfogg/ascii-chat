#include <ascii-chat/otel.h>
#include "session/server_like.h"
#include <ascii-chat/network/errors.h>
#include <ascii-chat/stats/runtime.h>
/* Media client state and render-worker ownership. The network runtime owns
 * establishment, packet reception, transport callbacks, and connection teardown. */

#include <ascii-chat/atomic.h>
#include <ascii-chat/network/tcp/async.h>
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
#include "connections.h"
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/discovery/nouns.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/media/codecs.h>
#include <ascii-chat/uthash.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/log.h>
#include <ascii-chat/log/io.h>

// FFmpeg headers for H.265 decoding
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

// =============================================================================
// Packet Handler Dispatch (O(1) hash table lookup)
// =============================================================================

typedef void (*client_packet_handler_t)(client_info_t *client, const void *data, size_t len);

#define CLIENT_DISPATCH_HASH_SIZE 32
#define CLIENT_DISPATCH_HANDLER_COUNT 14

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
    (client_packet_handler_t)handle_image_frame_h265_packet,       // 12
    (client_packet_handler_t)handle_image_frame_h264_packet,       // 13
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
    [27] = {PACKET_TYPE_IMAGE_FRAME_H264,      13},
    [26] = {PACKET_TYPE_IMAGE_FRAME_H265,      12},  // hash(3002)=26
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
    log_warn("Failed to parse error packet from client %s: %s", client_id, asciichat_error_format(NULL, parse_result));
    return;
  }

  log_error("Client %s reported %s", client_id, asciichat_error_format(message, reported_error));
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
 * - Write operations: rwlock_wrlock() for add_transport_client(), remove_client()
 * - Always acquire THIS lock before per-client mutexes (lock ordering)
 */
rwlock_t g_client_manager_rwlock = {0};

/** Track whether g_client_manager_rwlock was successfully initialized. */
bool g_client_manager_rwlock_initialized = false;

// Forward declarations for internal functions
void broadcast_server_state_to_all_clients(void); ///< Notify all clients of state changes
static int start_client_threads(server_context_t *server_ctx, client_info_t *client); ///< Common thread initialization

static void initialize_default_render_capabilities(client_info_t *client) {
  if (!client) {
    return;
  }

  // Keep safe defaults for early control packets, but do not mark capabilities
  // complete. The render thread must wait for the browser's actual dimensions
  // and requested frame rate instead of flooding a just-opened DataChannel at
  // the fallback rate.
  client->width = 64;
  client->height = 16;
  client->terminal_caps.color_level = TERM_COLOR_NONE;
  client->terminal_caps.color_count = 2;
  client->terminal_caps.render_mode = RENDER_MODE_FOREGROUND;
  client->terminal_caps.palette_type = PALETTE_STANDARD;
  client->terminal_caps.desired_fps = VIDEO_RENDER_FPS;
  client->terminal_caps.wants_padding = false;
  client->terminal_caps.utf8_support = false;
  client->terminal_caps.detection_reliable = false;
  client->client_palette_type = PALETTE_STANDARD;
  client->client_palette_initialized =
      initialize_client_palette(PALETTE_STANDARD, NULL, client->client_palette_chars, &client->client_palette_len,
                                client->client_luminance_palette) == 0;
  client->has_terminal_caps = false;
}

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

/** Start render workers and, for WebSocket/WebRTC, application queue consumers. */
static int start_client_threads(server_context_t *server_ctx, client_info_t *client) {
  if (!server_ctx || !client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "server_ctx or client is NULL");
    return -1;
  }

  const char *client_id = client->client_id;

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
    log_warn("Failed to send initial server state to client %s: %s", client_id,
             asciichat_error_format(NULL, packet_result));
  } else {
    log_debug("Sent initial server state to client %s: %u connected clients", client_id, state.connected_client_count);
  }

  atomic_store_bool(&client->event_ready, true);
  return create_client_render_threads(server_ctx, client);
}

static void server_client_initialize_transport_slot(server_context_t *server_ctx, client_info_t *client,
                                                    acip_transport_t *transport, const char *new_client_id,
                                                    const char *client_ip, int slot) {
  // Caller holds the client-manager write lock.
  // Free any existing buffers from previous client in this slot
  if (client->incoming_video_buffer) {
    video_frame_buffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
  }
  if (client->incoming_audio_buffer) {
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_audio_buffer = NULL;
  }

  memset(client, 0, sizeof(client_info_t));

  // Set up transport-specific fields
  client->transport_encrypted = acip_transport_get_type(transport) == ACIP_TRANSPORT_WEBRTC;
  client->security_ready = client->transport_encrypted;
  client->transport = transport; // Use provided transport
  SAFE_STRNCPY(client->client_id, new_client_id, sizeof(client->client_id) - 1);
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = 0; // transport doesn't use port numbers
  atomic_store_bool(&client->active, true);
  client->server_ctx = server_ctx; // Store server context for receive thread cleanup
  log_info("Added new transport client %s from %s (transport=%p, type=%d, slot=%d)", new_client_id, client_ip,
           transport, (int)acip_transport_get_type(transport), slot);
  atomic_store_bool(&client->shutting_down, false);
  atomic_store_u64(&client->last_sent_grid_sources, 0); // Send thread updates this
  log_debug("transport client slot assigned: client_id=%s assigned to slot %d", client->client_id, slot);
  client->connected_at = time(NULL);

  // Security is borrowed from the network owner after negotiation succeeds.
  client->crypto_handshake_ctx = NULL;
  client->crypto_initialized = false;

  // Use noun-based client name for display_name as well
  SAFE_STRNCPY(client->display_name, new_client_id, sizeof(client->display_name) - 1);
}

static asciichat_error_t server_client_allocate_transport_buffers(client_info_t *client, const char *new_client_id) {
  // Create individual video buffer for this client using modern double-buffering
  client->incoming_video_buffer = video_frame_buffer_create(new_client_id);
  if (!client->incoming_video_buffer) {
    log_error("Failed to create video buffer for transport client %s", new_client_id);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create video buffer for transport client %s", new_client_id);
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  if (!client->incoming_audio_buffer) {
    log_error("Failed to create audio buffer for transport client %s", new_client_id);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for transport client %s", new_client_id);
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t server_client_initialize_transport_sync(client_info_t *client, const char *new_client_id) {
  // Finish synchronization before publishing this client in the registry.
  if (mutex_init(&client->client_state_mutex, "client_state") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize client state mutex");
  if (mutex_init(&client->send_mutex, "client_send") != ASCIICHAT_OK) {
    mutex_destroy(&client->client_state_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize client send mutex");
  }
  initialize_default_render_capabilities(client);
  if (g_audio_mixer && client->incoming_audio_buffer &&
      mixer_add_source(g_audio_mixer, new_client_id, client->incoming_audio_buffer) < 0)
    log_warn("Failed to add client %s to audio mixer", new_client_id);
  return ASCIICHAT_OK;
}

/* Allocate application state for a borrowed connection transport. Media starts
 * after the shared runtime completes negotiation and receives capabilities. */
client_info_t *add_transport_client(server_context_t *server_ctx, acip_transport_t *transport, const char *client_ip) {
  if (!server_ctx || !transport || !client_ip) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
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
    if (g_client_manager.clients[i].client_id[0] != '\0' && atomic_load_bool(&g_client_manager.clients[i].active)) {
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

  // Generate unique client ID for transport client (noun only, without counter or port)
  char new_client_id[MAX_CLIENT_ID_LEN];
  if (generate_client_id(new_client_id, sizeof(new_client_id)) != 0) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    log_error("Failed to generate unique client ID for transport client");
    return NULL;
  }

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];

  server_client_initialize_transport_slot(server_ctx, client, transport, new_client_id, client_ip, slot);

  if (server_client_allocate_transport_buffers(client, new_client_id) != ASCIICHAT_OK ||
      server_client_initialize_transport_sync(client, new_client_id) != ASCIICHAT_OK) {
    goto error_cleanup_transport;
  }

  g_client_manager.client_count = existing_count + 1; // We just added a client
  log_debug("Client count updated: now %d clients (added transport client_id=%s to slot %d)",
            g_client_manager.client_count, new_client_id, slot);

  // Add client to uthash table for O(1) lookup
  HASH_ADD_STR(g_client_manager.clients_by_id, client_id, client);
  log_debug("Added transport client %s to uthash table", new_client_id);

  // Release the write lock IMMEDIATELY after adding to hash table
  // The client is fully initialized before other threads can look it up.
  rwlock_wrunlock(&g_client_manager_rwlock);

  // Register client with session_host (for discovery mode support)
  // transport clients use INVALID_SOCKET_VALUE since they don't have a TCP socket
  if (server_ctx->session_host) {
    client->session_client_id =
        session_host_add_client(server_ctx->session_host, NULL, ACIP_TRANSPORT_BORROWED, client_ip, 0);
    if (client->session_client_id == 0) {
      log_warn("Failed to register transport client %s with session_host", new_client_id);
    } else {
      log_debug("transport client %s registered with session_host as %u", new_client_id, client->session_client_id);
    }
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return client;

error_cleanup_transport:
  // Clean up all partially allocated resources for transport client
  // NOTE: This label is reached when allocation or initialization fails BEFORE
  // the client is added to the hash table. Don't call remove_client() here.
  cleanup_client_all_buffers(client);
  memset(client, 0, sizeof(*client));
  rwlock_wrunlock(&g_client_manager_rwlock);
  return NULL;
}

typedef struct {
  client_info_t *client;
  char display_name[MAX_DISPLAY_NAME_LEN];
} server_client_removal_t;

static asciichat_error_t server_client_begin_removal(server_context_t *server_ctx, const char *client_id,
                                                     server_client_removal_t *removal) {
  // Phase 1: Mark client inactive and prepare for cleanup while holding write lock

  log_debug("SOCKET_DEBUG: Attempting to remove client %s", client_id);
  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (strcmp(client->client_id, client_id) == 0 && client->client_id[0] != '\0') {
      if (client->network_connection && !atomic_load_bool(&client->event_cleanup)) {
        network_connection_request_stop(client->network_connection);
        rwlock_wrunlock(&g_client_manager_rwlock);
        return ASCIICHAT_OK;
      }
      // Check if already being removed by another thread
      // This prevents double-free and use-after-free crashes during concurrent cleanup
      if (atomic_load_bool(&client->shutting_down)) {
        rwlock_wrunlock(&g_client_manager_rwlock);
        log_debug("Client %s already being removed by another thread, skipping", client_id);
        return ASCIICHAT_OK; // Return success - removal is in progress
      }
      // Mark as shutting down and inactive immediately to stop new operations
      log_debug("Setting active=false in remove_client (client_id=%s)", client_id);
      log_info("Removing client %s - marking inactive and clearing video flags", client_id);
      atomic_store_bool(&client->shutting_down, true);
      atomic_store_bool(&client->active, false);
      atomic_store_bool(&client->is_sending_video, false);
      atomic_store_bool(&client->is_sending_audio, false);
      removal->client = client;

      // Store display name before clearing
      SAFE_STRNCPY(removal->display_name, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

      // Shutdown packet queues to unblock send thread
      // Video now uses double buffer, no queue to shutdown

      break;
    }
  }

  // If client not found, unlock and return
  if (!removal->client) {
    rwlock_wrunlock(&g_client_manager_rwlock);
    log_warn("Cannot remove client %s: not found", client_id);
    return SET_ERRNO(ERROR_NOT_FOUND, "Cannot remove client %s: not found", client_id);
  }

  // Unregister client from session_host (for discovery mode support)
  // NOTE: Client may not be registered if crypto handshake failed before session_host registration
  if (server_ctx->session_host && removal->client->session_client_id != 0) {
    asciichat_error_t session_result =
        session_host_remove_client(server_ctx->session_host, removal->client->session_client_id);
    if (session_result != ASCIICHAT_OK) {
      // ERROR_NOT_FOUND (91) is expected if client failed crypto before being registered with session_host
      if (session_result == ERROR_NOT_FOUND) {
        log_debug("Client %s not found in session_host (likely failed crypto before registration)", client_id);
      } else {
        log_warn("Failed to unregister client %s from session_host: %s", client_id,
                 asciichat_error_format(NULL, session_result));
      }
    } else {
      log_debug("Client %s unregistered from session_host", client_id);
    }
  }

  // Release write lock before joining threads.
  // This prevents deadlock with render threads that need read locks.
  rwlock_wrunlock(&g_client_manager_rwlock);

  return ASCIICHAT_OK;
}

static void server_client_release_resources_locked(const char *client_id, server_client_removal_t *removal) {
  // Other clients' render threads can still read this client's audio buffer.
  // Removing the source waits for the mixer lock before releasing that buffer.
  if (g_audio_mixer) {
    mixer_remove_source(g_audio_mixer, client_id);
#ifdef DEBUG_AUDIO
    log_debug("Removed client %s from audio mixer", client_id);
#endif
  }

  cleanup_client_all_buffers(removal->client);

  // Remove from uthash table
  // Verify client is actually in the hash table before deleting.
  // Another thread might have already removed it.
  if (removal->client) {
    client_info_t *hash_entry = NULL;
    HASH_FIND_STR(g_client_manager.clients_by_id, removal->client->client_id, hash_entry);
    if (hash_entry == removal->client) {
      HASH_DELETE(hh, g_client_manager.clients_by_id, removal->client);
      log_debug("Removed client %s from uthash table", client_id);
    } else {
      log_warn("Client %s already removed from hash table by another thread (found=%p, expected=%p)", client_id,
               (void *)hash_entry, (void *)removal->client);
    }
  } else {
    log_warn("Failed to remove client %s from hash table (client not found)", client_id);
  }

  removal->client->crypto_handshake_ctx = NULL;
  removal->client->crypto_initialized = false;
}

static void server_client_reset_slot_locked(const char *client_id, server_client_removal_t *removal) {
  // Verify all threads have actually exited before resetting client_id.
  // Threads that are still starting (at RtlUserThreadStart) haven't checked client_id yet.
  // We must ensure threads are fully joined before zeroing the client struct.
  // Use exponential backoff for thread termination verification
  int retry_count = 0;
  const int max_retries = 5;
  while (retry_count < max_retries && (thread_is_initialized(&removal->client->video_render_thread) ||
                                       thread_is_initialized(&removal->client->audio_render_thread))) {
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
  removal->client->client_id[0] = '\0'; // Clear the client_id string

  // Wait for threads to observe the client_id reset
  // Use sufficient delay for memory visibility across all CPU cores
  platform_sleep_us(5 * US_PER_MS_INT); // 5ms delay for memory barrier propagation

  // Destroy mutexes and condition variables
  // Always destroy these even if threads did not join properly
  // to prevent issues when the slot is reused
  mutex_destroy(&removal->client->client_state_mutex);
  mutex_destroy(&removal->client->send_mutex);

  // Clear client structure
  // NOTE: After memset, the mutex handles are zeroed but the OS resources
  // have been released by the destroy calls above
  memset(removal->client, 0, sizeof(client_info_t));
}

static bool server_client_finish_removal(const char *client_id, server_client_removal_t *removal) {
  // Phase 3: Clean up resources with write lock
  rwlock_wrlock(&g_client_manager_rwlock);

  // Re-validate target_client pointer after reacquiring lock.
  // Another thread might have invalidated the pointer while we had the lock released.
  if (removal->client) {
    // Verify client_id still matches and client is still in shutting_down state
    bool still_shutting_down = atomic_load_bool(&removal->client->shutting_down);
    if (strcmp(removal->client->client_id, client_id) != 0 || !still_shutting_down) {
      log_warn("Client %s pointer invalidated during thread cleanup (id=%s, shutting_down=%d)", client_id,
               removal->client->client_id, still_shutting_down);
      rwlock_wrunlock(&g_client_manager_rwlock);
      return false; // Another thread completed the cleanup
    }
  }

  server_client_release_resources_locked(client_id, removal);

  server_client_reset_slot_locked(client_id, removal);

  // Recalculate client count
  int remaining_count = 0;
  for (int j = 0; j < MAX_CLIENTS; j++) {
    if (g_client_manager.clients[j].client_id[0] != '\0') {
      remaining_count++;
    }
  }
  g_client_manager.client_count = remaining_count;

  log_debug("Client removed: client_id=%s (%s) removed, remaining clients: %d", client_id, removal->display_name,
            remaining_count);

  rwlock_wrunlock(&g_client_manager_rwlock);

  return true;
}

int remove_client(server_context_t *server_ctx, const char *client_id) {
  if (!server_ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Cannot remove client %s: NULL server_ctx", client_id);
    return -1;
  }

  server_client_removal_t removal = {0};
  if (server_client_begin_removal(server_ctx, client_id, &removal) != ASCIICHAT_OK) {
    return -1;
  }
  if (!removal.client) {
    return 0; // Another thread is already removing this client.
  }

  if (stop_client_render_threads(removal.client) != ASCIICHAT_OK)
    return -1;
  if (!server_client_finish_removal(client_id, &removal)) {
    return 0;
  }

  broadcast_server_state_to_all_clients();
  return 0;
}

/* ============================================================================
 * Client Thread Functions
 * ============================================================================
 */

// Forward declaration for ACIP server callbacks (defined later in file)
static const acip_server_callbacks_t g_acip_server_callbacks;

asciichat_error_t server_dispatch_packet(client_info_t *client, packet_type_t type, const void *data, size_t length) {
  if (type == PACKET_TYPE_ACIP_SETTINGS_SYNC)
    return handle_runtime_settings(client, data, length);
  return acip_handle_server_packet(client->transport, type, data, length, client, &g_acip_server_callbacks);
}

/**
 * @brief Async dispatch thread - processes queued received packets
 *
 * Decouples receive from dispatch to prevent backpressure on the network socket.
 * Received packets are queued by the receive thread, and processed asynchronously here.
 * This allows the receive thread to keep accepting packets while dispatch processes them.
 */
static void broadcast_state(network_connection_t *connection, void *arg) {
  if (network_connection_state(connection) != NETWORK_READY)
    return;
  server_connection_state_t *state = network_connection_context(connection);
  client_info_t *client = state ? atomic_ptr_load(&state->client) : NULL;
  if (!client || !atomic_load_bool(&client->event_ready) || !atomic_load_bool(&client->active))
    return;
  mutex_lock(&client->send_mutex);
  acip_send_server_state(network_connection_transport(connection), arg);
  mutex_unlock(&client->send_mutex);
}
static server_state_packet_t server_state_snapshot(void) {
  server_state_packet_t state = {0};
  rwlock_rdlock(&g_client_manager_rwlock);
  uint32_t active = 0;
  for (int i = 0; i < MAX_CLIENTS; ++i)
    if (atomic_load_bool(&g_client_manager.clients[i].active) &&
        atomic_load_bool(&g_client_manager.clients[i].is_sending_video))
      ++active;
  state.connected_client_count = HOST_TO_NET_U32(g_client_manager.client_count);
  state.active_client_count = HOST_TO_NET_U32(active);
  rwlock_rdunlock(&g_client_manager_rwlock);
  return state;
}
asciichat_error_t server_send_state(acip_transport_t *transport) {
  server_state_packet_t state = server_state_snapshot();
  return acip_send_server_state(transport, &state);
}
void broadcast_server_state_to_all_clients(void) {
  if (!g_client_manager_rwlock_initialized)
    return;
  server_state_packet_t state = server_state_snapshot();
  network_server_foreach(session_server_like_get_network_server(), broadcast_state, &state);
}

int start_transport_client_threads(server_context_t *server_ctx, const char *client_id) {
  if (!server_ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Server context is NULL");
    return -1;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    SET_ERRNO(ERROR_NOT_FOUND, "Client %s not found", client_id);
    return -1;
  }

  log_debug("Starting application workers for client %s...", client_id);
  return start_client_threads(server_ctx, client);
}

void cleanup_client_media_buffers(client_info_t *client) {
  if (!client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client is NULL");
    return;
  }

  if (client->digital_rain) {
    digital_rain_destroy(client->digital_rain);
    client->digital_rain = NULL;
  }

  if (client->incoming_video_buffer) {
    video_frame_buffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
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

  // Clean up video decoder (persistent per-client decoder)
  if (client->video_decoder_ctx) {
    avcodec_free_context((AVCodecContext **)&client->video_decoder_ctx);
    client->video_decoder_ctx = NULL;
  }
  if (client->video_decode_frame) {
    av_frame_free((AVFrame **)&client->video_decode_frame);
    client->video_decode_frame = NULL;
  }
  if (client->video_rgb_frame) {
    av_frame_free((AVFrame **)&client->video_rgb_frame);
    client->video_rgb_frame = NULL;
  }
  if (client->video_sws_ctx) {
    sws_freeContext(client->video_sws_ctx);
    client->video_sws_ctx = NULL;
  }
}

void cleanup_client_packet_queues(client_info_t *client) {
  if (!client)
    return;

  // Async dispatch: clean up received packet queue

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
static void acip_server_on_protocol_version(const protocol_version_packet_t *version, void *client_ctx, void *app_ctx);
static void acip_server_on_image_frame(const image_frame_packet_t *header, const void *pixel_data, size_t data_len,
                                       void *client_ctx, void *app_ctx);
static void acip_server_on_image_frame_h264(uint32_t width, uint32_t height, uint8_t flags, const void *data,
                                            size_t data_len, void *client_ctx, void *app_ctx);
static void acip_server_on_image_frame_h265(uint32_t width, uint32_t height, uint8_t flags, const void *h265_data,
                                            size_t data_len, void *client_ctx, void *app_ctx);
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
 * Handles all ACIP packet types from clients including crypto handshake and rekey protocols.
 * Callback wrappers delegate to existing handler functions in protocol.c
 */
static const acip_server_callbacks_t g_acip_server_callbacks = {
    .on_protocol_version = acip_server_on_protocol_version,
    .on_image_frame = acip_server_on_image_frame,
    .on_image_frame_h265 = acip_server_on_image_frame_h265,
    .on_image_frame_h264 = acip_server_on_image_frame_h264,
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

  // Reject empty or missing RGB payloads before diagnostics sample the first
  // pixel. Malformed frames must not turn a logging path into a server crash.
  if (!header || !client || !pixel_data || data_len < 3) {
    log_warn("Ignoring empty or invalid WebRTC image frame payload (data_len=%zu)", data_len);
    return;
  }

  log_dev("CALLBACK_IMAGE_FRAME: client_id=%s width=%u height=%u pixel_format=%u compressed_size=%u data_len=%zu",
          client->client_id, header->width, header->height, header->pixel_format, header->compressed_size, data_len);

  size_t center_pixel_offset = ((size_t)(header->height / 2) * header->width + header->width / 2) * 3;
  const uint8_t *first_pixel = pixel_data;
  const uint8_t *center_pixel =
      center_pixel_offset + 3 <= data_len ? (const uint8_t *)pixel_data + center_pixel_offset : first_pixel;
  log_debug_every(US_PER_SEC_INT,
                  "WebRTC pixel sample client=%s size=%ux%u bytes=%zu first=%02x%02x%02x center=%02x%02x%02x",
                  client->client_id, header->width, header->height, data_len, first_pixel[0], first_pixel[1],
                  first_pixel[2], center_pixel[0], center_pixel[1], center_pixel[2]);

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
  // Use atomic_exchange to atomically set flag and check old value
  bool was_sending_video = atomic_exchange_bool(&client->is_sending_video, true);
  if (!was_sending_video) {
    log_info("Client %s auto-enabled video stream (received IMAGE_FRAME)", client->client_id);
    log_info_client(client, "First video frame received - streaming active");
  }

  // Measure frame arrival at the server independently of composition and
  // delivery so low FPS can be attributed to the publishing client or server.
  uint64_t receive_now_ns = time_get_ns();
  double receive_fps = 0.0;
  uint32_t received_in_window = 0;
  uint64_t receive_window_ns = 0;
  mutex_lock(&client->client_state_mutex);
  client->frames_received_logged++;
  if (client->video_receive_rate_window_ns == 0) {
    client->video_receive_rate_window_ns = receive_now_ns;
  }
  client->video_receive_rate_window_frames++;
  receive_window_ns = receive_now_ns - client->video_receive_rate_window_ns;
  if (receive_window_ns >= NS_PER_SEC_INT) {
    received_in_window = client->video_receive_rate_window_frames;
    receive_fps = (double)received_in_window * NS_PER_SEC_INT / (double)receive_window_ns;
    client->video_receive_rate_window_ns = receive_now_ns;
    client->video_receive_rate_window_frames = 0;
  }
  bool log_frame_count = client->frames_received_logged % 25000 == 0;
  uint32_t total_received = client->frames_received_logged;
  mutex_unlock(&client->client_state_mutex);
  if (log_frame_count) {
    char pretty[64];
    format_bytes_pretty(data_len, pretty, sizeof(pretty));
    log_debug("Client %s has sent %u IMAGE_FRAME packets (%s)", client->client_id, total_received, pretty);
  }
  if (received_in_window > 0) {
    log_info("Server image ingress client=%s fps=%.1f frames=%u elapsed=%.2fs", client->client_id, receive_fps,
             received_in_window, (double)receive_window_ns / (double)NS_PER_SEC_INT);
  }

  // Sample a small prefix for duplicate-frame diagnostics; hashing every pixel
  // here repeats work performed by the image conversion path on every frame.
  size_t hash_len = data_len < 1000 ? data_len : 1000;
  uint32_t incoming_pixel_hash = hash_len ? fnv1a_hash_bytes(pixel_data, hash_len) : FNV1A_32_OFFSET_BASIS;

  // Per-client hash tracking to detect duplicate frames
  bool is_new_frame = (incoming_pixel_hash != client->last_received_frame_hash);

  // Inspect first few pixels of incoming frame
  uint32_t first_pixel_rgb = 0;
  if (data_len >= 3) {
    first_pixel_rgb = ((uint32_t)((unsigned char *)pixel_data)[0] << 16) |
                      ((uint32_t)((unsigned char *)pixel_data)[1] << 8) | (uint32_t)((unsigned char *)pixel_data)[2];
  }
  if (is_new_frame) {
    log_dev("RECV_FRAME #%u NEW: client=%s dimensions=%ux%u pixel_size=%zu hash=0x%08x first_rgb=0x%06x prev=0x%08x",
            client->frames_received, client->client_id, header->width, header->height, data_len, incoming_pixel_hash,
            first_pixel_rgb, client->last_received_frame_hash);
    client->last_received_frame_hash = incoming_pixel_hash;
  } else {
    log_dev("RECV_FRAME #%u DUP: client=%s dimensions=%ux%u pixel_size=%zu hash=0x%08x first_rgb=0x%06x",
            client->frames_received, client->client_id, header->width, header->height, data_len, incoming_pixel_hash,
            first_pixel_rgb);
  }
  if (client->frames_received_logged > 0 && client->frames_received_logged % 60 == 0) {
    log_info("Incoming video signature client=%s frames=%u state=%s hash=0x%08x size=%ux%u bytes=%zu",
             client->client_id, client->frames_received_logged, is_new_frame ? "changing" : "duplicate",
             incoming_pixel_hash, header->width, header->height, data_len);
  }

  // Store frame data to incoming_video_buffer
  // pixel_data points to websocket reassembly buffer which will be freed after callback returns,
  // so we must copy it immediately into the persistent frame buffer (which is pre-allocated to 2MB)
  if (client->incoming_video_buffer) {
    video_frame_buffer_t *vfb = client->incoming_video_buffer;
    log_dev("VFB: client=%s allocated_size=%zu active=%d", client->client_id, vfb->allocated_buffer_size, vfb->active);

    video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);
    if (frame && frame->data && data_len > 0) {
      // Debug: Check frame buffer capacity
      size_t buffer_capacity = client->incoming_video_buffer->allocated_buffer_size;
      size_t total_size = sizeof(uint32_t) * 2 + data_len;

      log_dev("FRAME_STORAGE: client=%s capacity=%zu data_len=%zu total=%zu", client->client_id, buffer_capacity,
              data_len, total_size);

      // CRITICAL: Bounds check BEFORE memcpy to prevent heap-buffer-overflow
      // If buffer is too small, skip frame storage instead of overflowing
      // NOTE: Use >= not > because we need one free byte after the last byte
      if (total_size >= buffer_capacity) {
        log_error("FRAME_STORAGE_OVERFLOW_PREVENTED: client=%s, need=%zu bytes, capacity=%zu bytes (cannot store "
                  "frame, would overflow at byte %zu)",
                  client->client_id, total_size, buffer_capacity, buffer_capacity);
        disconnect_client_for_bad_data(client, "FRAME_STORAGE_BUFFER_TOO_SMALL");
        return;
      }

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
}

/** Initialize a persistent decoder, replacing it on a codec switch. */
static asciichat_error_t video_decoder_ensure_initialized(client_info_t *client, enum AVCodecID codec_id) {
  AVCodecContext *ctx = client->video_decoder_ctx;
  if (ctx && ctx->codec_id == codec_id && client->video_decode_frame && client->video_rgb_frame)
    return ASCIICHAT_OK;
  if (ctx)
    avcodec_free_context((AVCodecContext **)&client->video_decoder_ctx);
  const AVCodec *codec = avcodec_find_decoder(codec_id);
  if (!codec)
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "Video decoder %s unavailable", avcodec_get_name(codec_id));
  ctx = avcodec_alloc_context3(codec);
  if (!ctx)
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate video decoder");
  ctx->max_pixels = 8192 * 8192;
  if (avcodec_open2(ctx, codec, NULL) < 0) {
    avcodec_free_context(&ctx);
    return SET_ERRNO(ERROR_INVALID_STATE, "Failed to open video decoder");
  }
  client->video_decoder_ctx = ctx;
  if (!client->video_decode_frame)
    client->video_decode_frame = av_frame_alloc();
  if (!client->video_rgb_frame)
    client->video_rgb_frame = av_frame_alloc();
  if (!client->video_decode_frame || !client->video_rgb_frame)
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate decoded video frames");
  return ASCIICHAT_OK;
}

static int server_receive_decoded_frame(AVCodecContext *ctx, AVFrame *frame) {
  ASCII_OTEL_SCOPE(FFMPEG_FRAME_RECEIVE);
  int result = avcodec_receive_frame(ctx, frame);
#ifdef ASCIICHAT_WITH_OTEL
  if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
    ascii_otel_error("FFmpeg server frame receive failed");
#endif
  return result;
}

static void acip_server_on_encoded_frame(uint32_t width, uint32_t height, uint8_t flags, const void *data,
                                         size_t data_len, void *client_ctx, enum AVCodecID codec_id) {
  ASCII_OTEL_SCOPE(FFMPEG_VIDEO_DECODE);
#ifdef ASCIICHAT_WITH_OTEL
  ascii_otel_scope_int(&otel_scope, "media.codec.id", codec_id);
  ascii_otel_scope_int(&otel_scope, "video.width", width);
  ascii_otel_scope_int(&otel_scope, "video.height", height);
#endif
  client_info_t *client = client_ctx;
  video_codec_t capability = codec_id == AV_CODEC_ID_H264 ? VIDEO_CODEC_H264 : VIDEO_CODEC_H265;
  if (!VIDEO_CODEC_SUPPORTED(client->codec_capabilities_video, capability) || width == 0 || height == 0 ||
      width > 8192 || height > 8192 || !data || data_len == 0 || data_len > INT_MAX) {
    disconnect_client_for_bad_data(client, "Invalid encoded video frame or unadvertised codec");
    return;
  }
  if (video_decoder_ensure_initialized(client, codec_id) != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "Failed to initialize video decoder");
    return;
  }
  AVCodecContext *ctx = client->video_decoder_ctx;
  AVFrame *frame = client->video_decode_frame;
  AVFrame *rgb = client->video_rgb_frame;
  if (flags & 2)
    avcodec_flush_buffers(ctx);
  AVPacket *packet = av_packet_alloc();
  if (!packet || av_new_packet(packet, (int)data_len) < 0) {
    av_packet_free(&packet);
    return;
  }
  // FFmpeg allocates the required zero padding after compressed input.
  memcpy(packet->data, data, data_len);
  uint64_t decode_start = time_get_ns();
  int result;
  {
    ASCII_OTEL_SCOPE(FFMPEG_PACKET_SEND);
    result = avcodec_send_packet(ctx, packet);
#ifdef ASCIICHAT_WITH_OTEL
    if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
      ascii_otel_error("FFmpeg server packet send failed");
#endif
  }
  av_packet_free(&packet);
  if (result < 0) {
    log_warn("%s input decode failed: %d", avcodec_get_name(codec_id), result);
    return;
  }
  while ((result = server_receive_decoded_frame(ctx, frame)) == 0) {
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_DECODE, time_get_ns() - decode_start);
    if (frame->width != (int)width || frame->height != (int)height) {
      disconnect_client_for_bad_data(client, "Decoded video dimensions do not match packet header");
      return;
    }
    struct SwsContext *sws =
        sws_getCachedContext(client->video_sws_ctx, frame->width, frame->height, frame->format, frame->width,
                             frame->height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    client->video_sws_ctx = sws;
    if (!sws)
      return;
    // swscale SIMD output needs aligned rows, including padding after the last row.
    int rgb_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width, frame->height, 32);
    size_t packed_size = (size_t)frame->width * frame->height * 3;
    if (rgb_size <= 0 || !client->incoming_video_buffer ||
        packed_size + 8 > client->incoming_video_buffer->allocated_buffer_size)
      return;
    uint8_t *pixels = SAFE_MALLOC((size_t)rgb_size + AV_INPUT_BUFFER_PADDING_SIZE, uint8_t *);
    if (!pixels)
      return;
    av_image_fill_arrays(rgb->data, rgb->linesize, pixels, AV_PIX_FMT_RGB24, frame->width, frame->height, 32);
    int rows;
    {
      ASCII_OTEL_SCOPE(FFMPEG_SCALE);
      rows = sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height, rgb->data,
                       rgb->linesize);
#ifdef ASCIICHAT_WITH_OTEL
      if (rows != frame->height)
        ascii_otel_error("FFmpeg server frame scale failed");
#endif
    }
    if (rows == frame->height) {
      atomic_store_bool(&client->is_sending_video, true);
      video_frame_t *vf = video_frame_begin_write(client->incoming_video_buffer);
      if (vf && vf->data) {
        uint32_t dimensions[2] = {HOST_TO_NET_U32(width), HOST_TO_NET_U32(height)};
        memcpy(vf->data, dimensions, sizeof(dimensions));
        for (int y = 0; y < frame->height; y++) {
          memcpy((uint8_t *)vf->data + sizeof(dimensions) + (size_t)y * width * 3,
                 rgb->data[0] + (size_t)y * rgb->linesize[0], (size_t)width * 3);
        }
        vf->size = sizeof(dimensions) + packed_size;
        vf->width = width;
        vf->height = height;
        vf->is_keyframe = (flags & 1) != 0;
        vf->capture_timestamp_ns = time_get_ns();
        vf->sequence_number = ++client->frames_received;
        video_frame_commit(client->incoming_video_buffer);
        stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_DECODED, 1);
      }
    }
    SAFE_FREE(pixels);
  }
  if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
    log_warn("%s output decode failed: %d", avcodec_get_name(codec_id), result);
}

static void acip_server_on_image_frame_h265(uint32_t width, uint32_t height, uint8_t flags, const void *data,
                                            size_t data_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  acip_server_on_encoded_frame(width, height, flags, data, data_len, client_ctx, AV_CODEC_ID_HEVC);
}

static void acip_server_on_image_frame_h264(uint32_t width, uint32_t height, uint8_t flags, const void *data,
                                            size_t data_len, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  acip_server_on_encoded_frame(width, height, flags, data, data_len, client_ctx, AV_CODEC_ID_H264);
}

void handle_image_frame_h264_packet(client_info_t *client, const void *data, size_t len) {
  asciichat_error_t result = acip_handle_server_packet(client->transport, PACKET_TYPE_IMAGE_FRAME_H264, data, len,
                                                       client, &g_acip_server_callbacks);
  if (result != ASCIICHAT_OK)
    disconnect_client_for_bad_data(client, "Invalid H.264 packet");
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
                  client->client_id, num_samples, atomic_load_bool(&client->is_sending_audio));

  if (!atomic_load_bool(&client->is_sending_audio)) {
    log_debug("Ignoring audio batch - client %s not in audio streaming mode", client->client_id);
    return;
  }

  if (client->incoming_audio_buffer) {
    asciichat_error_t write_result =
        audio_ring_buffer_write(client->incoming_audio_buffer, (float *)samples, num_samples);
    if (write_result != ASCIICHAT_OK) {
      log_error("Failed to write decoded audio batch to buffer: %s", asciichat_error_format(NULL, write_result));
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
  // The ACIP parser has already converted these flags to host byte order.
  // The shared packet handler consumes the network-order payload used by the
  // TCP packet dispatcher, so restore that representation at this boundary.
  uint32_t stream_types_net = HOST_TO_NET_U32(stream_types);
  handle_stream_start_packet(client, &stream_types_net, sizeof(stream_types_net));
}

static void acip_server_on_stream_stop(uint32_t stream_types, void *client_ctx, void *app_ctx) {
  (void)app_ctx;
  client_info_t *client = (client_info_t *)client_ctx;
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
  // Keep the lock through the packet write: this handler runs on the receive
  // thread and otherwise races the video/audio sender on TCP.
  mutex_lock(&client->send_mutex);
  if (atomic_load_bool(&client->shutting_down) || !client->transport) {
    mutex_unlock(&client->send_mutex);
    return; // Client is shutting down, skip pong
  }
  acip_transport_t *pong_transport = client->transport;
  asciichat_error_t pong_result = acip_send_pong(pong_transport);
  mutex_unlock(&client->send_mutex);

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
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - binary packet data, not string
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
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - binary packet data, not string
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
      crypto_handshake_process_rekey_request(client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST from client %s: %d", client->client_id, crypto_result);
    return;
  }

  // Send REKEY_RESPONSE
  mutex_lock(&client->client_state_mutex);
  // Get transport reference briefly to avoid deadlock on transport buffers
  mutex_lock(&client->send_mutex);
  if (atomic_load_bool(&client->shutting_down) || !client->transport) {
    mutex_unlock(&client->send_mutex);
    mutex_unlock(&client->client_state_mutex);
    return; // Client is shutting down
  }
  acip_transport_t *rekey_transport = client->transport;
  mutex_unlock(&client->send_mutex);

  // Network I/O happens OUTSIDE the send_mutex (client_state_mutex still held for crypto state)
  crypto_result = crypto_handshake_rekey_response(client->crypto_handshake_ctx, rekey_transport);
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
      crypto_handshake_process_rekey_response(client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE from client %s: %d", client->client_id, crypto_result);
    return;
  }

  // Send REKEY_COMPLETE to confirm and activate new key
  mutex_lock(&client->client_state_mutex);
  // Get transport reference briefly to avoid deadlock on transport buffers
  mutex_lock(&client->send_mutex);
  if (atomic_load_bool(&client->shutting_down) || !client->transport) {
    mutex_unlock(&client->send_mutex);
    mutex_unlock(&client->client_state_mutex);
    return; // Client is shutting down
  }
  acip_transport_t *complete_transport = client->transport;
  mutex_unlock(&client->send_mutex);

  // Network I/O happens OUTSIDE the send_mutex (client_state_mutex still held for crypto state)
  crypto_result = crypto_handshake_rekey_complete(client->crypto_handshake_ctx, complete_transport);
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
      crypto_handshake_process_rekey_complete(client->crypto_handshake_ctx, (void *)payload, payload_len);
  mutex_unlock(&client->client_state_mutex);

  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_COMPLETE from client %s: %d", client->client_id, crypto_result);
  } else {
    log_debug("Session rekeying completed successfully with client %s", client->client_id);
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
  if (type == 5000) { // CLIENT_CAPABILITIES
    log_debug("CLIENT: client_id=%s, data=%p, len=%zu", client->client_id, data, len);
  }

  // Rate limiting: Check and record packet-specific rate limits
  if (g_rate_limiter) {
    bool allowed = false;
    asciichat_errno_scope_t limit_scope = errno_scope_begin();
    asciichat_error_t result = check_and_record_packet_rate_limit(g_rate_limiter, client->client_ip, type, &allowed);
    if (result != ASCIICHAT_OK || !allowed) {
      if (result == ASCIICHAT_OK)
        SET_ERRNO(ERROR_RATE_LIMITED, "Packet quota exceeded");
      LOG_ERRNO_IF_SET("Packet rate-limit enforcement rejected client");
      asciichat_errno_scope_t notify_scope = errno_scope_begin();
      mutex_lock(&client->send_mutex);
      asciichat_error_t sent =
          acip_send_error(client->transport, result == ASCIICHAT_OK ? ERROR_RATE_LIMITED : ERROR_INTERNAL, NULL);
      mutex_unlock(&client->send_mutex);
      if (sent != ASCIICHAT_OK)
        LOG_ERRNO_IF_SET("Could not notify client about rate-limit decision");
      errno_scope_end(notify_scope, ASCIICHAT_ERRNO_HANDLED);
      if (result == ASCIICHAT_OK)
        errno_scope_end(limit_scope, ASCIICHAT_ERRNO_HANDLED);
      atomic_store_bool(&client->active, false);
      return;
    }
    ASSERT_NO_ERRNO_SINCE(limit_scope);
    errno_scope_end(limit_scope, ASCIICHAT_ERRNO_HANDLED);
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
