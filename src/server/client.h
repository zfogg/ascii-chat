/**
 * @file server/client.h
 * @ingroup server_client
 * @brief Per-client state management and lifecycle orchestration
 *
 * This header provides server-specific client management functions.
 * The client_info_t structure and network logging macros are defined
 * in lib/network/client.h.
 */
#pragma once

#include <ascii-chat/network/client.h>

// Forward declaration to avoid circular dependency with main.h
// Files that need the full definition should include main.h themselves
typedef struct server_context_t server_context_t;

/**
 * @brief Global client manager structure for server-side client coordination
 *
 * Manages all connected clients in the ascii-chat server. Provides O(1) client
 * lookup via hashtable while maintaining array-based storage for iteration.
 * This structure serves as the central coordination point for client lifecycle
 * management.
 *
 * ARCHITECTURE:
 * =============
 * The client manager uses a dual-storage approach:
 * - Array (clients[]): Fast iteration, stable pointers, sequential access
 * - Hashtable (client_hashtable): O(1) lookup by client_id
 *
 * This design provides:
 * - O(1) lookups via hashtable
 * - O(n) iteration via array (for stats, rendering, etc.)
 * - Stable pointers (array elements don't move)
 * - Linear memory layout (cache-friendly)
 *
 * THREAD SAFETY:
 * ==============
 * Protected by g_client_manager_rwlock (reader-writer lock):
 * - Read operations (lookups, stats): Acquire read lock (concurrent)
 * - Write operations (add/remove): Acquire write lock (exclusive)
 *
 * LOCK ORDERING:
 * - Always acquire g_client_manager_rwlock BEFORE per-client mutexes
 * - Prevents deadlocks in multi-client operations
 *
 * STRUCTURE FIELDS:
 * =================
 * - clients[]: Array of client_info_t structures (backing storage)
 * - client_hashtable: Hashtable for O(1) client_id -> client_info_t* lookups
 * - client_count: Current number of active clients
 * - mutex: Legacy mutex (mostly replaced by rwlock)
 * - next_client_id: Monotonic counter for unique client IDs
 *
 * @note The hashtable uses client_id as key and points to elements in clients[] array.
 * @note next_client_id is atomic for thread-safe ID assignment.
 * @note All client access should go through find_client_by_id() or find_client_by_socket()
 *       to ensure proper locking.
 *
 * @ingroup server_client
 */
typedef struct {
  /** @brief Array of client_info_t structures (backing storage) */
  client_info_t clients[MAX_CLIENTS];
  /** @brief uthash head pointer for O(1) client_id -> client_info_t* lookups */
  client_info_t *clients_by_id;
  /** @brief Current number of active clients */
  int client_count;
  /** @brief Legacy mutex (mostly replaced by rwlock) */
  mutex_t mutex;
} client_manager_t;

// Global client manager
extern client_manager_t g_client_manager;
extern rwlock_t g_client_manager_rwlock;

// Client management functions
int add_client(server_context_t *server_ctx, socket_t socket, const char *client_ip, int port);
int add_webrtc_client(server_context_t *server_ctx, acip_transport_t *transport, const char *client_ip,
                      bool start_threads);
int start_webrtc_client_threads(server_context_t *server_ctx, const char *client_id);
int remove_client(server_context_t *server_ctx, const char *client_id);
client_info_t *find_client_by_id(const char *client_id);
client_info_t *find_client_by_socket(socket_t socket);
void cleanup_client_media_buffers(client_info_t *client);
void cleanup_client_packet_queues(client_info_t *client);

// Client thread functions
void *client_receive_thread(void *arg);
void stop_client_threads(client_info_t *client);

// Packet processing functions
int process_encrypted_packet(client_info_t *client, packet_type_t *type, void **data, size_t *len, uint32_t *sender_id);
void process_decrypted_packet(client_info_t *client, packet_type_t type, void *data, size_t len);

// Client initialization
void initialize_client_info(client_info_t *client);
