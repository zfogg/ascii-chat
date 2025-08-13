#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "image.h"
#include "ascii.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "packet_queue.h"
#include "buffer_pool.h"
#include "ringbuffer.h"
#include "audio.h"
#include "aspect_ratio.h"
#include "mixer.h"
#include "hashtable.h"
#include "frame_debug.h"

/* ============================================================================
 * Global State
 * ============================================================================
 */

static volatile bool g_should_exit = false;

// No emergency socket tracking needed - main shutdown sequence handles socket closure
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

// Shutdown signaling for fast thread cleanup
static pthread_mutex_t g_shutdown_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_shutdown_cond = PTHREAD_COND_INITIALIZER;

/* Interruptible sleep that respects shutdown signal */
static void interruptible_usleep(useconds_t usec) {
  if (g_should_exit)
    return;

  pthread_mutex_lock(&g_shutdown_mutex);
  if (!g_should_exit) {
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += usec / 1000000;
    timeout.tv_nsec += (usec % 1000000) * 1000;
    if (timeout.tv_nsec >= 1000000000) {
      timeout.tv_sec++;
      timeout.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&g_shutdown_cond, &g_shutdown_mutex, &timeout);
  }
  pthread_mutex_unlock(&g_shutdown_mutex);
}

/* Performance statistics */
typedef struct {
  uint64_t frames_captured;
  uint64_t frames_sent;
  uint64_t frames_dropped;
  uint64_t bytes_sent;
  double avg_capture_fps;
  double avg_send_fps;
} server_stats_t;

/* ============================================================================
 * Multi-Client Support Structures
 * ============================================================================
 */

typedef struct {
  int socket;
  pthread_t receive_thread; // Thread for receiving client data
  // Send thread removed - using broadcast thread for all sending
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  char client_ip[INET_ADDRSTRLEN];
  int port;

  // Media capabilities
  bool can_send_video;
  bool can_send_audio;
  bool wants_color;   // Client wants colored ASCII output
  bool wants_stretch; // Client wants stretched output (ignore aspect ratio)
  bool is_sending_video;
  bool is_sending_audio;

  // Stream dimensions
  unsigned short width, height;

  // Statistics
  bool active;
  time_t connected_at;
  uint64_t frames_sent;
  uint64_t frames_received; // NEW: Track incoming frames from this client

  // Buffers for incoming media (individual per client)
  framebuffer_t *incoming_video_buffer;       // NEW: Buffer for this client's video
  audio_ring_buffer_t *incoming_audio_buffer; // NEW: Buffer for this client's audio

  // Cached frame for when buffer is empty (prevents flicker)
  multi_source_frame_t last_valid_frame; // Cache of most recent valid frame
  bool has_cached_frame;                 // Whether we have a valid cached frame

  // Packet queues for outgoing data (per-client queues for isolation)
  packet_queue_t *audio_queue; // Queue for audio packets to send to this client
  packet_queue_t *video_queue; // Queue for video packets to send to this client

  // Dedicated send thread for this client
  pthread_t send_thread;
  bool send_thread_running;
} client_info_t;

typedef struct {
  client_info_t clients[MAX_CLIENTS]; // Backing storage (still needed)
  hashtable_t *client_hashtable;      // Hash table for O(1) lookup by client_id
  int client_count;
  pthread_mutex_t mutex;
  uint32_t next_client_id; // For assigning unique IDs
} client_manager_t;

// Global multi-client state
static client_manager_t g_client_manager = {0};
static pthread_mutex_t g_client_manager_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Audio Mixing System
 * ============================================================================
 */

// Global audio mixer using new advanced mixer system
static mixer_t *g_audio_mixer = NULL;
static pthread_t g_audio_mixer_thread;
static bool g_audio_mixer_thread_created = false;

// Statistics logging thread
static pthread_t g_stats_logger_thread;
static bool g_stats_logger_thread_created = false;

// Frame debugging trackers
static frame_debug_tracker_t g_server_frame_debug;

// Blank frame statistics
static uint64_t g_blank_frames_sent = 0;

// Video mixing system (inline mixing, no global buffers needed)

// Last valid frame cache for consistent delivery (prevents flickering)
static char *g_last_valid_frame = NULL;
static size_t g_last_valid_frame_size = 0;
static unsigned short g_last_frame_width = 0;
static unsigned short g_last_frame_height = 0;
static bool g_last_frame_was_color = false;
static pthread_mutex_t g_frame_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static server_stats_t g_stats = {0};

static int listenfd = 0;

/* ============================================================================
 * Multi-Client Function Declarations
 * ============================================================================
 */

// Thread functions
void *client_receive_thread_func(void *arg);
void *client_send_thread_func(void *arg);

// Audio mixing thread function
void *audio_mixer_thread_func(void *arg);

// Statistics logging thread function
void *stats_logger_thread_func(void *arg);

// Video mixing functions
char *create_mixed_ascii_frame(unsigned short width, unsigned short height, bool wants_color, bool wants_stretch,
                               size_t *out_size);

// Client management functions
int add_client(int socket, const char *client_ip, int port);
int remove_client(uint32_t client_id);

// Fast client lookup functions using hash table
client_info_t *find_client_by_id(uint32_t client_id);
client_info_t *find_client_by_id_fast(uint32_t client_id); // O(1) hash table lookup

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Server terminal resize - we ignore this since we use client's terminal size
  // Only log that the event occurred
  log_debug("Server terminal resized (ignored - using client terminal size)");
}

static void sigint_handler(int sigint) {
  (void)(sigint);
  g_should_exit = true;

  // Signal-safe logging - avoid log_info() which may not be signal-safe
  const char msg[] = "SIGINT received - shutting down server...\n";
  write(STDOUT_FILENO, msg, sizeof(msg) - 1);

  // Wake up all sleeping threads immediately
  pthread_cond_broadcast(&g_shutdown_cond);

  // Close all client sockets to interrupt blocking receive threads (signal-safe)
  // Note: This is done without mutex locking since signal handlers should be minimal
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].socket > 0) {
      close(g_client_manager.clients[i].socket);
    }
  }

  // Close listening socket to interrupt accept() - this is signal-safe
  if (listenfd > 0) {
    close(listenfd);
  }

  // Don't do complex operations in signal handler - they can cause deadlocks
  // Let the main thread handle the cleanup when it sees g_should_exit = true
}

static void sigterm_handler(int sigterm) {
  (void)(sigterm);
  g_should_exit = true;

  // Signal-safe logging - avoid log_warn() which may not be signal-safe
  const char msg[] = "SIGTERM received - shutting down server...\n";
  write(STDERR_FILENO, msg, sizeof(msg) - 1);

  // Wake up all sleeping threads immediately - signal-safe operation
  pthread_cond_broadcast(&g_shutdown_cond);

  // Close listening socket to interrupt accept() - signal-safe
  if (listenfd > 0) {
    close(listenfd);
  }

  // NOTE: Client socket closure handled by main shutdown sequence (not signal handler)
  // Signal handler should be minimal - just set flag and wake threads
  // Main thread will properly close client sockets with mutex protection
}

/* ============================================================================
 * Fast Client Lookup Functions
 * ============================================================================ */

// O(1) hash table lookup for client by ID
client_info_t *find_client_by_id_fast(uint32_t client_id) {
  if (client_id == 0 || !g_client_manager.client_hashtable) {
    return NULL;
  }

  return (client_info_t *)hashtable_lookup(g_client_manager.client_hashtable, client_id);
}

// Traditional O(n) linear search (kept for debugging/verification)
client_info_t *find_client_by_id(uint32_t client_id) {
  if (client_id == 0) {
    return NULL;
  }

  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].client_id == client_id && g_client_manager.clients[i].active) {
      return &g_client_manager.clients[i];
    }
  }
  return NULL;
}

/* ============================================================================
 * No server capture thread - clients send their video
 * ============================================================================
 */

/* ============================================================================
 * Old server audio capture removed - clients capture and send their own audio
 * ============================================================================
 */

/* ============================================================================
 * Client Size Handling
 * ============================================================================
 */

int receive_client_size(int sockfd, unsigned short *width, unsigned short *height) {
  // Try to peek for a packet header to see if there's a size packet
  packet_header_t header;
  ssize_t peeked = recv(sockfd, &header, sizeof(header), MSG_PEEK | MSG_DONTWAIT);
  if (peeked <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0; // No data available (non-blocking)
    }
    return -1; // Error or connection closed
  }

  if (peeked < (ssize_t)sizeof(header)) {
    return 0; // Not enough data for a complete header
  }

  // Check if this is a size packet
  uint32_t magic = ntohl(header.magic);
  uint16_t type = ntohs(header.type);

  if (magic != PACKET_MAGIC || type != PACKET_TYPE_SIZE) {
    return 0; // Not a size packet
  }

  // Receive the complete packet
  packet_type_t pkt_type;
  void *data;
  size_t len;

  int result = receive_packet(sockfd, &pkt_type, &data, &len);
  if (result <= 0) {
    return result; // Error or connection closed
  }

  if (pkt_type != PACKET_TYPE_SIZE || len != 4) {
    buffer_pool_free(data, len);
    return 0; // Invalid size packet
  }

  const uint16_t *size_data = (const uint16_t *)data;
  *width = ntohs(size_data[0]);
  *height = ntohs(size_data[1]);

  buffer_pool_free(data, len);
  return 1; // Successfully parsed size packet
}

