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

#include "client.h"
#include "protocol.h"
#include "render.h"
#include "stream.h"
#include "common.h"
#include "buffer_pool.h"
#include "network.h"
#include "packet_queue.h"
#include "ringbuffer.h"
#include "os/audio.h"
#include "mixer.h"
#include "hashtable.h"

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
extern atomic_bool g_should_exit;    ///< Global shutdown flag from main.c
extern mixer_t *g_audio_mixer;       ///< Global audio mixer from main.c

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
 * - Thread Safety: Requires external read lock on g_client_manager_rwlock
 *
 * USAGE PATTERNS:
 * - Connection establishment during add_client() processing
 * - Socket error handling and cleanup operations
 * - Debugging and diagnostic functions
 *
 * @param socket Platform-abstracted socket descriptor to search for
 * @return Pointer to client_info_t if found, NULL if not found
 *
 * @warning Requires caller to hold g_client_manager_rwlock read lock
 * @note Only searches active clients (avoids returning stale entries)
 */
client_info_t *find_client_by_socket(socket_t socket) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].socket == socket && g_client_manager.clients[i].active) {
      return &g_client_manager.clients[i];
    }
  }
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
    if (g_client_manager.clients[i].client_id == 0) {
      if (slot == -1) {
        slot = i; // Take first available slot
      }
    } else {
      existing_count++;
    }
  }

  if (slot == -1) {
    rwlock_unlock(&g_client_manager_rwlock);
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
  client->client_id = ++g_client_manager.next_client_id;
  SAFE_STRNCPY(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  client->active = true;
  log_info("CLIENT SLOT ASSIGNED: client_id=%u assigned to slot %d, socket=%d", client->client_id, slot, socket);
  client->connected_at = time(NULL);
  snprintf(client->display_name, sizeof(client->display_name), "Client%u", client->client_id);

  // Create individual video buffer for this client
  client->incoming_video_buffer = framebuffer_create_multi(64); // Increased to 64 frames to handle bursts
  if (!client->incoming_video_buffer) {
    log_error("Failed to create video buffer for client %u", client->client_id);
    rwlock_unlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create();
  if (!client->incoming_audio_buffer) {
    log_error("Failed to create audio buffer for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
    rwlock_unlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create packet queues for outgoing data
  // Use node pools but share the global buffer pool
  client->audio_queue =
      packet_queue_create_with_pools(100, 200, false); // Max 100 audio packets, 200 nodes, NO local buffer pool
  if (!client->audio_queue) {
    log_error("Failed to create audio queue for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    rwlock_unlock(&g_client_manager_rwlock);
    return -1;
  }

  client->video_queue = packet_queue_create_with_pools(
      500, 1000, false); // Max 500 packets (both image frames in + ASCII frames out), 1000 nodes, NO local buffer pool
  if (!client->video_queue) {
    log_error("Failed to create video queue for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    packet_queue_destroy(client->audio_queue);
    client->incoming_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    client->audio_queue = NULL;
    rwlock_unlock(&g_client_manager_rwlock);
    return -1;
  }

  g_client_manager.client_count = existing_count + 1; // We just added a client
  log_info("CLIENT COUNT UPDATED: now %d clients (added client_id=%u to slot %d)", g_client_manager.client_count,
           client->client_id, slot);

  // Add client to hash table for O(1) lookup
  if (!hashtable_insert(g_client_manager.client_hashtable, client->client_id, client)) {
    log_error("Failed to add client %u to hash table", client->client_id);
    // Continue anyway - hash table is optimization, not critical
  }

  // Register this client's audio buffer with the mixer
  if (g_audio_mixer && client->incoming_audio_buffer) {
    if (mixer_add_source(g_audio_mixer, client->client_id, client->incoming_audio_buffer) < 0) {
      log_warn("Failed to add client %u to audio mixer", client->client_id);
    } else {
#ifdef DEBUG_AUDIO
      log_debug("Added client %u to audio mixer", client->client_id);
#endif
    }
  }

  rwlock_unlock(&g_client_manager_rwlock);

  // Start threads for this client
  if (ascii_thread_create(&client->receive_thread, client_receive_thread, client) != 0) {
    log_error("Failed to create receive thread for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  // Start send thread for this client
  if (ascii_thread_create(&client->send_thread, client_send_thread_func, client) != 0) {
    log_error("Failed to create send thread for client %u", client->client_id);
    // Join the receive thread before cleaning up to prevent race conditions
    ascii_thread_join(&client->receive_thread, NULL);
    // Now safe to remove client (won't double-free since first thread creation succeeded)
    remove_client(client->client_id);
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

  if (packet_queue_enqueue(client->video_queue, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state), 0, true) < 0) {
    log_warn("Failed to queue initial server state for client %u", client->client_id);
    return -1;
  }
#ifdef DEBUG_NETWORK
  log_info("Queued initial server state for client %u: %u connected clients", client->client_id,
            state.connected_client_count);
#endif

  // NEW: Create per-client rendering threads
  if (create_client_render_threads(client) != 0) {
    log_error("Failed to create render threads for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  // Broadcast server state to ALL clients AFTER the new client is fully set up
  // This notifies all clients (including the new one) about the updated grid
  broadcast_server_state_to_all_clients();

  return client->client_id;
}

int remove_client(uint32_t client_id) {
  rwlock_wrlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    // Remove the client if it matches the ID (regardless of active status)
    // This allows cleaning up clients that have been marked inactive
    if (client->client_id == client_id && client->client_id != 0) {
      client->active = false;

      // Clean up client resources
      if (client->socket > 0) {
        close(client->socket);
        client->socket = 0;
      }

      // Free cached frame if we have one
      if (client->has_cached_frame && client->last_valid_frame.data) {
        buffer_pool_free(client->last_valid_frame.data, client->last_valid_frame.size);
        client->last_valid_frame.data = NULL;
        client->has_cached_frame = false;
      }

      // Only destroy buffers if they haven't been destroyed already
      // Use temporary pointers to avoid race conditions
      framebuffer_t *video_buffer = client->incoming_video_buffer;
      audio_ring_buffer_t *audio_buffer = client->incoming_audio_buffer;

      client->incoming_video_buffer = NULL;
      client->incoming_audio_buffer = NULL;

      if (video_buffer) {
        framebuffer_destroy(video_buffer);
      }

      if (audio_buffer) {
        audio_ring_buffer_destroy(audio_buffer);
      }

      // Shutdown and destroy packet queues
      if (client->audio_queue) {
        packet_queue_shutdown(client->audio_queue);
      }
      if (client->video_queue) {
        packet_queue_shutdown(client->video_queue);
      }

      // Wait for send thread to exit if it was created
      // Note: We must join regardless of send_thread_running flag to prevent race condition
      // where thread sets flag to false just before we check it, causing missed cleanup
      if (ascii_thread_is_initialized(&client->send_thread)) {
        // The shutdown signal above will cause the send thread to exit
        int join_result = ascii_thread_join(&client->send_thread, NULL);
        if (join_result == 0) {
          log_debug("Send thread for client %u has terminated", client_id);
        } else {
          log_warn("Failed to join send thread for client %u: %d", client_id, join_result);
          return -1;
        }
      }

      // Join receive thread if it exists and we're not in the receive thread context
      // Note: simplified thread cleanup without thread_equal check
      {
        int join_result = ascii_thread_join(&client->receive_thread, NULL);
        if (join_result == 0) {
          log_debug("Receive thread for client %u has terminated", client_id);
        } else {
          log_warn("Failed to join receive thread for client %u: %d", client_id, join_result);
          return -1;
        }
      }

      // NEW: Destroy per-client render threads
      stop_client_render_threads(client);

      // Now destroy the queues
      packet_queue_t *audio_queue = client->audio_queue;
      packet_queue_t *video_queue = client->video_queue;
      client->audio_queue = NULL;
      client->video_queue = NULL;

      if (audio_queue) {
        packet_queue_destroy(audio_queue);
      }
      if (video_queue) {
        packet_queue_destroy(video_queue);
      }

      // Remove from audio mixer before clearing client data
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

      // Store display name before clearing to log it when we actually finish "removing the client"
      char display_name_copy[MAX_DISPLAY_NAME_LEN];
      SAFE_STRNCPY(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

      // Destroy per-client mutexes
      mutex_destroy(&client->cached_frame_mutex);
      mutex_destroy(&client->video_buffer_mutex);
      mutex_destroy(&client->client_state_mutex);

      // Clear the entire client structure to ensure it's ready for reuse
      memset(client, 0, sizeof(client_info_t));

      // Recalculate client_count to ensure accuracy
      // Count clients with valid client_id (non-zero)
      int remaining_count = 0;
      for (int j = 0; j < MAX_CLIENTS; j++) {
        if (g_client_manager.clients[j].client_id != 0) {
          remaining_count++;
        }
      }
      g_client_manager.client_count = remaining_count;

      log_info("CLIENT REMOVED: client_id=%u (%s) removed from slot, remaining clients: %d", client_id,
               display_name_copy, remaining_count);

      rwlock_unlock(&g_client_manager_rwlock); // we're gonna return from the function

      // Broadcast updated server state to all remaining clients
      // This will trigger them to clear their terminals before the new grid layout
      broadcast_server_state_to_all_clients();

      return 0;
    }
  }

  rwlock_unlock(&g_client_manager_rwlock);
  log_warn("Cannot remove client %u: not found", client_id);
  return -1;
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

  while (!atomic_load(&g_should_exit) && client->active && client->socket != INVALID_SOCKET_VALUE) {
    // Receive packet from this client
    int result = receive_packet_with_client(client->socket, &type, &sender_id, &data, &len);

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
    log_debug("Client %u receive thread: freeing orphaned packet data %zu bytes during shutdown", client->client_id,
              len);
    buffer_pool_free(data, len);
    data = NULL;
  }

  // Mark client as inactive and stop send thread first to avoid race conditions
  // Set send_thread_running to false to signal send thread to stop
  client->active = false;
  client->send_thread_running = false;

  // Don't call remove_client() from the receive thread itself - this causes a deadlock
  // because main thread may be trying to join this thread via remove_client()
  // The main cleanup code will handle client removal after threads exit

  log_info("Receive thread for client %u terminated", client->client_id);
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
  client->send_thread_running = true;

  while (!atomic_load(&g_should_exit) && client->active && client->send_thread_running) {
    queued_packet_t *packet = NULL;

    // Try to get audio packet first (higher priority for low latency)
    if (client->audio_queue) {
      packet = packet_queue_try_dequeue(client->audio_queue);
    }

    // If no audio packet, try video
    if (!packet && client->video_queue) {
      packet = packet_queue_try_dequeue(client->video_queue);
      if (packet) {
#ifdef DEBUG_THREADS
        log_debug("SEND_THREAD_DEBUG: Client %u got video packet from queue, type=%d, data_len=%zu", client->client_id,
                  packet->header.type, packet->data_len);
#endif
      }
    }

    // If still no packet, small sleep to prevent busy waiting
    if (!packet) {
#ifdef DEBUG_THREADS
      log_debug("SEND_THREAD_DEBUG: Client %u no packet found, sleeping briefly", client->client_id);
#endif
      interruptible_usleep(1000); // 1ms sleep instead of blocking indefinitely
    }

    // If we got a packet, send it
    if (packet) {
      // Send header
      ssize_t sent = send_with_timeout(client->socket, &packet->header, sizeof(packet->header), SEND_TIMEOUT);
      if (sent != sizeof(packet->header)) {
        // During shutdown, connection errors are expected - don't spam error logs
        if (!atomic_load(&g_should_exit)) {
          log_error("Failed to send packet header to client %u: %zd/%zu bytes", client->client_id, sent,
                    sizeof(packet->header));
        } else {
          log_debug("Client %u: send failed during shutdown", client->client_id);
        }
        packet_queue_free_packet(packet);
        break; // Socket error, exit thread
      }

      // Send payload if present
      if (packet->data_len > 0 && packet->data) {
        sent = send_with_timeout(client->socket, packet->data, packet->data_len, SEND_TIMEOUT);
        if (sent != (ssize_t)packet->data_len) {
          // During shutdown, connection errors are expected - don't spam error logs
          if (!atomic_load(&g_should_exit)) {
            log_error("Failed to send packet payload to client %u: %zd/%zu bytes", client->client_id, sent,
                      packet->data_len);
          } else {
            log_debug("Client %u: payload send failed during shutdown", client->client_id);
          }
          packet_queue_free_packet(packet);
          break; // Socket error, exit thread
        }
      }


      // Free the packet
      packet_queue_free_packet(packet);
    }
  }

  // Mark thread as stopped
  client->send_thread_running = false;
  log_debug("SEND_THREAD_DEBUG: Client %u send thread exiting (g_should_exit=%d, active=%d, running=%d)",
            client->client_id, atomic_load(&g_should_exit), client->active, client->send_thread_running);
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
    if (g_client_manager.clients[i].active && g_client_manager.clients[i].is_sending_video) {
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
    if (client->active && client->video_queue) {
      if (packet_queue_enqueue(client->video_queue, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state), 0, true) <
          0) {
        log_debug("Failed to queue server state for client %u", client->client_id);
      }
    }
  }
  rwlock_unlock(&g_client_manager_rwlock);

  log_info("Broadcast server state to all clients: %d connected, %d active with video", state.connected_client_count,
           active_video_count);
}

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

void stop_client_threads(client_info_t *client) {
  if (!client) return;

  // Signal threads to stop
  client->active = false;
  client->send_thread_running = false;

  // Wait for threads to finish
  if (ascii_thread_is_initialized(&client->send_thread)) {
    ascii_thread_join(&client->send_thread, NULL);
  }
  if (ascii_thread_is_initialized(&client->receive_thread)) {
    ascii_thread_join(&client->receive_thread, NULL);
  }
}

void cleanup_client_media_buffers(client_info_t *client) {
  if (!client) return;

  if (client->incoming_video_buffer) {
    framebuffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
  }

  if (client->incoming_audio_buffer) {
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_audio_buffer = NULL;
  }
}

void cleanup_client_packet_queues(client_info_t *client) {
  if (!client) return;

  if (client->audio_queue) {
    packet_queue_destroy(client->audio_queue);
    client->audio_queue = NULL;
  }

  if (client->video_queue) {
    packet_queue_destroy(client->video_queue);
    client->video_queue = NULL;
  }
}
