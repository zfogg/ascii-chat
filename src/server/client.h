#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "network.h"
#include "packet_queue.h"
#include "ringbuffer.h"
#include "platform/terminal.h"
#include "palette.h"
#include "hashtable.h"

// Use definitions from network.h (MAX_CLIENTS, MAX_DISPLAY_NAME_LEN)

/* ============================================================================
 * Client Information Structure
 * ============================================================================
 */
typedef struct {
  socket_t socket;
  asciithread_t receive_thread; // Thread for receiving client data
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  char client_ip[INET_ADDRSTRLEN];
  int port;

  // Media capabilities
  bool can_send_video;
  bool can_send_audio;
  bool wants_stretch; // Client wants stretched output (ignore aspect ratio)
  bool is_sending_video;
  bool is_sending_audio;

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
  unsigned short width, height;

  // Statistics
  bool active;
  time_t connected_at;
  uint64_t frames_sent;
  uint64_t frames_received; // Track incoming frames from this client

  // Buffers for incoming media (individual per client)
  framebuffer_t *incoming_video_buffer;       // Buffer for this client's video
  audio_ring_buffer_t *incoming_audio_buffer; // Buffer for this client's audio

  // Cached frame for when buffer is empty (prevents flicker)
  multi_source_frame_t last_valid_frame; // Cache of most recent valid frame
  bool has_cached_frame;                 // Whether we have a valid cached frame

  // Packet queues for outgoing data (per-client queues for isolation)
  packet_queue_t *audio_queue; // Queue for audio packets to send to this client
  packet_queue_t *video_queue; // Queue for video packets to send to this client

  // Dedicated send thread for this client
  asciithread_t send_thread;
  bool send_thread_running;

  // Per-client rendering threads
  asciithread_t video_render_thread;
  asciithread_t audio_render_thread;
  bool video_render_thread_running;
  bool audio_render_thread_running;

  // Per-client processing state
  struct timespec last_video_render_time;
  struct timespec last_audio_render_time;

  // Per-client synchronization
  mutex_t client_state_mutex;
  // CONCURRENCY FIX: Per-client cached frame protection for concurrent video generation
  mutex_t cached_frame_mutex;
  // THREAD-SAFE FRAMEBUFFER: Per-client video buffer mutex for concurrent access
  mutex_t video_buffer_mutex;
} client_info_t;

/* ============================================================================
 * Client Manager Structure
 * ============================================================================
 */
typedef struct {
  client_info_t clients[MAX_CLIENTS]; // Backing storage
  hashtable_t *client_hashtable;      // Hash table for O(1) lookup by client_id
  int client_count;
  mutex_t mutex;
  uint32_t next_client_id; // For assigning unique IDs
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

// Client initialization
void initialize_client_info(client_info_t *client);