/* ============================================================================
 * Audio Mixing Implementation
 * ============================================================================
 */

// Audio mixing is now handled by the mixer_t from mixer.h
// Old audio_mixer_t functions have been removed

// Audio mixing thread function using the new advanced mixer
void *audio_mixer_thread_func(void *arg) {
  (void)arg;
  log_info("Audio mixer thread started (per-client mixes excluding own audio)");

  float mix_buffer[AUDIO_FRAMES_PER_BUFFER];

  while (!g_should_exit) {
    if (!g_audio_mixer) {
      interruptible_usleep(10000); // 10ms - wait for mixer initialization
      continue;
    }

    // Create per-client mixes excluding each client's own audio
    pthread_mutex_lock(&g_client_manager_mutex);

    // Count active clients with audio
    int active_audio_clients = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket > 0 &&
          g_client_manager.clients[i].audio_queue) {
        active_audio_clients++;
      }
    }

    // Process each client separately
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      if (client->active && client->socket > 0 && client->audio_queue) {
        // Create a mix excluding this client's own audio
        int samples_mixed =
            mixer_process_excluding_source(g_audio_mixer, mix_buffer, AUDIO_FRAMES_PER_BUFFER, client->client_id);

        if (samples_mixed > 0 || active_audio_clients == 1) {
          // If only one client, send silence
          if (active_audio_clients == 1) {
            memset(mix_buffer, 0, AUDIO_FRAMES_PER_BUFFER * sizeof(float));
          }

          // Debug: Check for DEADBEEF
          uint32_t *check_magic = (uint32_t *)mix_buffer;
          if (*check_magic == 0xDEADBEEF || *check_magic == 0xEFBEADDE) {
            log_error(
                "DEADBEEF found in audio buffer for client %u! First 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x",
                client->client_id, ((unsigned char *)mix_buffer)[0], ((unsigned char *)mix_buffer)[1],
                ((unsigned char *)mix_buffer)[2], ((unsigned char *)mix_buffer)[3], ((unsigned char *)mix_buffer)[4],
                ((unsigned char *)mix_buffer)[5], ((unsigned char *)mix_buffer)[6], ((unsigned char *)mix_buffer)[7],
                ((unsigned char *)mix_buffer)[8], ((unsigned char *)mix_buffer)[9], ((unsigned char *)mix_buffer)[10],
                ((unsigned char *)mix_buffer)[11], ((unsigned char *)mix_buffer)[12], ((unsigned char *)mix_buffer)[13],
                ((unsigned char *)mix_buffer)[14], ((unsigned char *)mix_buffer)[15]);
          }

#ifdef AUDIO_DEBUG
          uint32_t crc_before = asciichat_crc32(mix_buffer, AUDIO_FRAMES_PER_BUFFER * sizeof(float));
          log_debug("Sending audio to client %u: samples_mixed=%d, CRC=0x%x", client->client_id, samples_mixed,
                    crc_before);
#endif

          // Queue the audio packet for this client
          size_t data_size = AUDIO_FRAMES_PER_BUFFER * sizeof(float);
          int result = packet_queue_enqueue(client->audio_queue, PACKET_TYPE_AUDIO, mix_buffer, data_size, 0,
                                            true); // client_id=0 for server-originated, copy=true
          if (result < 0) {
            log_debug("Failed to queue audio for client %u (queue full or shutdown)", client->client_id);
          }
        }
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    // Audio mixing rate - process every ~5.8ms to match buffer size
    // 256 samples at 44100 Hz = 5.8ms
    interruptible_usleep(5800); // 5.8ms
  }

  log_info("Audio mixer thread stopped");
  return NULL;
}

/* ============================================================================
 * Statistics Logging Thread
 * ============================================================================
 */

