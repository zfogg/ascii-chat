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
#include <math.h>

#include "image.h"
#include "ascii.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "ringbuffer.h"
#include "compression.h"
#include "audio.h"

/* ============================================================================
 * Global State
 * ============================================================================
 */

static volatile bool g_should_exit = false;
static pthread_t g_audio_thread;
static bool g_audio_thread_created = false;
static volatile bool g_audio_send_failed = false;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_socket_mutex = PTHREAD_MUTEX_INITIALIZER;
static audio_context_t g_audio_context = {0};

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
} client_info_t;

typedef struct {
  client_info_t clients[MAX_CLIENTS];
  int client_count;
  pthread_mutex_t mutex;
  uint32_t next_client_id; // NEW: For assigning unique IDs
} client_manager_t;

// Global multi-client state
static client_manager_t g_client_manager = {0};
static pthread_mutex_t g_client_manager_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Audio Mixing System
 * ============================================================================
 */

typedef struct {
  float *mix_buffer; // Temporary mixing buffer
  size_t mix_buffer_size;
  int active_input_count; // Number of clients sending audio
  pthread_mutex_t mix_mutex;
  pthread_t audio_mixer_thread;
  bool mixer_thread_created;
  audio_ring_buffer_t *mixed_audio_buffer; // Output buffer for mixed audio
} audio_mixer_t;

// Global audio mixer
static audio_mixer_t g_audio_mixer = {0};

// Video mixing system (inline mixing, no global buffers needed)

static server_stats_t g_stats = {0};

static int listenfd = 0;
static int connfd = 0;

/* ============================================================================
 * Multi-Client Function Declarations
 * ============================================================================
 */

// Thread functions
void *client_receive_thread_func(void *arg);

// Audio mixing functions
void *audio_mixer_thread_func(void *arg);
int audio_mixer_init(audio_mixer_t *mixer);
void audio_mixer_destroy(audio_mixer_t *mixer);
int mix_audio_from_clients(float *output_buffer, int num_samples);

// Video mixing functions
char *create_mixed_ascii_frame(unsigned short width, unsigned short height, bool wants_color, bool wants_stretch,
                               size_t *out_size);

// Client management functions
int add_client(int socket, const char *client_ip, int port);
int remove_client(uint32_t client_id);

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
  log_info("Server shutdown requested");

  // Close listening socket to interrupt accept()
  if (listenfd > 0) {
    close(listenfd);
  }

  // Close client socket to interrupt send/recv operations
  if (connfd > 0) {
    close(connfd);
  }
}

/* ============================================================================
 * No server capture thread - clients send their video
 * ============================================================================
 */

/* ============================================================================
 * Audio Thread
 * ============================================================================
 */

static void *audio_thread_func(void *arg) {
  (void)arg;

  log_info("Audio thread started");

  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];

  while (!g_should_exit && opt_audio_enabled) {
    if (connfd == 0) {
      usleep(10 * 1000);
      continue;
    }

    int samples_read = audio_read_samples(&g_audio_context, audio_buffer, AUDIO_SAMPLES_PER_PACKET);
    if (samples_read > 0) {
      pthread_mutex_lock(&g_socket_mutex);
      if (send_audio_packet(connfd, audio_buffer, samples_read) < 0) {
        pthread_mutex_unlock(&g_socket_mutex);
        log_error("Failed to send audio packet");
        g_audio_send_failed = true;
        break; // Exit on network error
      }
      pthread_mutex_unlock(&g_socket_mutex);
#ifdef AUDIO_DEBUG
      log_debug("Sent %d audio samples", samples_read);
#endif
    } else {
      // Only sleep if no audio available to reduce latency
      usleep(2 * 1000); // 2ms instead of 10ms when no audio
    }

    // Quick shutdown check without additional delay
    if (g_should_exit)
      break;
  }

  log_info("Audio thread stopped");
  return NULL;
}

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
    free(data);
    return 0; // Invalid size packet
  }

  const uint16_t *size_data = (const uint16_t *)data;
  *width = ntohs(size_data[0]);
  *height = ntohs(size_data[1]);

  free(data);
  return 1; // Successfully parsed size packet
}

void update_frame_buffer_for_size(unsigned short width, unsigned short height) {
  // Update global dimensions
  opt_width = width;
  opt_height = height;
  // Mark these dimensions as "auto" so aspect_ratio() will treat them as limits
  auto_width = 1;
  auto_height = 1;

  // DON'T clear the framebuffer when just updating size!
  // This causes use-after-free bugs when frames are in flight.
  // The capture thread will automatically start generating frames
  // with the new size, and old frames will naturally drain out.
  log_info("Updated frame size to: %ux%u (buffer not cleared)", width, height);
}

/* ============================================================================
 * Audio Mixing Implementation
 * ============================================================================
 */

