/**
 * @file lib/network/client.h
 * @brief Client state structure and network logging macros
 *
 * This header defines the client_info_t structure used by both library code
 * and server code. It also provides convenience macros for network logging.
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "network/network.h"
#include "network/packet.h"
#include "network/packet_queue.h"
#include "network/logging.h"
#include "crypto/handshake.h" // For crypto_handshake_context_t (complete type needed for field)
#include "util/ringbuffer.h"
#include "video/video_frame.h"
#include "platform/terminal.h"
#include "video/palette.h"
#include "util/uthash.h"

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
 */
typedef struct client_info {
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