void *stats_logger_thread_func(void *arg) {
  (void)arg;
  log_info("Statistics logger thread started");

  while (!g_should_exit) {
    // Log buffer pool statistics every 30 seconds with fast exit checking (10ms intervals)
    for (int i = 0; i < 3000 && !g_should_exit; i++) {
      interruptible_usleep(10000); // 10ms sleep - can be interrupted by shutdown
    }

    if (g_should_exit)
      break;

    log_info("=== Periodic Statistics Report ===");

    // Log global buffer pool stats
    buffer_pool_log_global_stats();

    // Log client statistics
    pthread_mutex_lock(&g_client_manager_mutex);
    int active_clients = 0;
    int clients_with_audio = 0;
    int clients_with_video = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (g_client_manager.clients[i].active) {
        active_clients++;
        if (g_client_manager.clients[i].audio_queue) {
          clients_with_audio++;
        }
        if (g_client_manager.clients[i].video_queue) {
          clients_with_video++;
        }
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    log_info("Active clients: %d, Audio: %d, Video: %d", active_clients, clients_with_audio, clients_with_video);
    log_info("Blank frames sent: %llu", (unsigned long long)g_blank_frames_sent);

    // Log hash table statistics
    if (g_client_manager.client_hashtable) {
      hashtable_print_stats(g_client_manager.client_hashtable, "Client Lookup");
    }

    // Log per-client buffer pool stats if they have local pools
    pthread_mutex_lock(&g_client_manager_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      if (client->active && client->client_id != 0) {
        // Log packet queue stats if available
        if (client->audio_queue) {
          uint64_t enqueued, dequeued, dropped;
          packet_queue_get_stats(client->audio_queue, &enqueued, &dequeued, &dropped);
          if (enqueued > 0 || dequeued > 0 || dropped > 0) {
            log_info("Client %u audio queue: %llu enqueued, %llu dequeued, %llu dropped", client->client_id,
                     (unsigned long long)enqueued, (unsigned long long)dequeued, (unsigned long long)dropped);
          }
        }
        if (client->video_queue) {
          uint64_t enqueued, dequeued, dropped;
          packet_queue_get_stats(client->video_queue, &enqueued, &dequeued, &dropped);
          if (enqueued > 0 || dequeued > 0 || dropped > 0) {
            log_info("Client %u video queue: %llu enqueued, %llu dequeued, %llu dropped", client->client_id,
                     (unsigned long long)enqueued, (unsigned long long)dequeued, (unsigned long long)dropped);
          }
        }
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);
  }

  log_info("Statistics logger thread stopped");
  return NULL;
}

/* ============================================================================
 * Video Broadcasting Thread
 * ============================================================================
 */

static pthread_t g_video_broadcast_thread;
static bool g_video_broadcast_running = false;

static void *video_broadcast_thread_func(void *arg) {
  (void)arg;

  log_info("Video broadcast thread started");
  g_video_broadcast_running = true;

  // Dynamic frame rate control based on buffer occupancy
  // Base rates: 30 FPS normal, up to 60 FPS when draining buffers
  const int base_frame_interval_ms = 1000 / 30; // 30 FPS base rate
  const int fast_frame_interval_ms = 1000 / 60; // 60 FPS when draining

  struct timespec last_broadcast_time;
  clock_gettime(CLOCK_MONOTONIC, &last_broadcast_time);

  // Track the number of active clients for console clear detection
  int last_connected_count = 0;

  while (!g_should_exit) {
    // Dynamic rate limiting based on buffer occupancy
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_broadcast_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_broadcast_time.tv_nsec) / 1000000;

    // Check buffer occupancy across all clients to determine processing speed
    int total_buffer_occupancy = 0;
    int total_buffer_capacity = 0;
    int active_buffer_count = 0;

    pthread_mutex_lock(&g_client_manager_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      if (client->active && client->is_sending_video && client->incoming_video_buffer) {
        size_t occupancy = ringbuffer_size(client->incoming_video_buffer->rb);
        size_t capacity = client->incoming_video_buffer->rb->capacity;
        total_buffer_occupancy += (int)occupancy;
        total_buffer_capacity += (int)capacity;
        active_buffer_count++;
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    // Calculate dynamic frame interval based on buffer occupancy
    int frame_interval_ms = base_frame_interval_ms;

    if (active_buffer_count > 0) {
      double occupancy_ratio = (double)total_buffer_occupancy / (double)total_buffer_capacity;

      if (occupancy_ratio > 0.3) {
        // ANY significant occupancy: speed up to 60 FPS to minimize lag
        frame_interval_ms = fast_frame_interval_ms;
        log_info("AGGRESSIVE MODE TRIGGERED: occupancy %.1f%% > 30%%, switching to 60 FPS", occupancy_ratio * 100.0);
      } else {
        // Low occupancy: normal 30 FPS (still responsive)
        frame_interval_ms = base_frame_interval_ms;
      }

      // Log dynamic rate changes occasionally
      static int rate_log_counter = 0;
      if (++rate_log_counter % 300 == 0) { // Every 10 seconds at 30 FPS
        log_info("Dynamic processing: %d buffers, occupancy %.1f%%, rate %.1f FPS", active_buffer_count,
                 occupancy_ratio * 100.0, 1000.0 / frame_interval_ms);
      }
    }

    if (elapsed_ms < frame_interval_ms) {
      interruptible_usleep((frame_interval_ms - elapsed_ms) * 1000);
      continue;
    }

    // Remove startup delay - might be causing issues
    // Clients should be ready when they set dimensions

    // log_debug("Broadcast thread tick (elapsed=%ldms)", elapsed_ms);

    // Check if we have any clients
    pthread_mutex_lock(&g_client_manager_mutex);
    int client_count = g_client_manager.client_count;

    // Count active clients that are ready to receive video
    int active_client_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      if (client->active && client->socket > 0 && client->width > 0 && client->height > 0) {
        active_client_count++;
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    if (client_count == 0) {
      // log_debug("Broadcast thread: No clients connected");
      interruptible_usleep(100000); // 100ms sleep when no clients - can be interrupted by shutdown
      last_connected_count = 0;
      continue;
    }

    // Check if the number of connected clients has changed (not just active)
    if (client_count != last_connected_count) {
      log_info("Connected client count changed from %d to %d, sending server state update", last_connected_count,
               client_count);

      // Create server state packet
      server_state_packet_t state;
      state.connected_client_count = client_count;
      state.active_client_count = active_client_count;
      memset(state.reserved, 0, sizeof(state.reserved));

      // Convert to network byte order
      server_state_packet_t net_state;
      net_state.connected_client_count = htonl(state.connected_client_count);
      net_state.active_client_count = htonl(state.active_client_count);
      memset(net_state.reserved, 0, sizeof(net_state.reserved));

      // Queue server state update to all clients
      pthread_mutex_lock(&g_client_manager_mutex);
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket > 0 &&
            g_client_manager.clients[i].video_queue) {
          int result = packet_queue_enqueue(g_client_manager.clients[i].video_queue, PACKET_TYPE_SERVER_STATE,
                                            &net_state, sizeof(net_state), 0, true);
          if (result < 0) {
            log_warn("Failed to queue server state for client %u", g_client_manager.clients[i].client_id);
          }
        }
      }
      pthread_mutex_unlock(&g_client_manager_mutex);

      // Update the tracked count
      last_connected_count = client_count;
    }

    static int frame_counter = 0;
    frame_counter++;
    if (frame_counter % MAX_FPS == 0) { // Log every MAX_FPS frames (1 second)
      log_info("Broadcast thread: frame %d, %d clients connected (%d active)", frame_counter, client_count,
               active_client_count);
    }

    // log_debug("Broadcast thread: %d clients connected", client_count);

    // Each client gets a custom-sized frame for their terminal
    // No more "common" dimensions that cause wrong-sized frames!

    int sent_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      // Get client info with mutex lock
      pthread_mutex_lock(&g_client_manager_mutex);
      client_info_t client_copy = g_client_manager.clients[i];
      pthread_mutex_unlock(&g_client_manager_mutex);

      // Skip if client hasn't finished initialization
      if (client_copy.width == 0 || client_copy.height == 0) {
        continue;
      }

      // Add debug logging to track what's happening with second client
      if (client_copy.active) {
        static int client_frame_count[MAX_CLIENTS] = {0};
        client_frame_count[i]++;
        if (client_frame_count[i] % MAX_FPS == 0) { // Log every 30 frames
          log_info("Broadcasting to client %u (slot %d): socket=%d, width=%u, height=%u, frames_sent=%d",
                   client_copy.client_id, i, client_copy.socket, client_copy.width, client_copy.height,
                   client_frame_count[i]);
        }
      }

      if (client_copy.active && client_copy.socket > 0) {
        // Allow all connected clients to receive frames immediately
        // This enables single clients to see their own video (like in video chat apps)

        // Create a custom-sized frame for THIS client's terminal dimensions
        size_t client_frame_size = 0;

        // DEBUG: Log frame generation parameters
        if (frame_counter % (MAX_FPS * 2) == 0) { // Log every 2 seconds
          log_info("FRAME DEBUG: Generating frame for client %u: %ux%u, color=%s", client_copy.client_id,
                   client_copy.width, client_copy.height, (client_copy.wants_color && opt_color_output) ? "yes" : "no");
        }

        char *client_frame =
            create_mixed_ascii_frame(client_copy.width, client_copy.height, client_copy.wants_color && opt_color_output,
                                     client_copy.wants_stretch, &client_frame_size);

        if (!client_frame || client_frame_size == 0) {
          // No frame available for this client, skip
          continue;
        }

        // Debug frame generation
        if (g_frame_debug_enabled) {
          frame_debug_record_frame(&g_server_frame_debug, client_frame, client_frame_size);
        }

        // BLANK FRAME DEBUG: Check if we're sending empty/blank frames
        bool is_blank_frame = true;
        size_t non_whitespace_count = 0;
        size_t printable_chars = 0;

        for (size_t j = 0; j < client_frame_size && j < 1024; j++) { // Check first 1KB
          char c = client_frame[j];
          if (c >= 32 && c < 127) { // Printable ASCII
            printable_chars++;
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
              non_whitespace_count++;
            }
          }
        }

        // Frame is blank if <1% non-whitespace or <10 total non-whitespace chars
        double non_ws_ratio = (printable_chars > 0) ? (double)non_whitespace_count / printable_chars : 0.0;
        is_blank_frame = (non_whitespace_count < 10 || non_ws_ratio < 0.01);

        if (is_blank_frame) {
          // Don't send blank frames - just skip this client
          log_debug("Skipping blank frame for client %u - size=%zu, non-ws=%zu/%zu (%.1f%%)", client_copy.client_id,
                    client_frame_size, non_whitespace_count, printable_chars, non_ws_ratio * 100.0);
          free(client_frame);
          continue;
        } else if (frame_counter % (MAX_FPS * 3) == 0) { // Log every 3 seconds for non-blank frames
          // Calculate expected size for verification
          size_t expected_size =
              (size_t)client_copy.width * client_copy.height + client_copy.height; // +height for newlines
          log_info("FRAME SIZE DEBUG: Client %u (%ux%u) - generated=%zu, expected~%zu, ratio=%.2f",
                   client_copy.client_id, client_copy.width, client_copy.height, client_frame_size, expected_size,
                   (double)client_frame_size / expected_size);
        }

        // Lock mutex while sending to ensure thread safety
        pthread_mutex_lock(&g_client_manager_mutex);
        // Verify client is still active and socket matches
        if (!g_client_manager.clients[i].active || g_client_manager.clients[i].socket != client_copy.socket) {
          pthread_mutex_unlock(&g_client_manager_mutex);
          log_warn("Client %u state changed during broadcast, skipping", client_copy.client_id);
          free(client_frame);
          continue;
        }
        pthread_mutex_unlock(&g_client_manager_mutex);

        // Create unified ASCII frame packet with THIS client's dimensions
        ascii_frame_packet_t frame_header = {.width = htonl(client_copy.width),
                                             .height = htonl(client_copy.height),
                                             .original_size = htonl(client_frame_size),
                                             .compressed_size = htonl(0), // Not compressed for now
                                             .checksum = htonl(asciichat_crc32(client_frame, client_frame_size)),
                                             .flags = htonl(client_copy.wants_color ? FRAME_FLAG_HAS_COLOR : 0)};

        // Allocate buffer for complete packet (header + data)
        size_t packet_size = sizeof(ascii_frame_packet_t) + client_frame_size;
        char *packet_buffer = malloc(packet_size);
        if (!packet_buffer) {
          log_error("Failed to allocate packet buffer for client %u", client_copy.client_id);
          free(client_frame);
          continue;
        }

        // Copy header and frame data into single buffer
        memcpy(packet_buffer, &frame_header, sizeof(ascii_frame_packet_t));
        memcpy(packet_buffer + sizeof(ascii_frame_packet_t), client_frame, client_frame_size);

        // Queue the complete frame as a single packet
        pthread_mutex_lock(&g_client_manager_mutex);
        if (g_client_manager.clients[i].active && g_client_manager.clients[i].video_queue) {
          int result = packet_queue_enqueue(g_client_manager.clients[i].video_queue, PACKET_TYPE_ASCII_FRAME,
                                            packet_buffer, packet_size, 0, true);
          if (result < 0) {
            log_error("Failed to queue ASCII frame for client %u: queue full or shutdown", client_copy.client_id);
          } else {
            sent_count++;
            static int success_count[MAX_CLIENTS] = {0};
            success_count[i]++;
            if (success_count[i] == 1 || success_count[i] % MAX_FPS == 0) {
              log_info("Successfully queued %d ASCII frames for client %u (slot %d, size=%zu)", success_count[i],
                       client_copy.client_id, i, packet_size);
            }
          }
        }
        pthread_mutex_unlock(&g_client_manager_mutex);

        free(packet_buffer);
        // Free the client-specific frame
        free(client_frame);
      }
    }

    if (sent_count > 0) {
      // log_debug("Sent frames to %d clients", sent_count);
    }

    last_broadcast_time = current_time;
  }

  g_video_broadcast_running = false;
  log_info("Video broadcast thread stopped");
  return NULL;
}