// Initialize the audio mixer
int audio_mixer_init(audio_mixer_t *mixer) {
  if (!mixer)
    return -1;

  mixer->mix_buffer_size = AUDIO_FRAMES_PER_BUFFER * sizeof(float);
  SAFE_MALLOC(mixer->mix_buffer, mixer->mix_buffer_size, float *);
  mixer->active_input_count = 0;
  if (pthread_mutex_init(&mixer->mix_mutex, NULL) != 0) {
    log_error("Failed to initialize mixer mutex");
    free(mixer->mix_buffer);
    return -1;
  }

  mixer->mixed_audio_buffer = audio_ring_buffer_create();
  if (!mixer->mixed_audio_buffer) {
    log_error("Failed to create mixed audio ring buffer");
    free(mixer->mix_buffer);
    return -1;
  }

  mixer->active_input_count = 0;
  pthread_mutex_init(&mixer->mix_mutex, NULL);
  mixer->mixer_thread_created = false;

  log_info("Audio mixer initialized");
  return 0;
}

// Destroy the audio mixer
void audio_mixer_destroy(audio_mixer_t *mixer) {
  if (!mixer)
    return;

  if (mixer->mixer_thread_created) {
    pthread_join(mixer->audio_mixer_thread, NULL);
    mixer->mixer_thread_created = false;
  }

  pthread_mutex_destroy(&mixer->mix_mutex);

  if (mixer->mix_buffer) {
    free(mixer->mix_buffer);
    mixer->mix_buffer = NULL;
  }

  if (mixer->mixed_audio_buffer) {
    audio_ring_buffer_destroy(mixer->mixed_audio_buffer);
    mixer->mixed_audio_buffer = NULL;
  }

  log_info("Audio mixer destroyed");
}

// Mix audio from all active clients
int mix_audio_from_clients(float *output_buffer, int num_samples) {
  if (!output_buffer || num_samples <= 0)
    return -1;

  // Clear output buffer
  memset(output_buffer, 0, num_samples * sizeof(float));

  pthread_mutex_lock(&g_client_manager_mutex);
  int mixed_clients = 0;

  // Mix audio from each client that has audio to contribute
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    if (!client->active || !client->is_sending_audio || !client->incoming_audio_buffer) {
      continue;
    }

    // Read samples from this client's audio buffer
    float client_buffer[num_samples];
    int samples_read = audio_ring_buffer_read(client->incoming_audio_buffer, client_buffer, num_samples);

    if (samples_read > 0) {
      // Mix this client's audio into the output
      for (int j = 0; j < samples_read; j++) {
        output_buffer[j] += client_buffer[j];
      }
      mixed_clients++;
    }
  }

  pthread_mutex_unlock(&g_client_manager_mutex);

  // Normalize if we mixed multiple clients to prevent clipping
  if (mixed_clients > 1) {
    float scale = 1.0f / mixed_clients;
    for (int i = 0; i < num_samples; i++) {
      output_buffer[i] *= scale;
    }
  }

  return mixed_clients;
}

// Audio mixing thread function
void *audio_mixer_thread_func(void *arg) {
  (void)arg;
  log_info("Audio mixer thread started");

  float mix_buffer[AUDIO_FRAMES_PER_BUFFER];

  while (!g_should_exit) {
    // Mix audio from all clients
    int clients_mixed = mix_audio_from_clients(mix_buffer, AUDIO_FRAMES_PER_BUFFER);

    if (clients_mixed > 0) {
      // Store mixed audio in the mixer's output buffer
      pthread_mutex_lock(&g_audio_mixer.mix_mutex);
      if (g_audio_mixer.mixed_audio_buffer) {
        int written = audio_ring_buffer_write(g_audio_mixer.mixed_audio_buffer, mix_buffer, AUDIO_FRAMES_PER_BUFFER);
        if (written < AUDIO_FRAMES_PER_BUFFER) {
          log_debug("Mixed audio buffer full, dropped %d samples", AUDIO_FRAMES_PER_BUFFER - written);
        }
      }
      pthread_mutex_unlock(&g_audio_mixer.mix_mutex);

      // Send mixed audio to all connected clients
      pthread_mutex_lock(&g_client_manager_mutex);
      for (int i = 0; i < MAX_CLIENTS; i++) {
        client_info_t *client = &g_client_manager.clients[i];
        if (client->active) {
          int sent = send_audio_packet(client->socket, mix_buffer, AUDIO_FRAMES_PER_BUFFER);
          if (sent < 0) {
            log_debug("Failed to send mixed audio to client %u", client->client_id);
          }
        }
      }
      pthread_mutex_unlock(&g_client_manager_mutex);
    }

    // Audio mixing rate - ~100 FPS for low latency
    usleep(10000); // 10ms
  }

  log_info("Audio mixer thread stopped");
  return NULL;
}

/* ============================================================================
 * Aspect Ratio Helpers
 * ============================================================================
 */

