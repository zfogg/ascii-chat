/**
 * @file lib/network/client.h
 * @brief Client state structures
 *
 * Defines two client structures:
 * - client_info_t: Server-side per-client state
 * - app_client_t: Client-side application state (replaces mixed-concern tcp_client_t)
 */
#pragma once

// C11 stdatomic.h conflicts with MSVC's C++ <atomic> header on Windows.
#if defined(__cplusplus) && defined(_WIN32)
#include <atomic>
using std::atomic_bool;
using std::atomic_int;
using std::atomic_uint;
#else
#include <stdatomic.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "../network/packet.h"
#include "../network/packet_queue.h"
#include "../network/acip/transport.h"  // For acip_transport_t
#include "../crypto/handshake/common.h" // For crypto_handshake_context_t (complete type needed for field)
#include "../ringbuffer.h"
#include "../video/video_frame.h"
#include "../platform/terminal.h"
#include "../platform/thread.h" // For thread_id_t
#include "../video/palette.h"
#include "../audio/audio.h"

/**
 * @brief Participant type for distinguishing network vs memory participants
 *
 * Network participants communicate over TCP/IP or WebRTC, while memory
 * participants inject media directly into the host's mixer (used when
 * the host participates in the session with their own webcam/audio).
 */
typedef enum {
  PARTICIPANT_TYPE_NETWORK, // Remote participant via socket/transport
  PARTICIPANT_TYPE_MEMORY   // Local host participant (direct memory access)
} participant_type_t;

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
  participant_type_t participant_type; // Network (socket) or memory (direct injection)
  socket_t socket;
  bool is_tcp_client;                // True for TCP clients, false for WebRTC (for cleanup logic)
  acip_transport_t *transport;       // ACIP transport for protocol-agnostic packet sending
  asciichat_thread_t receive_thread; // Thread for receiving client data
  thread_id_t receive_thread_id;     // Thread ID of receive thread (for self-join detection)
  void *server_ctx;                  // Pointer to server_context_t (avoid circular includes)
  atomic_uint client_id;             // Thread-safe client ID
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
  uint64_t frames_received;          // Track incoming frames from this client
  uint32_t frames_received_logged;   // Track for periodic logging (thread-safe via client_state_mutex)
  uint32_t last_received_frame_hash; // Hash of last frame received from this client

  // Buffers for incoming media (individual per client)
  video_frame_buffer_t *incoming_video_buffer; // Modern double-buffered video frame
  audio_ring_buffer_t *incoming_audio_buffer;  // Buffer for this client's audio

  // Professional double-buffer system for outgoing frames
  video_frame_buffer_t *outgoing_video_buffer; // Double buffer for ASCII frames to send

  // Packet queue for audio only (video uses double buffer now)
  packet_queue_t *audio_queue; // Queue for audio packets to send to this client

  // Async dispatch: queue for received packets (async processing)
  packet_queue_t *received_packet_queue; // Queue of complete received packets waiting for dispatch
  asciichat_thread_t dispatch_thread;    // Async dispatch thread processes queued packets
  atomic_bool dispatch_thread_running;   // Flag to signal dispatch thread to exit

  // Dedicated send thread for this client
  asciichat_thread_t send_thread;
  atomic_bool send_thread_running;

  // Per-client grid tracking for CLEAR_CONSOLE logic
  atomic_int last_rendered_grid_sources; // Render thread: source count in buffered frame
  atomic_int last_sent_grid_sources;     // Send thread: source count in last sent frame

  // Frame statistics for debugging
  atomic_ulong frames_sent_count; // Total ASCII frames sent to this client

  // Pre-allocated buffers to avoid malloc/free in send thread (prevents buffer pool contention)
  void *send_buffer;
  size_t send_buffer_size;
  void *crypto_plaintext_buffer; // For encryption plaintext (frame + header)
  size_t crypto_plaintext_size;
  void *crypto_ciphertext_buffer; // For encryption ciphertext (encrypted result)
  size_t crypto_ciphertext_size;

  // Per-client rendering threads
  asciichat_thread_t video_render_thread;
  asciichat_thread_t audio_render_thread;
  atomic_bool video_render_thread_running;
  atomic_bool audio_render_thread_running;

  // Per-client synchronization
  mutex_t client_state_mutex;
  mutex_t send_mutex; // Protects socket writes (prevents concurrent send race)

  // Per-client crypto context for secure communication
  crypto_handshake_context_t crypto_handshake_ctx;
  bool crypto_initialized;

  // Pending packet storage for --no-encrypt mode
  // When client uses --no-encrypt, the first packet (e.g., CLIENT_JOIN) arrives
  // during crypto handshake attempt. We store it here so the caller can process it.
  packet_type_t pending_packet_type;
  void *pending_packet_payload;
  size_t pending_packet_length;

  // uthash handle for hash table operations
  UT_hash_handle hh;
} client_info_t;