/* ============================================================================
 * Video Mixing Functions
 * ============================================================================
 */

// Create a mixed ASCII frame from all active image sources
char *create_mixed_ascii_frame(unsigned short width, unsigned short height, bool wants_color, bool wants_stretch,
                               size_t *out_size) {
  (void)wants_stretch; // Unused - we always handle aspect ratio ourselves
  if (!out_size || width == 0 || height == 0) {
    log_error("Invalid parameters for create_mixed_ascii_frame: width=%u, height=%u, out_size=%p", width, height,
              out_size);
    return NULL;
  }

  // Collect all active image sources
  typedef struct {
    image_t *image;
    uint32_t client_id;
  } image_source_t;

  image_source_t sources[MAX_CLIENTS];
  int source_count = 0;

  // Collect client image sources
  pthread_mutex_lock(&g_client_manager_mutex);
  /*int active_client_count = 0;               */
  /*for (int i = 0; i < MAX_CLIENTS; i++) {    */
  /*  if (g_client_manager.clients[i].active) {*/
  /*    active_client_count++;                 */
  /*  }                                        */
  /*}                                          */

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    if (client->active && client->is_sending_video) {
#ifdef DEBUG_THREADS
      log_debug("Client %d: active=%d, is_sending_video=%d", i, client->active, client->is_sending_video);
#endif
    }

    if (client->active && client->is_sending_video && source_count < MAX_CLIENTS) {
      // Skip clients who haven't sent their first real frame yet to avoid blank frames
      if (!client->has_cached_frame && client->incoming_video_buffer) {
        size_t occupancy = ringbuffer_size(client->incoming_video_buffer->rb);
        if (occupancy == 0) {
          // No frames available and no cached frame - skip this client for now
          continue;
        }
      }
      // ENHANCED RINGBUFFER USAGE: Dynamic frame reading based on buffer occupancy
      // - Normal mode: Read ONE frame (oldest first - FIFO)
      // - High occupancy mode: Read multiple frames to drain buffer quickly
      // - Always use cached frame if buffer is empty
      multi_source_frame_t current_frame = {0};
      bool got_new_frame = false;
      int frames_to_read = 1;

      // Check buffer occupancy to determine reading strategy
      if (client->incoming_video_buffer) {
        size_t occupancy = ringbuffer_size(client->incoming_video_buffer->rb);
        size_t capacity = client->incoming_video_buffer->rb->capacity;
        double occupancy_ratio = (double)occupancy / (double)capacity;

        if (occupancy_ratio > 0.3) {
          // AGGRESSIVE: Skip to latest frame for ANY significant occupancy
          // The goal is MINIMAL LAG - ringbuffers are for network problems, not normal operation
          frames_to_read = (int)occupancy - 1; // Read all but keep latest
          if (frames_to_read > 20)
            frames_to_read = 20; // Cap to avoid excessive processing
          if (frames_to_read < 1)
            frames_to_read = 1; // Always read at least 1
        }
        // else: Normal mode when buffer is nearly empty (< 30% occupancy)

        // Read frames (potentially multiple to drain buffer)
        multi_source_frame_t discard_frame = {0};
        for (int read_count = 0; read_count < frames_to_read; read_count++) {
          bool frame_available = framebuffer_read_multi_frame(
              client->incoming_video_buffer, (read_count == frames_to_read - 1) ? &current_frame : &discard_frame);
          if (!frame_available) {
            break; // No more frames available
          }

          if (read_count == frames_to_read - 1) {
            // This is the frame we'll use
            got_new_frame = true;
          } else {
            // This is a frame we're discarding to catch up
            if (discard_frame.data) {
              buffer_pool_free(discard_frame.data, discard_frame.size);
              discard_frame.data = NULL;
            }
          }
        }

        // Log buffer draining activity
        if (frames_to_read > 1) {
          static int drain_log_counter = 0;
          if (++drain_log_counter % 30 == 0) { // Log every 30 drain events
            log_info("Buffer draining: client %u, occupancy %.1f%%, read %d frames", client->client_id,
                     occupancy_ratio * 100.0, frames_to_read);
          }
        }

#ifdef DEBUG_THREADS
        log_debug("Client %d: occupancy=%.1f%%, read %d frames, got_new_frame=%d", i, occupancy_ratio * 100.0,
                  frames_to_read, got_new_frame);
#endif

        if (got_new_frame) {
          // Got a new frame - update our cache
          // Free old cached frame data if we had one
          if (client->has_cached_frame && client->last_valid_frame.data) {
            buffer_pool_free(client->last_valid_frame.data, client->last_valid_frame.size);
          }

          // Cache this frame for future use (make a copy)
          client->last_valid_frame.magic = current_frame.magic;
          client->last_valid_frame.source_client_id = current_frame.source_client_id;
          client->last_valid_frame.frame_sequence = current_frame.frame_sequence;
          client->last_valid_frame.timestamp = current_frame.timestamp;
          client->last_valid_frame.size = current_frame.size;

          // Allocate and copy frame data for cache using buffer pool
          client->last_valid_frame.data = buffer_pool_alloc(current_frame.size);
          if (client->last_valid_frame.data) {
            memcpy(client->last_valid_frame.data, current_frame.data, current_frame.size);
            client->has_cached_frame = true;
          }
        }
      }

      // Use either the new frame or the cached frame
      multi_source_frame_t *frame_to_use = NULL;
      if (got_new_frame) {
        frame_to_use = &current_frame;
      } else if (client->has_cached_frame) {
        frame_to_use = &client->last_valid_frame;
      }

      if (frame_to_use && frame_to_use->data && frame_to_use->size > sizeof(uint32_t) * 2) {
        // Parse the image data
        // Format: [width:4][height:4][rgb_data:w*h*3]
        uint32_t img_width = ntohl(*(uint32_t *)frame_to_use->data);
        uint32_t img_height = ntohl(*(uint32_t *)(frame_to_use->data + sizeof(uint32_t)));

        // Validate dimensions are reasonable (max 4K resolution)
        if (img_width == 0 || img_width > 4096 || img_height == 0 || img_height > 4096) {
          log_error("Invalid image dimensions from client %u: %ux%u (data may be corrupted)", client->client_id,
                    img_width, img_height);
          // Don't free cached frame, just skip this client
          if (got_new_frame) {
            buffer_pool_free(current_frame.data, current_frame.size);
          }
          continue;
        }

        // Validate that the frame size matches expected size
        size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
        if (frame_to_use->size != expected_size) {
          log_error("Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image", client->client_id,
                    frame_to_use->size, expected_size, img_width, img_height);
          // Don't free cached frame, just skip this client
          if (got_new_frame) {
            buffer_pool_free(current_frame.data, current_frame.size);
          }
          continue;
        }

        // The image data received from the client is formatted as:
        // [width:4 bytes][height:4 bytes][pixel data...]
        // So, the first 8 bytes (2 * sizeof(uint32_t)) are the width and height (each 4 bytes).
        // The actual pixel data (an array of rgb_t structs) starts immediately after these 8 bytes.
        // By adding sizeof(uint32_t) * 2 to the data pointer, we skip past the width and height fields
        // and point directly to the start of the pixel data.
        rgb_t *pixels = (rgb_t *)(frame_to_use->data + sizeof(uint32_t) * 2);

        // log_info("[SERVER COMPOSITE] Got image from client %u: %ux%u, aspect: %.3f (needs terminal adjustment)",
        //          client->client_id, img_width, img_height, (float)img_width / (float)img_height);

        // Create an image_t structure
        image_t *img = image_new(img_width, img_height);
        if (img) {
          memcpy(img->pixels, pixels, (size_t)img_width * (size_t)img_height * sizeof(rgb_t));
          sources[source_count].image = img;
          sources[source_count].client_id = client->client_id;
          source_count++;
          // log_debug("Got image from client %u: %ux%u", client->client_id, img_width, img_height);
        }

        // Free the current frame data if we got a new one (cached frame persists)
        if (got_new_frame) {
          buffer_pool_free(current_frame.data, current_frame.size);
        }
      }
    }
  }
  pthread_mutex_unlock(&g_client_manager_mutex);

  // No active video sources - don't generate placeholder frames
  if (source_count == 0) {
    *out_size = 0;
    return NULL;
  }

  // Create composite image for multiple sources with grid layout
  image_t *composite = NULL;

  if (source_count == 1 && sources[0].image) {
    // Single source - calculate proper dimensions for display

    // Use our helper function to calculate the best fit
    int display_width_chars, display_height_chars;
    calculate_fit_dimensions_pixel(sources[0].image->w, sources[0].image->h, width, height, &display_width_chars,
                                   &display_height_chars);

    // Create composite at exactly the display size in pixels
    // Since stretch=false, ascii_convert won't resize, so composite = output
    composite = image_new(display_width_chars, display_height_chars);
    if (!composite) {
      log_error("Failed to create composite image");
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        image_destroy(sources[i].image);
      }
      return NULL;
    }

    image_clear(composite);

    // log_info("[SERVER ASPECT] Created composite: %dx%d pixels for %dx%d char display", display_width_chars,
    //          display_height_chars, display_width_chars, display_height_chars);

    // Resize source image directly to composite
    image_resize(sources[0].image, composite);
  } else if (source_count > 1) {
    // Multiple sources - create grid layout
    // IMPORTANT: Create composite in PIXEL space, not character space
    // Since each character is 2 pixels tall, we need height * 2 pixels
    composite = image_new(width, height * 2);
    if (!composite) {
      log_error("Failed to create composite image");
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        image_destroy(sources[i].image);
      }
      return NULL;
    }

    // Clear the composite with black background
    image_clear(composite);

    // Calculate grid dimensions based on source count
    // For 2 sources: side by side (2x1 grid)
    // For 3-4 sources: 2x2 grid
    // For 5-9 sources: 3x3 grid
    int grid_cols = (source_count == 2) ? 2 : (source_count <= 4) ? 2 : 3;
    int grid_rows = (source_count + grid_cols - 1) / grid_cols;

    // log_info("[GRID] Creating %dx%d grid for %d sources on %dx%d terminal", grid_cols, grid_rows, source_count,
    // width,
    //  height);

    // Calculate cell dimensions in characters
    int cell_width = width / grid_cols;
    int cell_height = height / grid_rows;

    // log_info("[GRID] Each cell is %dx%d chars (terminal %dx%d / grid %dx%d)", cell_width, cell_height, width, height,
    //          grid_cols, grid_rows);

    // Place each source in the grid
    for (int i = 0; i < source_count && i < 9; i++) { // Max 9 sources in 3x3 grid
      if (!sources[i].image)
        continue;

      int row = i / grid_cols;
      int col = i % grid_cols;
      int cell_x_offset = col * cell_width;
      int cell_y_offset = row * cell_height;

      float src_aspect = (float)sources[i].image->w / (float)sources[i].image->h;
      // log_info("[GRID CELL %d] Source: %dx%d (aspect %.3f), Cell: %dx%d chars at (%d,%d)", i, sources[i].image->w,
      //          sources[i].image->h, src_aspect, cell_width, cell_height, cell_x_offset, cell_y_offset);

      // For grid cells, calculate dimensions to fill at least one dimension
      // while maintaining aspect ratio
      // IMPORTANT: We need to work in pixel space for the image
      // The cell dimensions need special handling:
      // - Width: 1 char = 1 pixel width (no conversion needed)
      // - Height: 1 char = 2 pixels height (terminal aspect ratio)

      // Convert cell dimensions to pixel space for aspect ratio calculations
      int cell_width_px = cell_width;       // Width in pixels = width in chars
      int cell_height_px = cell_height * 2; // Height in pixels = chars * 2

      // Calculate the best fit maintaining aspect ratio
      // src_aspect already calculated above
      float cell_aspect = (float)cell_width_px / (float)cell_height_px;

      int target_width_px, target_height_px;

      if (src_aspect > cell_aspect) {
        // Image is wider than cell - fit to width
        target_width_px = cell_width_px;
        target_height_px = (int)(cell_width_px / src_aspect + 0.5f);
      } else {
        // Image is taller than cell - fit to height
        target_height_px = cell_height_px;
        target_width_px = (int)(cell_height_px * src_aspect + 0.5f);
      }

      // log_info("[GRID CELL %d] Best fit in cell: %dx%d pixels (from %dx%d image, cell %dx%d chars = %dx%d px)", i,
      //          target_width_px, target_height_px, sources[i].image->w, sources[i].image->h, cell_width, cell_height,
      //          cell_width_px, cell_height_px);

      // Create resized image with pixel dimensions
      image_t *resized = image_new(target_width_px, target_height_px);
      if (resized) {
        image_resize(sources[i].image, resized);

        // Center the image in the cell
        // Convert pixel dimensions back to character dimensions for centering
        int target_width_chars = target_width_px;
        int target_height_chars = target_height_px / 2; // Convert pixels to chars

        int x_padding = (cell_width - target_width_chars) / 2;
        int y_padding = (cell_height - target_height_chars) / 2;

        // log_debug("Cell %d: Centering %dx%d px (%dx%d chars) in %dx%d cell, padding: %d,%d", i, target_width_px,
        //           target_height_px, target_width_chars, target_height_chars, cell_width, cell_height, x_padding,
        //           y_padding);

        // Copy resized image to composite (now in pixel space)
        for (int y = 0; y < target_height_px; y++) {
          for (int x = 0; x < target_width_px; x++) {
            int src_idx = y * target_width_px + x;
            // Convert character offsets to pixel offsets
            // x_padding and cell_x_offset are in characters, keep them as-is for X
            // For Y, we need to convert character offsets to pixel offsets (multiply by 2)
            int dst_x = cell_x_offset + x_padding + x;
            int dst_y = (cell_y_offset + y_padding) * 2 + y; // Convert char offset to pixel offset
            int dst_idx = dst_y * width + dst_x;

            if (src_idx < resized->w * resized->h && dst_idx < composite->w * composite->h &&
                dst_x < (cell_x_offset + cell_width) && dst_y < ((cell_y_offset + cell_height) * 2)) {
              composite->pixels[dst_idx] = resized->pixels[src_idx];
            }
          }
        }

        image_destroy(resized);
      }
    }

    // log_debug("Created grid layout: %dx%d grid for %d sources", grid_cols, grid_rows, source_count);
  } else {
    // No sources, create empty composite
    composite = image_new(width, height);
    if (!composite) {
      log_error("Failed to create empty image");
      *out_size = 0;
      return NULL;
    }
    image_clear(composite);
  }

  // Pass the terminal dimensions of the client to ascii_convert
  // The composite is already sized correctly in pixels (width*2, height)
  // ascii_convert expects character dimensions, so we pass the original width and height
  // Pass stretch=false because we've already sized the composite to the exact dimensions we want
  char *ascii_frame = ascii_convert(composite, width, height, wants_color, true, false);

  if (ascii_frame) {
    *out_size = strlen(ascii_frame);

    // Cache this valid frame to prevent flickering when source_count becomes 0
    pthread_mutex_lock(&g_frame_cache_mutex);

    // Free old cached frame
    if (g_last_valid_frame) {
      free(g_last_valid_frame);
      g_last_valid_frame = NULL; // CRITICAL: Prevent double-free
      g_last_valid_frame_size = 0;
    }

    // Cache the new frame
    g_last_valid_frame_size = *out_size;
    SAFE_MALLOC(g_last_valid_frame, g_last_valid_frame_size, char *);
    if (g_last_valid_frame) {
      memcpy(g_last_valid_frame, ascii_frame, g_last_valid_frame_size);
      g_last_frame_width = width;
      g_last_frame_height = height;
      g_last_frame_was_color = wants_color;
    }

    pthread_mutex_unlock(&g_frame_cache_mutex);
  } else {
    log_error("Failed to convert image to ASCII");
    *out_size = 0;
  }

  // We always create a new composite image regardless of source count
  image_destroy(composite);

  // Always destroy source images as we created them
  for (int i = 0; i < source_count; i++) {
    if (sources[i].image) {
      image_destroy(sources[i].image);
    }
  }

  return ascii_frame;
}