// Calculate the best dimensions to fit an image in a terminal area while preserving aspect ratio
// Returns the dimensions in characters/pixels (1:1 for our use case with stretch=false)
static void calculate_fit_dimensions(int img_width, int img_height, int max_width, int max_height, int *out_width,
                                     int *out_height) {
  if (!out_width || !out_height || img_width <= 0 || img_height <= 0) {
    if (out_width)
      *out_width = max_width;
    if (out_height)
      *out_height = max_height;
    return;
  }

  float src_aspect = (float)img_width / (float)img_height;

  // Try filling width
  int width_if_fill_w = max_width;
  int height_if_fill_w = (int)((float)max_width / src_aspect + 0.5f);

  // Try filling height
  int width_if_fill_h = (int)((float)max_height * src_aspect + 0.5f);
  int height_if_fill_h = max_height;

  log_debug("calculate_fit_dimensions: img %dx%d (aspect %.3f), max %dx%d", img_width, img_height, src_aspect,
            max_width, max_height);
  log_debug("  Fill width: %dx%d, Fill height: %dx%d", width_if_fill_w, height_if_fill_w, width_if_fill_h,
            height_if_fill_h);

  // Choose the option that fits
  if (height_if_fill_w <= max_height) {
    // Filling width fits
    log_debug("  Choosing fill width: %dx%d", width_if_fill_w, height_if_fill_w);
    *out_width = width_if_fill_w;
    *out_height = height_if_fill_w;
  } else {
    // Fill height instead
    log_debug("  Choosing fill height: %dx%d", width_if_fill_h, height_if_fill_h);
    *out_width = width_if_fill_h;
    *out_height = height_if_fill_h;
  }

  // Clamp to bounds
  if (*out_width > max_width)
    *out_width = max_width;
  if (*out_height > max_height)
    *out_height = max_height;
  if (*out_width < 1)
    *out_width = 1;
  if (*out_height < 1)
    *out_height = 1;

  log_debug("  Final output: %dx%d", *out_width, *out_height);
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

  // Frame rate control (30 FPS)
  const int target_fps = 30;
  const int frame_interval_ms = 1000 / target_fps;
  struct timespec last_broadcast_time;
  clock_gettime(CLOCK_MONOTONIC, &last_broadcast_time);

  // Track the number of active clients for console clear detection
  int last_active_client_count = 0;
  int last_connected_count = 0;

  while (!g_should_exit) {
    // Rate limiting
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_broadcast_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_broadcast_time.tv_nsec) / 1000000;

    if (elapsed_ms < frame_interval_ms) {
      usleep((frame_interval_ms - elapsed_ms) * 1000);
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
      if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket > 0 &&
          g_client_manager.clients[i].width > 0 && g_client_manager.clients[i].height > 0) {
        active_client_count++;
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    if (client_count == 0) {
      // log_debug("Broadcast thread: No clients connected");
      usleep(100000); // 100ms sleep when no clients
      last_active_client_count = 0;
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

      // Send server state update to all clients
      pthread_mutex_lock(&g_client_manager_mutex);
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket > 0) {
          if (send_server_state_packet(g_client_manager.clients[i].socket, &state) < 0) {
            log_warn("Failed to send server state to client %u", g_client_manager.clients[i].client_id);
          }
        }
      }
      pthread_mutex_unlock(&g_client_manager_mutex);

      // Update the tracked count
      last_connected_count = client_count;
    }

    // Update the tracked active count
    last_active_client_count = active_client_count;

    static int frame_counter = 0;
    frame_counter++;
    if (frame_counter % 30 == 0) { // Log every 30 frames (1 second)
      log_info("Broadcast thread: frame %d, %d clients connected (%d active)", frame_counter, client_count,
               active_client_count);
    }

    // log_debug("Broadcast thread: %d clients connected", client_count);

    // First, consume all old frames and keep only the latest from each client
    // This ensures we always use the most recent frame and prevents desync
    pthread_mutex_lock(&g_client_manager_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      if (client->active && client->incoming_video_buffer) {
        multi_source_frame_t frame;
        multi_source_frame_t latest_frame = {0};
        bool has_frame = false;

        // Consume all frames and keep only the latest
        while (framebuffer_read_multi_frame(client->incoming_video_buffer, &frame)) {
          // Free the previous frame if we had one
          if (has_frame && latest_frame.data) {
            free(latest_frame.data);
          }
          latest_frame = frame;
          has_frame = true;
        }

        // If we got a frame, write it back so peek can see it
        if (has_frame && latest_frame.data) {
          // Write the latest frame back to the buffer for peeking
          framebuffer_write_multi_frame(client->incoming_video_buffer, latest_frame.data, latest_frame.size,
                                        latest_frame.source_client_id, latest_frame.frame_sequence,
                                        latest_frame.timestamp);
          // Now free our copy
          free(latest_frame.data);
        }
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    // Create and send frames to each client based on their dimensions
    // NOTE: We DON'T lock mutex here because create_mixed_ascii_frame will lock it
    int sent_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      // Get client info with mutex lock
      pthread_mutex_lock(&g_client_manager_mutex);
      client_info_t client_copy = g_client_manager.clients[i];
      pthread_mutex_unlock(&g_client_manager_mutex);

      // Skip if client hasn't finished initialization
      if (client_copy.width == 0 || client_copy.height == 0) {
        if (client_copy.active) {
          static int skip_count[MAX_CLIENTS] = {0};
          skip_count[i]++;
          if (skip_count[i] == 1 || skip_count[i] % 30 == 0) {
            log_warn("Skipping client %u (slot %d) - dimensions not set (w=%u, h=%u) - skipped %d times",
                     client_copy.client_id, i, client_copy.width, client_copy.height, skip_count[i]);
          }
        }
        continue;
      }

      // Add debug logging to track what's happening with second client
      if (client_copy.active) {
        static int client_frame_count[MAX_CLIENTS] = {0};
        client_frame_count[i]++;
        if (client_frame_count[i] % 30 == 0) { // Log every 30 frames
          log_info("Broadcasting to client %u (slot %d): socket=%d, width=%u, height=%u, frames_sent=%d",
                   client_copy.client_id, i, client_copy.socket, client_copy.width, client_copy.height,
                   client_frame_count[i]);
        }
      }

      if (client_copy.active && client_copy.socket > 0) {
        // Lock mutex while sending to ensure thread safety
        pthread_mutex_lock(&g_client_manager_mutex);
        // Verify client is still active and socket matches
        if (!g_client_manager.clients[i].active || g_client_manager.clients[i].socket != client_copy.socket) {
          pthread_mutex_unlock(&g_client_manager_mutex);
          log_warn("Client %u state changed during broadcast, skipping", client_copy.client_id);
          continue;
        }
        pthread_mutex_unlock(&g_client_manager_mutex);
        // Use client's dimensions if available, otherwise use defaults
        unsigned short target_width = client_copy.width > 0 ? client_copy.width : 110;
        unsigned short target_height = client_copy.height > 0 ? client_copy.height : 70;

        // Create mixed frame for this client's dimensions
        size_t mixed_size = 0;
        char *mixed_frame = create_mixed_ascii_frame(target_width, target_height, client_copy.wants_color,
                                                     client_copy.wants_stretch, &mixed_size);

        if (mixed_frame && mixed_size > 0) {

          // Send frame with header that includes dimensions
          compressed_frame_header_t header = {.magic = COMPRESSION_FRAME_MAGIC,
                                              .compressed_size = 0, // Not compressed
                                              .original_size = mixed_size,
                                              .checksum = calculate_crc32(mixed_frame, mixed_size),
                                              .width = target_width,
                                              .height = target_height};

          // Send header first
          int header_result = send_video_header_packet(client_copy.socket, &header, sizeof(header));
          if (header_result < 0) {
            log_error("Failed to send header to client %u (socket=%d): %s", client_copy.client_id, client_copy.socket,
                      strerror(errno));
          } else {
            // Then send the frame data
            int frame_result = send_video_packet(client_copy.socket, mixed_frame, mixed_size);
            if (frame_result < 0) {
              log_error("Failed to send video frame to client %u (socket=%d): %s", client_copy.client_id,
                        client_copy.socket, strerror(errno));
              // Mark client as inactive if we can't send to it
              pthread_mutex_lock(&g_client_manager_mutex);
              if (g_client_manager.clients[i].client_id == client_copy.client_id) {
                g_client_manager.clients[i].active = false;
              }
              pthread_mutex_unlock(&g_client_manager_mutex);
            } else {
              sent_count++;
              static int success_count[MAX_CLIENTS] = {0};
              success_count[i]++;
              if (success_count[i] == 1 || success_count[i] % 30 == 0) { // Log first and every 30 successful sends
                log_info("Successfully sent %d frames to client %u (slot %d, socket=%d, size=%zu)", success_count[i],
                         client_copy.client_id, i, client_copy.socket, mixed_size);
              }
            }
          }

          free(mixed_frame);
        } else {
          log_warn("No mixed frame created for client %u (frame=%p, size=%zu)", client_copy.client_id, mixed_frame,
                   mixed_size);
        }
      }
    }

    if (sent_count > 0) {
      // log_debug("Sent frames to %d clients", sent_count);
    }

    // Frames are already consumed at the beginning of the cycle
    // No need to consume them again here

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
  int active_client_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active) {
      active_client_count++;
    }
  }
  static int mix_frame_count = 0;
  mix_frame_count++;
  if (mix_frame_count % 30 == 0) { // Log every 30 frames
    log_info("create_mixed_ascii_frame #%d: Creating %ux%u frame (%d active clients)", mix_frame_count, width, height,
             active_client_count);
  }

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->active) {
      static int log_count = 0;
      log_count++;
      if (log_count <= 10 || log_count % 100 == 0) { // Log first 10 then every 100
        log_info("  Client slot %d: id=%u, sending_video=%d, has_buffer=%d, width=%u, height=%u", i, client->client_id,
                 client->is_sending_video, (client->incoming_video_buffer != NULL), client->width, client->height);
      }
    }

    if (client->active && client->is_sending_video && client->incoming_video_buffer && source_count < MAX_CLIENTS) {
      // Peek at the latest frame without consuming it
      multi_source_frame_t latest_frame = {0};
      bool got_frame = framebuffer_peek_latest_multi_frame(client->incoming_video_buffer, &latest_frame);

      log_debug("Client %u: peek attempt, got_frame=%d, data=%p, size=%zu", client->client_id, got_frame,
                latest_frame.data, latest_frame.size);

      if (got_frame && latest_frame.data && latest_frame.size > sizeof(uint32_t) * 2) {
        // Parse the image data
        // Format: [width:4][height:4][rgb_data:w*h*3]
        uint32_t img_width = ntohl(*(uint32_t *)latest_frame.data);
        uint32_t img_height = ntohl(*(uint32_t *)(latest_frame.data + sizeof(uint32_t)));
        rgb_t *pixels = (rgb_t *)(latest_frame.data + sizeof(uint32_t) * 2);

        log_info("[SERVER COMPOSITE] Got image from client %u: %ux%u, aspect: %.3f (needs terminal adjustment)",
                 client->client_id, img_width, img_height, (float)img_width / (float)img_height);

        // Create an image_t structure
        image_t *img = image_new(img_width, img_height);
        if (img) {
          memcpy(img->pixels, pixels, img_width * img_height * sizeof(rgb_t));
          sources[source_count].image = img;
          sources[source_count].client_id = client->client_id;
          source_count++;
          log_debug("Got image from client %u: %ux%u", client->client_id, img_width, img_height);
        }

        // Free the frame data
        free(latest_frame.data);
      }
    }
  }
  pthread_mutex_unlock(&g_client_manager_mutex);

  // If no image sources, return empty frame
  if (source_count == 0) {
    log_debug("No image sources available for mixing (%d active clients, 0 sending video)", active_client_count);
    *out_size = 0;
    return NULL;
  }

  if (mix_frame_count % 30 == 0) { // Log every 30 frames
    log_info("Mixing %d image sources for %ux%u output (frame #%d)", source_count, width, height, mix_frame_count);
  }

  // Create composite image for multiple sources with grid layout
  image_t *composite = NULL;

  // Initialize to prevent old frame data from appearing
  // This is critical for preventing the "old frames" issue

  if (source_count == 1 && sources[0].image) {
    // Single source - calculate proper dimensions for display

    // Calculate source aspect ratio
    float src_aspect = (float)sources[0].image->w / (float)sources[0].image->h;

    log_info("Single source: img %dx%d (aspect %.3f), terminal %dx%d chars", sources[0].image->w, sources[0].image->h,
             src_aspect, width, height);

    // Use our helper function to calculate the best fit
    int display_width_chars, display_height_chars;
    calculate_fit_dimensions(sources[0].image->w, sources[0].image->h, width, height, &display_width_chars,
                             &display_height_chars);

    log_info("[SERVER ASPECT] Best fit for single source: %dx%d chars", display_width_chars, display_height_chars);

    // Create composite at exactly the display size in pixels
    // Since stretch=false, ascii_convert won't resize, so composite = output
    composite = image_new(display_width_chars, display_height_chars);
    if (!composite) {
      log_error("Failed to create composite image");
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        if (sources[i].image) {
          image_destroy(sources[i].image);
        }
      }
      return NULL;
    }

    image_clear(composite);

    log_info("[SERVER ASPECT] Created composite: %dx%d pixels for %dx%d char display", display_width_chars,
             display_height_chars, display_width_chars, display_height_chars);

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
        if (sources[i].image) {
          image_destroy(sources[i].image);
        }
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

    log_info("[GRID] Creating %dx%d grid for %d sources on %dx%d terminal", grid_cols, grid_rows, source_count, width,
             height);

    // Calculate cell dimensions in characters
    int cell_width = width / grid_cols;
    int cell_height = height / grid_rows;

    log_info("[GRID] Each cell is %dx%d chars (terminal %dx%d / grid %dx%d)", cell_width, cell_height, width, height,
             grid_cols, grid_rows);

    // Place each source in the grid
    for (int i = 0; i < source_count && i < 9; i++) { // Max 9 sources in 3x3 grid
      if (!sources[i].image)
        continue;

      int row = i / grid_cols;
      int col = i % grid_cols;
      int cell_x_offset = col * cell_width;
      int cell_y_offset = row * cell_height;

      float src_aspect = (float)sources[i].image->w / (float)sources[i].image->h;
      log_info("[GRID CELL %d] Source: %dx%d (aspect %.3f), Cell: %dx%d chars at (%d,%d)", i, sources[i].image->w,
               sources[i].image->h, src_aspect, cell_width, cell_height, cell_x_offset, cell_y_offset);

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

      log_info("[GRID CELL %d] Best fit in cell: %dx%d pixels (from %dx%d image, cell %dx%d chars = %dx%d px)", i,
               target_width_px, target_height_px, sources[i].image->w, sources[i].image->h, cell_width, cell_height,
               cell_width_px, cell_height_px);

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

        log_debug("Cell %d: Centering %dx%d px (%dx%d chars) in %dx%d cell, padding: %d,%d", i, target_width_px,
                  target_height_px, target_width_chars, target_height_chars, cell_width, cell_height, x_padding,
                  y_padding);

        // Copy resized image to composite (now in pixel space)
        for (int y = 0; y < target_height_px; y++) {
          for (int x = 0; x < target_width_px; x++) {
            int src_idx = y * target_width_px + x;
            // Convert character offsets to pixel offsets
            // x_padding and cell_x_offset are in characters, keep them as-is for X
            // For Y, we need to convert character offsets to pixel offsets (multiply by 2)
            int dst_x = cell_x_offset + x_padding + x;
            int dst_y = (cell_y_offset + y_padding) * 2 + y;  // Convert char offset to pixel offset
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

    log_debug("Created grid layout: %dx%d grid for %d sources", grid_cols, grid_rows, source_count);
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

  // Pass the terminal dimensions to ascii_convert
  // The composite is already sized correctly in pixels (width*2, height)
  // ascii_convert expects character dimensions, so we pass the original width and height
  // Pass stretch=false because we've already sized the composite to the exact dimensions we want
  log_info("[SERVER ASPECT] ascii_convert(composite, %d, %d, %d, true, false)", width, height, wants_color);
  char *ascii_frame = ascii_convert(composite, width, height, wants_color, true, false);

  if (ascii_frame) {
    *out_size = strlen(ascii_frame);

    // Analyze the actual ASCII frame dimensions
    int actual_width = 0;
    int actual_height = 0;
    int current_line_width = 0;
    bool in_escape = false;

    for (const char *p = ascii_frame; *p; p++) {
      if (*p == '\033') {
        in_escape = true;
      } else if (in_escape && *p == 'm') {
        in_escape = false;
      } else if (!in_escape) {
        if (*p == '\n') {
          if (current_line_width > actual_width) {
            actual_width = current_line_width;
          }
          current_line_width = 0;
          actual_height++;
        } else if (*p != '\r') {
          current_line_width++;
        }
      }
    }

    log_info("[SERVER FRAME ANALYSIS] Requested: %dx%d, Composite: %dx%d pixels, ASCII: %dx%d chars, "
             "Fill: %.1f%% width, %.1f%% height, Sources: %d",
             width, height, composite->w, composite->h, actual_width, actual_height, (float)actual_width / width * 100,
             (float)actual_height / height * 100, source_count);
  } else {
    log_error("Failed to convert image to ASCII");
    *out_size = 0;
  }

  // Clean up
  // We always create a new composite image regardless of source count
  if (composite) {
    image_destroy(composite);
  }

  // Always destroy source images as we created them
  for (int i = 0; i < source_count; i++) {
    if (sources[i].image) {
      image_destroy(sources[i].image);
    }
  }

  return ascii_frame;
}

/* ============================================================================
 * Main Server Logic
 * ============================================================================
 */

int main(int argc, char *argv[]) {
  log_init("server.log", LOG_DEBUG);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat server starting...");

  options_init(argc, argv);
  int port = strtoint(opt_port);

  precalc_luminance_palette();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);

  // Handle terminal resize events
  signal(SIGWINCH, sigwinch_handler);
  // Handle Ctrl+C for cleanup
  signal(SIGINT, sigint_handler);
  // Ignore SIGPIPE
  signal(SIGPIPE, SIG_IGN);

  // Server no longer captures webcam - clients send their video
  // Frame buffer removed - using client buffers instead

  // Initialize audio if enabled
  if (opt_audio_enabled) {
    if (audio_init(&g_audio_context) != 0) {
      log_fatal("Failed to initialize audio system");
      exit(ASCIICHAT_ERR_AUDIO);
    }

    if (audio_start_capture(&g_audio_context) != 0) {
      log_error("Failed to start audio capture");
      audio_destroy(&g_audio_context);
      exit(ASCIICHAT_ERR_AUDIO);
    }

    log_info("Audio system initialized and capture started");
  }

  // No capture thread needed - clients send their video

  // Start audio thread if enabled
  if (opt_audio_enabled) {
    if (pthread_create(&g_audio_thread, NULL, audio_thread_func, NULL) != 0) {
      log_fatal("Failed to create audio thread");
      audio_destroy(&g_audio_context);
      exit(ASCIICHAT_ERR_THREAD);
    }
    g_audio_thread_created = true;

    // Initialize audio mixer for multi-user audio mixing
    if (audio_mixer_init(&g_audio_mixer) != 0) {
      log_error("Failed to initialize audio mixer");
    } else {
      // Start audio mixer thread
      if (pthread_create(&g_audio_mixer.audio_mixer_thread, NULL, audio_mixer_thread_func, NULL) != 0) {
        log_error("Failed to create audio mixer thread");
        audio_mixer_destroy(&g_audio_mixer);
      } else {
        g_audio_mixer.mixer_thread_created = true;
        log_info("Audio mixer initialized and thread started");
      }
    }
  }

  // Start video broadcast thread for mixing and sending frames to all clients
  if (pthread_create(&g_video_broadcast_thread, NULL, video_broadcast_thread_func, NULL) != 0) {
    log_error("Failed to create video broadcast thread");
  } else {
    log_info("Video broadcast thread started");
  }

  // Network setup
  struct sockaddr_in serv_addr;
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    log_fatal("Failed to create socket: %s", strerror(errno));
    exit(1);
  }

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

  // Main multi-client connection loop
  while (!g_should_exit) {
    log_info("Waiting for client connections... (%d/%d clients)", g_client_manager.client_count, MAX_CLIENTS);

    // Check for disconnected clients BEFORE accepting new ones
    // This ensures slots are freed up for new connections
    pthread_mutex_lock(&g_client_manager_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
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
        i = -1; // Will be incremented to 0 at loop continuation
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);

    // Accept network connection with timeout
    connfd = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    if (connfd < 0) {
      if (errno == ETIMEDOUT) {
        // Timeout is normal, just continue
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
    int client_id = add_client(connfd, client_ip, client_port);
    if (client_id < 0) {
      log_error("Failed to add client, rejecting connection");
      close(connfd);
      continue;
    }

    log_info("Client %d added successfully, total clients: %d", client_id, g_client_manager.client_count);

    // Don't clear framebuffer here - it will be cleared when next client connects
    // This avoids race conditions with any frames that might still be in use
  }

  // Cleanup
  log_info("Server shutting down...");
  g_should_exit = true;

  // Wait for video broadcast thread to finish
  if (g_video_broadcast_running) {
    log_info("Waiting for video broadcast thread to finish...");
    pthread_join(g_video_broadcast_thread, NULL);
    log_info("Video broadcast thread stopped");
  }

  // Wait for audio thread to finish if enabled
  if (opt_audio_enabled && g_audio_thread_created) {
    log_info("Waiting for audio thread to finish...");

    // Stop audio capture to help thread exit
    audio_stop_capture(&g_audio_context);

    // Wait for thread to exit
    pthread_join(g_audio_thread, NULL);

    audio_destroy(&g_audio_context);
    log_info("Audio thread joined and context destroyed");

    // Cleanup audio mixer
    audio_mixer_destroy(&g_audio_mixer);
  }

  // Cleanup resources
  // No server framebuffer or webcam to clean up
  close(listenfd);

  // Final statistics
  pthread_mutex_lock(&g_stats_mutex);
  log_info("Final stats: captured=%lu, sent=%lu, dropped=%lu", g_stats.frames_captured, g_stats.frames_sent,
           g_stats.frames_dropped);
  pthread_mutex_unlock(&g_stats_mutex);

  printf("Server shutdown complete.\n");

  // Destroy mutexes (do this before log_destroy in case logging uses them)
  pthread_mutex_destroy(&g_stats_mutex);
  pthread_mutex_destroy(&g_socket_mutex);

  log_destroy();
  return 0;
}

/* ============================================================================
 * Multi-Client Thread Functions
 * ============================================================================
 */

// Thread function to handle incoming data from a specific client
void *client_receive_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in receive thread");
    return NULL;
  }

  log_info("Started receive thread for client %u (%s)", client->client_id, client->display_name);

  packet_type_t type;
  uint32_t sender_id;
  void *data;
  size_t len;

  while (!g_should_exit && client->active) {
    // Receive packet from this client
    int result = receive_packet_with_client(client->socket, &type, &sender_id, &data, &len);

    if (result <= 0) {
      if (result == 0) {
        log_info("Client %u disconnected", client->client_id);
      } else {
        log_error("Error receiving from client %u: %s", client->client_id, strerror(errno));
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
        strncpy(client->display_name, join_info->display_name, MAX_DISPLAY_NAME_LEN - 1);
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

    case PACKET_TYPE_VIDEO: {
      // Handle incoming image data from client
      // Format: [width:4][height:4][rgb_data:w*h*3]
      if (client->is_sending_video && data && len > sizeof(uint32_t) * 2) {
        // Parse image dimensions
        uint32_t img_width = ntohl(*(uint32_t *)data);
        uint32_t img_height = ntohl(*(uint32_t *)(data + sizeof(uint32_t)));
        size_t expected_size = sizeof(uint32_t) * 2 + img_width * img_height * sizeof(rgb_t);

        log_info("[SERVER RECEIVE] Client %u sent frame: %ux%u, aspect: %.3f (original aspect)", client->client_id,
                 img_width, img_height, (float)img_width / (float)img_height);

        if (len != expected_size) {
          log_error("Invalid image packet from client %u: expected %zu bytes, got %zu", client->client_id,
                    expected_size, len);
          break;
        }

        // log_debug("Received image from client %u: %ux%u", client->client_id, img_width, img_height);

        // Store the entire packet (including dimensions) in the buffer
        // The mixing function will parse it
        uint32_t timestamp = (uint32_t)time(NULL);
        if (client->incoming_video_buffer) {
          bool stored = framebuffer_write_multi_frame(client->incoming_video_buffer, (const char *)data, len,
                                                      client->client_id, 0, timestamp);
          if (stored) {
            client->frames_received++;
            // log_debug("Stored image from client %u (size=%zu, total=%llu)", client->client_id, len,
            //           client->frames_received);
          } else {
            log_warn("Failed to store image from client %u (buffer full?)", client->client_id);
          }
        } else {
          log_error("Client %u has no incoming video buffer!", client->client_id);
        }
      } else {
        log_debug("Ignoring video packet: is_sending=%d, len=%zu", client->is_sending_video, len);
      }
      break;
    }

    case PACKET_TYPE_AUDIO: {
      // Handle incoming audio samples from client
      if (client->is_sending_audio && data && len > 0) {
        // Convert data to float samples
        int num_samples = len / sizeof(float);
        if (num_samples > 0 && client->incoming_audio_buffer) {
          const float *samples = (const float *)data;
          int written = audio_ring_buffer_write(client->incoming_audio_buffer, samples, num_samples);
          if (written < num_samples) {
            log_debug("Client %u audio buffer full, dropped %d samples", client->client_id, num_samples - written);
          } else {
            log_debug("Stored %d audio samples from client %u", num_samples, client->client_id);
          }
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
        log_info("Client %u updated size to %ux%u", client->client_id, client->width, client->height);
      }
      break;
    }

    case PACKET_TYPE_PING: {
      // Handle ping from client - send pong back
      if (send_pong_packet(client->socket) < 0) {
        log_debug("Failed to send PONG response to client %u", client->client_id);
      } else {
        log_debug("Sent PONG response to client %u", client->client_id);
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
      free(data);
      data = NULL;
    }
  }

  // Mark client as inactive so main thread can clean it up
  // Do NOT call remove_client here - it causes race conditions and double-frees
  // Do NOT close the socket here - let the main thread detect it and clean up
  client->active = false;
  log_info("Receive thread for client %u terminated", client->client_id);
  return NULL;
}

// Client management functions
int add_client(int socket, const char *client_ip, int port) {
  pthread_mutex_lock(&g_client_manager_mutex);

  if (g_client_manager.client_count >= MAX_CLIENTS) {
    pthread_mutex_unlock(&g_client_manager_mutex);
    log_error("Maximum client limit reached (%d)", MAX_CLIENTS);
    return -1;
  }

  // Find empty slot
  int slot = -1;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!g_client_manager.clients[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    pthread_mutex_unlock(&g_client_manager_mutex);
    log_error("No available client slots");
    return -1;
  }

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];
  memset(client, 0, sizeof(client_info_t));

  client->socket = socket;
  client->client_id = ++g_client_manager.next_client_id;
  strncpy(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  client->active = true;
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

  g_client_manager.client_count++;
  pthread_mutex_unlock(&g_client_manager_mutex);

  // Start threads for this client
  if (pthread_create(&client->receive_thread, NULL, client_receive_thread_func, client) != 0) {
    log_error("Failed to create receive thread for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  // No send thread needed - broadcast thread handles all sending
  // This avoids race conditions and simplifies synchronization
  log_info("Client %u initialized (using broadcast thread for sending)", client->client_id);

  // Send initial server state to the new client
  server_state_packet_t state;
  state.connected_client_count = g_client_manager.client_count;
  state.active_client_count = 0; // Will be updated by broadcast thread
  memset(state.reserved, 0, sizeof(state.reserved));

  if (send_server_state_packet(client->socket, &state) < 0) {
    log_warn("Failed to send initial server state to client %u", client->client_id);
  } else {
    log_info("Sent initial server state to client %u: %u connected clients", client->client_id,
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

      g_client_manager.client_count--;

      log_info("Removed client %u (%s)", client_id, client->display_name);

      // Clear the entire client structure to ensure it's ready for reuse
      memset(client, 0, sizeof(client_info_t));

      pthread_mutex_unlock(&g_client_manager_mutex);
      return 0;
    }
  }

  pthread_mutex_unlock(&g_client_manager_mutex);
  log_error("Client %u not found for removal", client_id);
  return -1;
}