/* ============================================================================
 * Client-Side Application State (replaces tcp_client_t mixed concerns)
 * ============================================================================ */

/** Forward declarations */
struct tcp_client;
struct websocket_client;

/**
 * @brief Audio packet for async transmission
 */
typedef struct {
  uint8_t data[4096];       ///< Opus-encoded audio data
  size_t size;              ///< Size of encoded data
  uint16_t frame_sizes[48]; ///< Individual frame sizes for Opus batching
  int frame_count;          ///< Number of frames in packet
} app_client_audio_packet_t;

#define APP_CLIENT_AUDIO_QUEUE_SIZE 256

/**
 * @brief Client-side application state
 *
 * Transport-agnostic container for application-layer state previously mixed
 * in tcp_client_t. Holds audio queues, thread handles, crypto context, and
 * display state. Maintains references to active network client and transport.
 */
typedef struct app_client {
  /* ========================================================================
   * Active Transport & Network Client
   * ======================================================================== */

  acip_transport_t *active_transport;
  acip_transport_type_t transport_type;
  struct tcp_client *tcp_client;
  struct websocket_client *ws_client;

  /* ========================================================================
   * Audio State
   * ======================================================================== */

  audio_context_t audio_ctx;
  app_client_audio_packet_t audio_send_queue[APP_CLIENT_AUDIO_QUEUE_SIZE];
  int audio_send_queue_head;
  int audio_send_queue_tail;
  mutex_t audio_send_queue_mutex;
  cond_t audio_send_queue_cond;
  bool audio_send_queue_initialized;
  atomic_bool audio_sender_should_exit;
  asciichat_thread_t audio_capture_thread;
  asciichat_thread_t audio_sender_thread;
  bool audio_capture_thread_created;
  bool audio_sender_thread_created;
  atomic_bool audio_capture_thread_exited;

  /* ========================================================================
   * Protocol State
   * ======================================================================== */

  asciichat_thread_t data_reception_thread;
  bool data_thread_created;
  atomic_bool data_thread_exited;
  uint32_t last_active_count;
  bool server_state_initialized;
  bool should_clear_before_next_frame;
  uint32_t my_client_id;
  bool encryption_enabled;

  /* ========================================================================
   * Capture State
   * ======================================================================== */

  asciichat_thread_t capture_thread;
  bool capture_thread_created;
  atomic_bool capture_thread_exited;

  /* ========================================================================
   * Keepalive State
   * ======================================================================== */

  asciichat_thread_t ping_thread;
  bool ping_thread_created;
  atomic_bool ping_thread_exited;

  /* ========================================================================
   * Display State
   * ======================================================================== */

  bool has_tty;
  atomic_bool is_first_frame_of_connection;
  tty_info_t tty_info;

  /* ========================================================================
   * Crypto State
   * ======================================================================== */

  crypto_handshake_context_t crypto_ctx;
  bool crypto_initialized;

} app_client_t;

/**
 * @brief Create and initialize client application context
 * @return Pointer to initialized context, or NULL on failure
 */
app_client_t *app_client_create(void);

/**
 * @brief Destroy client application context and free all resources
 * @param client_ptr Pointer to client pointer (set to NULL after free)
 */
void app_client_destroy(app_client_t **client_ptr);