// Cleanup frame cache on shutdown
void cleanup_frame_cache() {
  pthread_mutex_lock(&g_frame_cache_mutex);
  if (g_last_valid_frame) {
    free(g_last_valid_frame);
    g_last_valid_frame = NULL;
    g_last_valid_frame_size = 0;
    // Reset other cache state to prevent stale data
    g_last_frame_width = 0;
    g_last_frame_height = 0;
    g_last_frame_was_color = false;
  }
  pthread_mutex_unlock(&g_frame_cache_mutex);
}

/* ============================================================================
 * Main Server Logic
 * ============================================================================
 */

int main(int argc, char *argv[]) {
  options_init(argc, argv);

  // Initialize logging - use specified log file or default
  const char *log_filename = (strlen(opt_log_file) > 0) ? opt_log_file : "server.log";
  log_init(log_filename, LOG_DEBUG);
  atexit(log_destroy);

#ifdef DEBUG_MEMORY
  atexit(debug_memory_report);
#endif

  // Initialize global shared buffer pool
  data_buffer_pool_init_global();
  atexit(data_buffer_pool_cleanup_global);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat server starting...");

  log_info("SERVER: Options initialized, using log file: %s", log_filename);
  int port = strtoint(opt_port);
  log_info("SERVER: Port set to %d", port);

  log_info("SERVER: Precalculating luminance palette...");
  precalc_luminance_palette();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);
  log_info("SERVER: RGB palettes precalculated");

  // Handle terminal resize events
  log_info("SERVER: Setting up signal handlers...");
  signal(SIGWINCH, sigwinch_handler);

  // Simple signal handling (temporarily disable complex threading signal handling)
  log_info("SERVER: Setting up simple signal handlers...");

  // Handle Ctrl+C for cleanup
  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigterm_handler);

  // Ignore SIGPIPE
  signal(SIGPIPE, SIG_IGN);
  log_info("SERVER: Signal handling setup complete");
  // Initialize audio mixer if audio is enabled
  log_info("SERVER: Checking audio initialization (audio_enabled=%s)", opt_audio_enabled ? "true" : "false");
  if (opt_audio_enabled) {
    // Initialize the new advanced audio mixer for multi-user audio mixing
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      log_error("Failed to initialize audio mixer");
    } else {
      log_info("SERVER: Audio mixer initialized successfully");
      // Start audio mixer thread
      if (pthread_create(&g_audio_mixer_thread, NULL, audio_mixer_thread_func, NULL) != 0) {
        log_error("Failed to create audio mixer thread");
        mixer_destroy(g_audio_mixer);
        g_audio_mixer = NULL;
      } else {
        g_audio_mixer_thread_created = true;
        log_info("Advanced audio mixer initialized with ducking and compression");
      }
    }
  }

  // Start video broadcast thread for mixing and sending frames to all clients
  log_info("SERVER: Creating video broadcast thread...");
  if (pthread_create(&g_video_broadcast_thread, NULL, video_broadcast_thread_func, NULL) != 0) {
    log_error("Failed to create video broadcast thread");
  } else {
    log_info("Video broadcast thread started");
  }

  // Start statistics logging thread for periodic performance monitoring
  log_info("SERVER: Creating statistics logger thread...");
  if (pthread_create(&g_stats_logger_thread, NULL, stats_logger_thread_func, NULL) != 0) {
    log_error("Failed to create statistics logger thread");
  } else {
    g_stats_logger_thread_created = true;
    log_info("Statistics logger thread started");
  }

  // Network setup
  log_info("SERVER: Setting up network sockets...");
  struct sockaddr_in serv_addr;
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  log_info("SERVER: Creating listen socket...");
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    log_fatal("Failed to create socket: %s", strerror(errno));
    exit(1);
  }
  log_info("SERVER: Listen socket created (fd=%d)", listenfd);

  log_info("Server listening on port %d", port);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  // Set socket options
  int yes = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    log_fatal("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    perror("setsockopt");
    exit(ASCIICHAT_ERR_NETWORK);
  }

  // If we Set keep-alive on the listener before accept(), connfd will inherit it.
  if (set_socket_keepalive(listenfd) < 0) {
    log_warn("Failed to set keep-alive on listener: %s", strerror(errno));
  }

  // Bind socket
  if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    log_fatal("Socket bind failed: %s", strerror(errno));
    perror("Error: network bind failed");
    exit(1);
  }

  // Listen for connections
  if (listen(listenfd, 10) < 0) {
    log_fatal("Connection listen failed: %s", strerror(errno));
    exit(1);
  }

  struct timespec last_stats_time;
  clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

  // Initialize client manager
  memset(&g_client_manager, 0, sizeof(g_client_manager));
  pthread_mutex_init(&g_client_manager.mutex, NULL);
  g_client_manager.next_client_id = 0;

  // Initialize client hash table for O(1) lookup
  g_client_manager.client_hashtable = hashtable_create();
  if (!g_client_manager.client_hashtable) {
    log_fatal("Failed to create client hash table");
    exit(1);
  }

  // Initialize frame debugging
  g_frame_debug_enabled = true; // Enable by default for debugging
  g_frame_debug_verbosity = 2;  // Show warnings and stats
  frame_debug_init(&g_server_frame_debug, "Server-VideoGeneration");

  // Main multi-client connection loop
  while (!g_should_exit) {
    // Only log when client count changes
    static int last_logged_count = -1;
    if (g_client_manager.client_count != last_logged_count) {
      log_info("Waiting for client connections... (%d/%d clients)", g_client_manager.client_count, MAX_CLIENTS);
      last_logged_count = g_client_manager.client_count;
    }

    // Check for disconnected clients BEFORE accepting new ones
    // This ensures slots are freed up for new connections
    pthread_mutex_lock(&g_client_manager_mutex);
    int i = 0;
    while (i < MAX_CLIENTS) {
      client_info_t *client = &g_client_manager.clients[i];
      // Check if this client has been marked inactive by its receive thread
      if (client->client_id != 0 && !client->active && client->receive_thread != 0) {
        // Client marked as inactive and has a thread to clean up
        uint32_t client_id = client->client_id;
        pthread_t receive_thread = client->receive_thread;

        // Clear the thread handle immediately to avoid double-join
        client->receive_thread = 0;

        pthread_mutex_unlock(&g_client_manager_mutex);

        log_info("Cleaning up disconnected client %u", client_id);
        // Wait for receive thread to finish
        pthread_join(receive_thread, NULL);

        // Remove the client and clean up resources
        remove_client(client_id);

        // Start over from the beginning since we released the lock
        pthread_mutex_lock(&g_client_manager_mutex);
        i = 0;
        continue;
      }
      i++;
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    // Accept network connection with timeout
    int client_sock = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    if (client_sock < 0) {
      if (errno == ETIMEDOUT) {
        // Timeout is normal, just continue
#ifdef DEBUG_MEMORY
        // debug_memory_report();
#endif
        continue;
      }
      if (errno == EINTR) {
        // Interrupted by signal - check if we should exit
        log_debug("accept() interrupted by signal");
        if (g_should_exit) {
          break;
        }
        continue;
      }
      log_error("Network accept failed: %s", network_error_string(errno));
      continue;
    }

    // Log client connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    log_info("New client connected from %s:%d", client_ip, client_port);

    // Add client to multi-client manager
    int client_id = add_client(client_sock, client_ip, client_port);
    if (client_id < 0) {
      log_error("Failed to add client, rejecting connection");
      close(client_sock);
      continue;
    }

    log_info("Client %d added successfully, total clients: %d", client_id, g_client_manager.client_count);

    // Check if we should exit after processing this client
    if (g_should_exit) {
      break;
    }
  }

  // Cleanup
  log_info("Server shutting down...");
  g_should_exit = true;

  // Wake up all sleeping threads before waiting for them
  pthread_cond_broadcast(&g_shutdown_cond);

  // CRITICAL: Close all client sockets to interrupt blocking receive_packet() calls
  log_info("Closing all client sockets to interrupt blocking I/O...");
  pthread_mutex_lock(&g_client_manager_mutex);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket > 0) {
      log_debug("Closing socket for client %u to interrupt receive thread", g_client_manager.clients[i].client_id);
      shutdown(g_client_manager.clients[i].socket, SHUT_RDWR);
      close(g_client_manager.clients[i].socket);
      g_client_manager.clients[i].socket = -1;
    }
  }
  pthread_mutex_unlock(&g_client_manager_mutex);

  // Wait for video broadcast thread to finish
  if (g_video_broadcast_running) {
    log_info("Waiting for video broadcast thread to finish...");
    pthread_join(g_video_broadcast_thread, NULL);
    log_info("Video broadcast thread stopped");
  }

  // Wait for stats logger thread to finish
  if (g_stats_logger_thread_created) {
    log_info("Waiting for stats logger thread to finish...");
    pthread_join(g_stats_logger_thread, NULL);
    log_info("Stats logger thread stopped");
    g_stats_logger_thread_created = false;
  }

  // Cleanup audio mixer if enabled
  if (opt_audio_enabled) {
    // Wait for audio mixer thread to finish
    if (g_audio_mixer_thread_created) {
      log_info("Waiting for audio mixer thread to finish...");
      pthread_join(g_audio_mixer_thread, NULL);
      log_info("Audio mixer thread stopped");
      g_audio_mixer_thread_created = false;
    }

    // Cleanup audio mixer
    if (g_audio_mixer) {
      mixer_destroy(g_audio_mixer);
      g_audio_mixer = NULL;
    }
  }

  // Cleanup cached frames
  cleanup_frame_cache();

  // Clean up all connected clients
  log_info("Cleaning up connected clients...");

  // First, close all client sockets to interrupt receive threads
  pthread_mutex_lock(&g_client_manager_mutex);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->active && client->socket > 0) {
      log_debug("Closing socket for client %u to interrupt receive thread", client->client_id);
      shutdown(client->socket, SHUT_RDWR);
      close(client->socket);
      client->socket = 0; // Mark as closed
    }
  }
  pthread_mutex_unlock(&g_client_manager_mutex);

  // Now clean up client resources
  log_info("Scanning for clients to clean up (active or with allocated resources)...");
  pthread_mutex_lock(&g_client_manager_mutex);
  int active_clients_found = 0;
  int inactive_clients_with_resources = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->client_id != 0) { // Client slot has been used
      bool has_resources = (client->audio_queue != NULL || client->video_queue != NULL ||
                            client->incoming_video_buffer != NULL || client->incoming_audio_buffer != NULL);

      if (client->active) {
        active_clients_found++;
        log_info("Found active client %u in slot %d during shutdown cleanup", client->client_id, i);
        pthread_mutex_unlock(&g_client_manager_mutex);
        remove_client(client->client_id);
        pthread_mutex_lock(&g_client_manager_mutex);
      } else if (has_resources) {
        inactive_clients_with_resources++;
        log_info("Found inactive client %u in slot %d with allocated resources - cleaning up", client->client_id, i);
        pthread_mutex_unlock(&g_client_manager_mutex);
        remove_client(client->client_id);
        pthread_mutex_lock(&g_client_manager_mutex);
      }
    }
  }
  pthread_mutex_unlock(&g_client_manager_mutex);
  log_info("Client cleanup complete - removed %d active clients and %d inactive clients with resources",
           active_clients_found, inactive_clients_with_resources);

  // Cleanup resources
  // No server framebuffer or webcam to clean up
  close(listenfd);

  // Final statistics
  pthread_mutex_lock(&g_stats_mutex);
  log_info("Final stats: captured=%lu, sent=%lu, dropped=%lu", g_stats.frames_captured, g_stats.frames_sent,
           g_stats.frames_dropped);
  pthread_mutex_unlock(&g_stats_mutex);

  printf("Server shutdown complete.\n");

  // Cleanup client manager resources
  if (g_client_manager.client_hashtable) {
    hashtable_destroy(g_client_manager.client_hashtable);
    g_client_manager.client_hashtable = NULL;
  }

  // No emergency cleanup needed - proper resource management handles cleanup

  // Explicitly clean up global buffer pool before atexit handlers
  // This ensures any malloc fallbacks are freed while pool tracking is still active
  data_buffer_pool_cleanup_global();

  // Destroy mutexes (do this before log_destroy in case logging uses them)
  pthread_mutex_destroy(&g_stats_mutex);
  pthread_mutex_destroy(&g_socket_mutex);
  pthread_mutex_destroy(&g_client_manager_mutex);

  return 0;
}

