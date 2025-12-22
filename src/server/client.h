/**
 * @file server/client.h
 * @ingroup server_client
 * @brief Per-client state management and lifecycle orchestration
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "network/network.h"
#include "network/packet.h"
#include "logging.h"
#include "packet_queue.h"
#include "ringbuffer.h"
#include "video_frame.h"
#include "platform/terminal.h"
#include "palette.h"
#include "util/uthash.h"
#include "crypto/handshake.h"

// Use definitions from network.h (MAX_CLIENTS, MAX_DISPLAY_NAME_LEN)

/**
 * @brief Per-client state structure for server-side client management
 *
 * Represents complete state for a single connected client in the ascii-chat server.
 * This structure stores all client-specific information including network connection,
 * media capabilities, terminal settings, threading state, and synchronization primitives.
 *
 * CORE FIELDS:
 * ============
 * - Network: Socket, IP address, port, thread handles
 * - Media: Video/audio capabilities, stream state, buffers
 * - Terminal: Capabilities, palette cache, dimensions
 * - Threading: Send/receive/render thread handles and flags
 * - Synchronization: Mutexes for thread-safe state access
 * - Crypto: Cryptographic handshake context for secure communication
 *
 * THREADING MODEL:
 * ================
 * Each client has dedicated threads:
 * - receive_thread: Handles incoming packets (protocol processing)
 * - send_thread: Manages outgoing packet delivery (packet queues)
 * - video_render_thread: Generates ASCII frames at 60fps
 * - audio_render_thread: Mixes audio streams at 172fps
 *
 * BUFFER MANAGEMENT:
 * ==================
 * - incoming_video_buffer: Double-buffered video frames from client
 * - incoming_audio_buffer: Ring buffer for client's audio samples
 * - outgoing_video_buffer: Double-buffered ASCII frames to send
 * - audio_queue: Packet queue for audio packets to send
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - Pre-allocated buffers to avoid malloc/free in hot paths
 * - send_buffer: For packet assembly
 * - crypto_plaintext_buffer: For encryption plaintext
 * - crypto_ciphertext_buffer: For encryption ciphertext
 *
 * @note All atomic fields are thread-safe for concurrent access.
 * @note Buffer pointers are set once during client creation and never change.
 * @note Thread handles are valid only when threads are running.
 *
 * @ingroup server_client
 */
typedef struct {
  socket_t socket;
  asciithread_t receive_thread; // Thread for receiving client data
  atomic_uint client_id;        // Thread-safe client ID
  char display_name[MAX_DISPLAY_NAME_LEN];
  char client_ip[INET_ADDRSTRLEN];
  int port;

  // Media capabilities
  bool can_send_video;
  bool can_send_audio;
  bool wants_stretch;           // Client wants stretched output (ignore aspect ratio)
  atomic_bool is_sending_video; // Thread-safe video stream state
  atomic_bool is_sending_audio; // Thread-safe audio stream state

  // Opus codec for audio compression/decompression
  void *opus_decoder; // opus_codec_t* - Opus decoder for this client's audio

  // Terminal capabilities (for rendering appropriate ASCII frames)
  terminal_capabilities_t terminal_caps;
  bool has_terminal_caps; // Whether we've received terminal capabilities from this client

  // Per-client palette cache
  char client_palette_chars[256];     // Client's palette characters
  size_t client_palette_len;          // Length of client's palette
  char client_luminance_palette[256]; // Client's luminance-to-character mapping
  palette_type_t client_palette_type; // Client's palette type
  bool client_palette_initialized;    // Whether client's palette is set up

  // Stream dimensions
  atomic_ushort width, height;

  // Statistics
  atomic_bool active;
  atomic_bool shutting_down;                 // Set when client is being removed
  atomic_bool protocol_disconnect_requested; // Set when protocol violation requires disconnect
  time_t connected_at;
  uint64_t frames_sent;
  uint64_t frames_received;        // Track incoming frames from this client
  uint32_t frames_received_logged; // Track for periodic logging (thread-safe via client_state_mutex)

  // Buffers for incoming media (individual per client)
  video_frame_buffer_t *incoming_video_buffer; // Modern double-buffered video frame
  audio_ring_buffer_t *incoming_audio_buffer;  // Buffer for this client's audio

  // Professional double-buffer system for outgoing frames
  video_frame_buffer_t *outgoing_video_buffer; // Double buffer for ASCII frames to send

  // Packet queue for audio only (video uses double buffer now)
  packet_queue_t *audio_queue; // Queue for audio packets to send to this client

  // Dedicated send thread for this client
  asciithread_t send_thread;
  atomic_bool send_thread_running;

  // Per-client grid tracking for CLEAR_CONSOLE logic
  atomic_int last_rendered_grid_sources; // Render thread: source count in buffered frame
  atomic_int last_sent_grid_sources;     // Send thread: source count in last sent frame

  // Pre-allocated buffers to avoid malloc/free in send thread (prevents buffer pool contention)
  void *send_buffer;
  size_t send_buffer_size;
  void *crypto_plaintext_buffer; // For encryption plaintext (frame + header)
  size_t crypto_plaintext_size;
  void *crypto_ciphertext_buffer; // For encryption ciphertext (encrypted result)
  size_t crypto_ciphertext_size;

  // Per-client rendering threads
  asciithread_t video_render_thread;
  asciithread_t audio_render_thread;
  atomic_bool video_render_thread_running;
  atomic_bool audio_render_thread_running;

  // Per-client processing state
  struct timespec last_video_render_time;
  struct timespec last_audio_render_time;

  // Per-client synchronization
  mutex_t client_state_mutex;
  mutex_t send_mutex; // Protects socket writes (prevents concurrent send race)

  // Per-client crypto context for secure communication
  crypto_handshake_context_t crypto_handshake_ctx;
  bool crypto_initialized;

  // uthash handle for hash table operations
  UT_hash_handle hh;
} client_info_t;

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
  /** @brief Monotonic counter for unique client IDs (atomic for thread-safety) */
  _Atomic uint32_t next_client_id;
} client_manager_t;

// Global client manager
extern client_manager_t g_client_manager;
extern rwlock_t g_client_manager_rwlock;

// Client management functions
int add_client(socket_t socket, const char *client_ip, int port);
int remove_client(uint32_t client_id);
client_info_t *find_client_by_id(uint32_t client_id);
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
