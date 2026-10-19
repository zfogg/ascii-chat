#pragma once
/** Server media state. The network runtime owns establishment, security,
 * packet execution, and transport teardown. This state owns media buffers,
 * capabilities, and the render workers joined by the connection closing hook. */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <ascii-chat/atomic.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/acip/transport.h> // For acip_transport_t
#include <ascii-chat/network/connection.h>
#include <ascii-chat/crypto/handshake/common.h> // For crypto_handshake_context_t (complete type needed for field)
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/thread.h> // For thread_id_t
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/audio/audio.h>

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

typedef struct client_info {
  participant_type_t participant_type; // Network (socket) or memory (direct injection)
  network_connection_t *network_connection;
  atomic_t event_ready;
  atomic_t event_cleanup;
  acip_transport_t *transport;       // ACIP transport for protocol-agnostic packet sending
  void *server_ctx;                  // Pointer to server_context_t (avoid circular includes)
  char client_id[MAX_CLIENT_ID_LEN]; // String-based client ID: "noun.N (transport:port)"
  uint32_t session_client_id;        // Session host client ID (0 if not registered)
  char display_name[MAX_DISPLAY_NAME_LEN];
  char client_ip[INET_ADDRSTRLEN];
  int port;

  atomic_t supports_recoverable_errors; // Negotiated optional error notifications

  // Media capabilities
  bool can_send_video;
  bool can_send_audio;
  bool wants_stretch;        // Client wants stretched output (ignore aspect ratio)
  atomic_t is_sending_video; // Thread-safe video stream state
  atomic_t is_sending_audio; // Thread-safe audio stream state

  // Codec capabilities (from CLIENT_CAPABILITIES packet)
  uint32_t codec_capabilities_video; // VIDEO_CODEC_CAP_* bitmask
  uint32_t codec_capabilities_audio; // AUDIO_CODEC_CAP_* bitmask

  // Opus codec for audio compression/decompression
  void *opus_decoder; // opus_codec_t* - Opus decoder for this client's audio

  // Video decoder (persistent across frames for proper video decoding)
  void *video_decoder_ctx;  // AVCodecContext* - H.264/HEVC decoder for this client
  void *video_decode_frame; // AVFrame* - Persistent frame for video decoding
  void *video_rgb_frame;    // AVFrame* - RGB frame for color space conversion
  void *video_sws_ctx;      // SwsContext* - Color space converter for YUV->RGB

  // Terminal capabilities (for rendering appropriate ASCII frames)
  terminal_capabilities_t terminal_caps;
  bool has_terminal_caps; // Whether we've received terminal capabilities from this client

  // Per-client palette cache
  char client_palette_chars[256];     // Client's palette characters
  size_t client_palette_len;          // Length of client's palette
  char client_luminance_palette[256]; // Client's luminance-to-character mapping
  palette_type_t client_palette_type; // Client's palette type
  bool client_palette_initialized;    // Whether client's palette is set up

  // Per-client state for Matrix Rain post-processing of generated ASCII.
  digital_rain_t *digital_rain;
  double digital_rain_last_update_time;

  // Stream dimensions
  uint16_t width, height;

  // Statistics
  atomic_t active;
  atomic_t shutting_down;                 // Set when client is being removed
  atomic_t protocol_disconnect_requested; // Set when protocol violation requires disconnect
  time_t connected_at;
  uint64_t frames_sent;
  uint64_t frames_received;        // Track incoming frames from this client
  uint32_t frames_received_logged; // Track for periodic logging (thread-safe via client_state_mutex)
  uint64_t video_receive_rate_window_ns;
  uint32_t video_receive_rate_window_frames;
  uint32_t last_received_frame_hash; // Hash of last frame received from this client

  // Buffers for incoming media (individual per client)
  video_frame_buffer_t *incoming_video_buffer; // Modern double-buffered video frame
  audio_ring_buffer_t *incoming_audio_buffer;  // Buffer for this client's audio

  // Tracks the last source layout submitted to the shared send scheduler.
  atomic_t last_sent_grid_sources;

  // Frame statistics for debugging
  atomic_t frames_sent_count; // Total ASCII frames sent to this client

  // Per-client rendering threads
  asciichat_thread_t video_render_thread;
  asciichat_thread_t audio_render_thread;
  atomic_t video_render_thread_running;
  atomic_t audio_render_thread_running;

  // Per-client synchronization
  mutex_t client_state_mutex;
  mutex_t send_mutex; // Serializes application control and media transitions

  // Borrowed from the owning network connection; valid until producer threads join.
  crypto_handshake_context_t *crypto_handshake_ctx;
  bool crypto_initialized;
  bool transport_encrypted; ///< TLS or DTLS already encrypts this client's transport.
  bool security_ready;      // Negotiation and any required authentication completed
  bool encrypt_data;        // Whether payload encryption is enabled (false = auth-only mode)

  // uthash handle for hash table operations
  UT_hash_handle hh;
} client_info_t;