/* ============================================================================
 * Multi-Client Thread Functions
 * ============================================================================
 */

// Thread function to handle incoming data from a specific client
// Helper function to handle PACKET_TYPE_IMAGE_FRAME case
static void handle_image_frame_packet(client_info_t *client, void *data, size_t len) {
  // Handle incoming image data from client
  // Format: [width:4][height:4][rgb_data:w*h*3]
  if (!client->is_sending_video) {
    // Auto-enable video sending when we receive image frames
    client->is_sending_video = true;
    log_info("Client %u auto-enabled video stream (received IMAGE_FRAME)", client->client_id);
  } else {
    // Log periodically to confirm we're receiving frames
    static int frame_count[MAX_CLIENTS] = {0};
    frame_count[client->client_id % MAX_CLIENTS]++;
    if (frame_count[client->client_id % MAX_CLIENTS] % 100 == 0) {
      log_debug("Client %u has sent %d IMAGE_FRAME packets", client->client_id,
                frame_count[client->client_id % MAX_CLIENTS]);
    }
  }
  if (data && len > sizeof(uint32_t) * 2) {
    // Parse image dimensions
    uint32_t img_width = ntohl(*(uint32_t *)data);
    uint32_t img_height = ntohl(*(uint32_t *)(data + sizeof(uint32_t)));
    size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);

    // log_info("[SERVER RECEIVE] Client %u sent frame: %ux%u, aspect: %.3f (original aspect)", client->client_id,
    //          img_width, img_height, (float)img_width / (float)img_height);

    if (len != expected_size) {
      log_error("Invalid image packet from client %u: expected %zu bytes, got %zu", client->client_id, expected_size,
                len);
      return;
    }

    // log_debug("Received image from client %u: %ux%u", client->client_id, img_width, img_height);

    // DEBUG: Analyze image brightness to understand webcam startup behavior
    if (len > sizeof(uint32_t) * 2) {
      const rgb_t *pixels = (const rgb_t *)(data + sizeof(uint32_t) * 2);
      size_t pixel_count = (size_t)img_width * (size_t)img_height;

      // Sample brightness from first 100 pixels or all pixels if fewer
      size_t sample_size = (pixel_count < 100) ? pixel_count : 100;
      uint32_t total_brightness = 0;
      uint32_t black_pixels = 0;

      for (size_t i = 0; i < sample_size; i++) {
        uint32_t brightness = (uint32_t)pixels[i].r + (uint32_t)pixels[i].g + (uint32_t)pixels[i].b;
        total_brightness += brightness;
        if (brightness < 10)
          black_pixels++; // Very dark pixels
      }

      double avg_brightness = (double)total_brightness / (sample_size * 3.0 * 255.0) * 100.0;
      double black_percentage = (double)black_pixels / sample_size * 100.0;

      // Log every 10th frame to track webcam warmup
      static int frame_debug_counter[MAX_CLIENTS] = {0};
      frame_debug_counter[client->client_id % MAX_CLIENTS]++;

      if (frame_debug_counter[client->client_id % MAX_CLIENTS] % 10 == 1) {
        log_info("WEBCAM WARMUP DEBUG: Client %u frame #%d - avg_brightness=%.1f%%, black_pixels=%.1f%% (%zu/%zu)",
                 client->client_id, frame_debug_counter[client->client_id % MAX_CLIENTS], avg_brightness,
                 black_percentage, black_pixels, sample_size);
      }
    }

    // Store the entire packet (including dimensions) in the buffer
    // The mixing function will parse it
    uint32_t timestamp = (uint32_t)time(NULL);
    if (client->incoming_video_buffer) {
      bool stored = framebuffer_write_multi_frame(client->incoming_video_buffer, (const char *)data, len,
                                                  client->client_id, 0, timestamp);
      if (stored) {
        client->frames_received++;
#ifdef DEBUG_THREADS
        log_debug("Stored image from client %u (size=%zu, total=%llu)", client->client_id, len,
                  client->frames_received);
#endif
      } else {
        log_warn("Failed to store image from client %u (buffer full?)", client->client_id);
      }
    } else {
      // During shutdown, this is expected - don't spam error logs
      if (!g_should_exit) {
        log_error("Client %u has no incoming video buffer!", client->client_id);
      } else {
        log_debug("Client %u: ignoring video packet during shutdown", client->client_id);
      }
    }
  } else {
    log_debug("Ignoring video packet: len=%zu (too small)", len);
  }
}

void *client_receive_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in receive thread");
    return NULL;
  }

  // Enable thread cancellation for clean shutdown
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  log_info("Started receive thread for client %u (%s)", client->client_id, client->display_name);

  packet_type_t type;
  uint32_t sender_id;
  void *data = NULL; // Initialize to prevent static analyzer warning about uninitialized use
  size_t len;

  while (!g_should_exit && client->active) {
    // Receive packet from this client
    int result = receive_packet_with_client(client->socket, &type, &sender_id, &data, &len);

    if (result <= 0) {
      if (result == 0) {
        log_info("DISCONNECT: Client %u disconnected (clean close, result=0)", client->client_id);
      } else {
        log_error("DISCONNECT: Error receiving from client %u (result=%d): %s", client->client_id, result,
                  strerror(errno));
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
    case PACKET_TYPE_CLIENT_JOIN: {
      // Handle client join request
      if (len == sizeof(client_info_packet_t)) {
        const client_info_packet_t *join_info = (const client_info_packet_t *)data;
        SAFE_STRNCPY(client->display_name, join_info->display_name, MAX_DISPLAY_NAME_LEN - 1);
        client->can_send_video = (join_info->capabilities & CLIENT_CAP_VIDEO) != 0;
        client->can_send_audio = (join_info->capabilities & CLIENT_CAP_AUDIO) != 0;
        client->wants_color = (join_info->capabilities & CLIENT_CAP_COLOR) != 0;
        client->wants_stretch = (join_info->capabilities & CLIENT_CAP_STRETCH) != 0;
        log_info("Client %u joined: %s (video=%d, audio=%d, color=%d, stretch=%d)", client->client_id,
                 client->display_name, client->can_send_video, client->can_send_audio, client->wants_color,
                 client->wants_stretch);

        // REMOVED: Don't send CLEAR_CONSOLE to other clients when a new client joins
        // This was causing flickering for existing clients
        // The grid layout will update naturally with the next frame
      }
      break;
    }

    case PACKET_TYPE_STREAM_START: {
      // Handle stream start request
      if (len == sizeof(uint32_t)) {
        uint32_t stream_type = ntohl(*(uint32_t *)data);
        if (stream_type & STREAM_TYPE_VIDEO) {
          client->is_sending_video = true;
          log_info("Client %u started video stream", client->client_id);
        }
        if (stream_type & STREAM_TYPE_AUDIO) {
          client->is_sending_audio = true;
          log_info("Client %u started audio stream", client->client_id);
        }
      }
      break;
    }

    case PACKET_TYPE_STREAM_STOP: {
      // Handle stream stop request
      if (len == sizeof(uint32_t)) {
        uint32_t stream_type = ntohl(*(uint32_t *)data);
        if (stream_type & STREAM_TYPE_VIDEO) {
          client->is_sending_video = false;
          log_info("Client %u stopped video stream", client->client_id);
        }
        if (stream_type & STREAM_TYPE_AUDIO) {
          client->is_sending_audio = false;
          log_info("Client %u stopped audio stream", client->client_id);
        }
      }
      break;
    }

    case PACKET_TYPE_IMAGE_FRAME: {
      handle_image_frame_packet(client, data, len);
      break;
    }

    case PACKET_TYPE_AUDIO: {
      // Handle incoming audio samples from client (old single-packet format)
      if (client->is_sending_audio && data && len > 0) {
        // Convert data to float samples
        int num_samples = len / sizeof(float);
        if (num_samples > 0 && client->incoming_audio_buffer) {
          const float *samples = (const float *)data;
          int written = audio_ring_buffer_write(client->incoming_audio_buffer, samples, num_samples);
          // Note: audio_ring_buffer_write now always writes all samples, dropping old ones if needed
          (void)written;
          // log_debug("Stored %d audio samples from client %u", num_samples, client->client_id);
        }
      }
      break;
    }

    case PACKET_TYPE_AUDIO_BATCH: {
      // Handle batched audio samples from client (new efficient format)
      if (client->is_sending_audio && data && len >= sizeof(audio_batch_packet_t)) {
        // Parse batch header
        const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;
        uint32_t batch_count = ntohl(batch_header->batch_count);
        uint32_t total_samples = ntohl(batch_header->total_samples);
        uint32_t sample_rate = ntohl(batch_header->sample_rate);
        // uint32_t channels = ntohl(batch_header->channels); // For future stereo support

        // Suppress static analyzer warnings for conditionally used variables
        (void)batch_count; // Used in DEBUG_AUDIO log
        (void)sample_rate; // Used in DEBUG_AUDIO log

        // Validate batch parameters
        size_t expected_size = sizeof(audio_batch_packet_t) + (total_samples * sizeof(float));
        if (len != expected_size) {
          log_error("Invalid audio batch size from client %u: got %zu, expected %zu", client->client_id, len,
                    expected_size);
          break;
        }

        if (total_samples > AUDIO_BATCH_SAMPLES * 2) { // Sanity check
          log_error("Audio batch too large from client %u: %u samples", client->client_id, total_samples);
          break;
        }

        // Extract samples (they follow the header)
        const float *samples = (const float *)((const uint8_t *)data + sizeof(audio_batch_packet_t));

        // Write all samples to the ring buffer
        if (client->incoming_audio_buffer) {
          int written = audio_ring_buffer_write(client->incoming_audio_buffer, samples, total_samples);
          // Note: audio_ring_buffer_write now always writes all samples, dropping old ones if needed
          (void)written;
#ifdef DEBUG_AUDIO
          log_debug("Stored audio batch from client %u: %u chunks, %u samples @ %uHz", client->client_id, batch_count,
                    total_samples, sample_rate);
#endif
        }
      }
      break;
    }

    case PACKET_TYPE_SIZE: {
      // Handle size update from client
      if (len == 4) {
        const uint16_t *size_data = (const uint16_t *)data;
        client->width = ntohs(size_data[0]);
        client->height = ntohs(size_data[1]);
        log_info("Client %u updated size to %ux%u (active=%d, socket=%d)", client->client_id, client->width,
                 client->height, client->active, client->socket);
      }
      break;
    }

    case PACKET_TYPE_PING: {
      // Handle ping from client - queue pong response
      if (client->video_queue) {
        // PONG packet has no payload
        int result = packet_queue_enqueue(client->video_queue, PACKET_TYPE_PONG, NULL, 0, 0, false);
        if (result < 0) {
          log_debug("Failed to queue PONG response for client %u", client->client_id);
        } else {
          log_debug("Queued PONG response for client %u", client->client_id);
        }
      }
      break;
    }

    case PACKET_TYPE_PONG: {
      // Handle pong from client - just log it
      log_debug("Received PONG from client %u", client->client_id);
      break;
    }

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
  log_info("DISCONNECT: Client %u (in slot ?) marked as inactive due to receive thread termination", client->client_id);
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
  log_debug("SEND_THREAD_DEBUG: Send thread started for client %u", client->client_id);

  // Mark thread as running
  client->send_thread_running = true;

  while (!g_should_exit && client->active && client->send_thread_running) {
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
        if (!g_should_exit) {
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
          if (!g_should_exit) {
            log_error("Failed to send packet payload to client %u: %zd/%zu bytes", client->client_id, sent,
                      packet->data_len);
          } else {
            log_debug("Client %u: payload send failed during shutdown", client->client_id);
          }
          packet_queue_free_packet(packet);
          break; // Socket error, exit thread
        }
      }

// Successfully sent packet
#ifdef NETWORK_DEBUG
      uint16_t pkt_type = ntohs(packet->header.type);
      log_debug("Sent packet type=%d to client %u (len=%zu)", pkt_type, client->client_id, packet->data_len);
#endif

      // Free the packet
      packet_queue_free_packet(packet);
    }
  }

  // Mark thread as stopped
  client->send_thread_running = false;
  log_debug("SEND_THREAD_DEBUG: Client %u send thread exiting (g_should_exit=%d, active=%d, running=%d)",
            client->client_id, g_should_exit, client->active, client->send_thread_running);
  log_info("Send thread for client %u terminated", client->client_id);
  return NULL;
}

// Client management functions
int add_client(int socket, const char *client_ip, int port) {
  pthread_mutex_lock(&g_client_manager_mutex);

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
    pthread_mutex_unlock(&g_client_manager_mutex);
    log_error("No available client slots (all %d slots are in use)", MAX_CLIENTS);

    // Send a rejection message to the client before closing
    const char *reject_msg = "SERVER_FULL: Maximum client limit reached\n";
    send(socket, reject_msg, strlen(reject_msg), MSG_NOSIGNAL);

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
    pthread_mutex_unlock(&g_client_manager_mutex);
    return -1;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create();
  if (!client->incoming_audio_buffer) {
    log_error("Failed to create audio buffer for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
    pthread_mutex_unlock(&g_client_manager_mutex);
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
    pthread_mutex_unlock(&g_client_manager_mutex);
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
    pthread_mutex_unlock(&g_client_manager_mutex);
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
      log_debug("Added client %u to audio mixer", client->client_id);
    }
  }

  pthread_mutex_unlock(&g_client_manager_mutex);

  // Start threads for this client
  if (pthread_create(&client->receive_thread, NULL, client_receive_thread_func, client) != 0) {
    log_error("Failed to create receive thread for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  // Start send thread for this client
  log_debug("SEND_THREAD_DEBUG: Creating send thread for client %u", client->client_id);
  if (pthread_create(&client->send_thread, NULL, client_send_thread_func, client) != 0) {
    log_error("SEND_THREAD_DEBUG: FAILED to create send thread for client %u", client->client_id);
    log_error("Failed to create send thread for client %u", client->client_id);
    // Join the receive thread before cleaning up to prevent race conditions
    pthread_join(client->receive_thread, NULL);
    // Now safe to remove client (won't double-free since first thread creation succeeded)
    remove_client(client->client_id);
    return -1;
  }

  log_info("Client %u initialized with dedicated send thread", client->client_id);

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
  } else {
    log_info("Queued initial server state for client %u: %u connected clients", client->client_id,
             state.connected_client_count);
  }

  log_info("Added client %u from %s:%d", client->client_id, client_ip, port);
  return client->client_id;
}

int remove_client(uint32_t client_id) {
  pthread_mutex_lock(&g_client_manager_mutex);

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
      if (client->send_thread != 0) {
        // The shutdown signal above will cause the send thread to exit
        int join_result = pthread_join(client->send_thread, NULL);
        if (join_result == 0) {
          log_debug("Send thread for client %u has terminated", client_id);
        } else {
          log_warn("Failed to join send thread for client %u: %d", client_id, join_result);
        }
      }

      // Join receive thread if it exists and we're not in the receive thread context
      if (client->receive_thread != 0 && !pthread_equal(pthread_self(), client->receive_thread)) {
        int join_result = pthread_join(client->receive_thread, NULL);
        if (join_result == 0) {
          log_debug("Receive thread for client %u has terminated", client_id);
        } else {
          log_warn("Failed to join receive thread for client %u: %d", client_id, join_result);
        }
      }

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
        log_debug("Removed client %u from audio mixer", client_id);
      }

      // Remove from hash table
      if (!hashtable_remove(g_client_manager.client_hashtable, client_id)) {
        log_warn("Failed to remove client %u from hash table", client_id);
      }

      // Store display name before clearing
      char display_name_copy[MAX_DISPLAY_NAME_LEN];
      SAFE_STRNCPY(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

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

      log_info("CLIENT REMOVED: client_id=%u (%s) removed from slot ?, remaining clients: %d", client_id,
               display_name_copy, remaining_count);

      pthread_mutex_unlock(&g_client_manager_mutex);
      return 0;
    }
  }

  pthread_mutex_unlock(&g_client_manager_mutex);
  // During shutdown, clients may be removed multiple times - don't spam error logs
  if (!g_should_exit) {
    log_error("Client %u not found for removal", client_id);
  } else {
    log_debug("Client %u: already removed during shutdown", client_id);
  }
  return -1;
}
