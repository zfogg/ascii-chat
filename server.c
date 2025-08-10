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
#include "ringbuffer.h"
#include "audio.h"
#include "aspect_ratio.h"
#include "mixer.h"

/* ============================================================================
 * Global State
 * ============================================================================
 */

static volatile bool g_should_exit = false;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

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

  // Packet queues for outgoing data (per-client queues for isolation)
  packet_queue_t *audio_queue; // Queue for audio packets to send to this client
  packet_queue_t *video_queue; // Queue for video packets to send to this client

  // Dedicated send thread for this client
  pthread_t send_thread;
  bool send_thread_running;
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

// Global audio mixer using new advanced mixer system
static mixer_t *g_audio_mixer = NULL;
static pthread_t g_audio_mixer_thread;
static bool g_audio_mixer_thread_created = false;

// Video mixing system (inline mixing, no global buffers needed)

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
    free(data);
    return 0; // Invalid size packet
  }

  const uint16_t *size_data = (const uint16_t *)data;
  *width = ntohs(size_data[0]);
  *height = ntohs(size_data[1]);

  free(data);
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
  log_info("Audio mixer thread started (using advanced mixer with ducking and compression)");

  float mix_buffer[AUDIO_FRAMES_PER_BUFFER];
  float send_buffer[AUDIO_FRAMES_PER_BUFFER]; // Separate buffer for sending

  while (!g_should_exit) {
    if (!g_audio_mixer) {
      usleep(10000); // 10ms - wait for mixer initialization
      continue;
    }

    // Use the new mixer to process audio from all clients
    // The mixer handles ducking, compression, and crowd scaling automatically
    int samples_mixed = mixer_process(g_audio_mixer, mix_buffer, AUDIO_FRAMES_PER_BUFFER);

    if (samples_mixed > 0) {
      // Copy to send buffer to avoid race conditions
      memcpy(send_buffer, mix_buffer, AUDIO_FRAMES_PER_BUFFER * sizeof(float));

      // Debug: Calculate CRC before sending and check for DEADBEEF
      uint32_t *check_magic = (uint32_t *)send_buffer;
      if (*check_magic == 0xDEADBEEF || *check_magic == 0xEFBEADDE) {
        log_error(
            "DEADBEEF found in audio buffer! First 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
            "%02x %02x %02x %02x %02x",
            ((unsigned char *)send_buffer)[0], ((unsigned char *)send_buffer)[1], ((unsigned char *)send_buffer)[2],
            ((unsigned char *)send_buffer)[3], ((unsigned char *)send_buffer)[4], ((unsigned char *)send_buffer)[5],
            ((unsigned char *)send_buffer)[6], ((unsigned char *)send_buffer)[7], ((unsigned char *)send_buffer)[8],
            ((unsigned char *)send_buffer)[9], ((unsigned char *)send_buffer)[10], ((unsigned char *)send_buffer)[11],
            ((unsigned char *)send_buffer)[12], ((unsigned char *)send_buffer)[13], ((unsigned char *)send_buffer)[14],
            ((unsigned char *)send_buffer)[15]);
      }

#ifdef AUDIO_DEBUG
      uint32_t crc_before = asciichat_crc32(send_buffer, AUDIO_FRAMES_PER_BUFFER * sizeof(float));
      log_debug("Sending audio: samples_mixed=%d, CRC=0x%x", samples_mixed, crc_before);
#endif

      // Queue mixed audio to all connected clients
      pthread_mutex_lock(&g_client_manager_mutex);
      for (int i = 0; i < MAX_CLIENTS; i++) {
        client_info_t *client = &g_client_manager.clients[i];
        if (client->active && client->socket > 0 && client->audio_queue) {
          // Queue the audio packet for this client
          // Note: We copy the data so each queue has its own copy
          size_t data_size = AUDIO_FRAMES_PER_BUFFER * sizeof(float);
          int result = packet_queue_enqueue(client->audio_queue, PACKET_TYPE_AUDIO, send_buffer, data_size, 0,
                                            true); // client_id=0 for server-originated, copy=true
          if (result < 0) {
            log_debug("Failed to queue audio for client %u (queue full or shutdown)", client->client_id);
          }
        }
      }
      pthread_mutex_unlock(&g_client_manager_mutex);
    }

    // Audio mixing rate - ~50 FPS for balance between latency and network load
    usleep(20000); // 20ms
  }

  log_info("Audio mixer thread stopped");
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

  // Frame rate control - use a reasonable rate that matches client capabilities
  // 15 FPS gives clients time to send frames and reduces buffer starvation
  const int frame_interval_ms = 1000 / 15;  // 15 FPS instead of 120
  struct timespec last_broadcast_time;
  clock_gettime(CLOCK_MONOTONIC, &last_broadcast_time);

  // Track the number of active clients for console clear detection
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

    // Don't manipulate frames in the broadcast thread
    // The framebuffer already maintains frames and peek will get the latest
    // Removing this entire section prevents race conditions and corruption

    // Collect all active clients' settings
    unsigned short common_width = 110;
    unsigned short common_height = 70;
    bool wants_color = false;
    bool wants_stretch = false;
    
    pthread_mutex_lock(&g_client_manager_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (g_client_manager.clients[i].active && g_client_manager.clients[i].width > 0) {
        common_width = g_client_manager.clients[i].width;
        common_height = g_client_manager.clients[i].height;
        // If ANY client wants color, enable it
        if (g_client_manager.clients[i].wants_color) {
          wants_color = true;
        }
        if (g_client_manager.clients[i].wants_stretch) {
          wants_stretch = true;
        }
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);
    
    // Create ONE mixed frame for all clients
    // The read operations happen inside this function
    size_t mixed_size = 0;
    char *mixed_frame = create_mixed_ascii_frame(common_width, common_height, wants_color, wants_stretch, &mixed_size);
    
    if (!mixed_frame || mixed_size == 0) {
      // No frame available, wait for next cycle
      last_broadcast_time = current_time;
      continue;
    }

    // Now send this frame to all clients
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
        // Lock mutex while sending to ensure thread safety
        pthread_mutex_lock(&g_client_manager_mutex);
        // Verify client is still active and socket matches
        if (!g_client_manager.clients[i].active || g_client_manager.clients[i].socket != client_copy.socket) {
          pthread_mutex_unlock(&g_client_manager_mutex);
          log_warn("Client %u state changed during broadcast, skipping", client_copy.client_id);
          continue;
        }
        pthread_mutex_unlock(&g_client_manager_mutex);

        // Use the common frame for all clients
        // Create unified ASCII frame packet with metadata
        ascii_frame_packet_t frame_header = {.width = htonl(common_width),
                                             .height = htonl(common_height),
                                             .original_size = htonl(mixed_size),
                                             .compressed_size = htonl(0), // Not compressed for now
                                             .checksum = htonl(asciichat_crc32(mixed_frame, mixed_size)),
                                             .flags = htonl(client_copy.wants_color ? FRAME_FLAG_HAS_COLOR : 0)};

        // Allocate buffer for complete packet (header + data)
        size_t packet_size = sizeof(ascii_frame_packet_t) + mixed_size;
        char *packet_buffer = malloc(packet_size);
        if (!packet_buffer) {
          log_error("Failed to allocate packet buffer for client %u", client_copy.client_id);
          continue;
        }

        // Copy header and frame data into single buffer
        memcpy(packet_buffer, &frame_header, sizeof(ascii_frame_packet_t));
        memcpy(packet_buffer + sizeof(ascii_frame_packet_t), mixed_frame, mixed_size);

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
      }
    }

    // Free the mixed frame after sending to all clients
    free(mixed_frame);

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
  int active_client_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active) {
      active_client_count++;
    }
  }

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    
    if (client->active && client->is_sending_video && client->incoming_video_buffer && source_count < MAX_CLIENTS) {
      // Try to peek at the latest frame first
      multi_source_frame_t latest_frame = {0};
      bool got_frame = framebuffer_peek_latest_multi_frame(client->incoming_video_buffer, &latest_frame);
      
      // If we got a frame, consume it to prevent buffer overflow
      if (got_frame) {
        multi_source_frame_t consumed_frame = {0};
        framebuffer_read_multi_frame(client->incoming_video_buffer, &consumed_frame);
        // Use the peeked frame data, but free the consumed frame if different
        if (consumed_frame.data && consumed_frame.data != latest_frame.data) {
          free(consumed_frame.data);
        }
      }

      if (got_frame && latest_frame.data && latest_frame.size > sizeof(uint32_t) * 2) {
        // Parse the image data
        // Format: [width:4][height:4][rgb_data:w*h*3]
        uint32_t img_width = ntohl(*(uint32_t *)latest_frame.data);
        uint32_t img_height = ntohl(*(uint32_t *)(latest_frame.data + sizeof(uint32_t)));

        // Validate dimensions are reasonable (max 4K resolution)
        if (img_width == 0 || img_width > 4096 || img_height == 0 || img_height > 4096) {
          log_error("Invalid image dimensions from client %u: %ux%u (data may be corrupted)", client->client_id,
                    img_width, img_height);
          // Free the corrupted frame
          free(latest_frame.data);
          continue;
        }

        // Validate that the frame size matches expected size
        size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
        if (latest_frame.size != expected_size) {
          log_error("Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image", client->client_id,
                    latest_frame.size, expected_size, img_width, img_height);
          // Free the corrupted frame
          free(latest_frame.data);
          continue;
        }

        // The image data received from the client is formatted as:
        // [width:4 bytes][height:4 bytes][pixel data...]
        // So, the first 8 bytes (2 * sizeof(uint32_t)) are the width and height (each 4 bytes).
        // The actual pixel data (an array of rgb_t structs) starts immediately after these 8 bytes.
        // By adding sizeof(uint32_t) * 2 to the data pointer, we skip past the width and height fields
        // and point directly to the start of the pixel data.
        rgb_t *pixels = (rgb_t *)(latest_frame.data + sizeof(uint32_t) * 2);

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

        // Free the frame data
        free(latest_frame.data);
      }
    }
  }
  pthread_mutex_unlock(&g_client_manager_mutex);

  // If no image sources, return empty frame
  if (source_count == 0) {
    // Count how many clients are actually marked as sending video
    int sending_video_count = 0;
    pthread_mutex_lock(&g_client_manager_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (g_client_manager.clients[i].active && g_client_manager.clients[i].is_sending_video) {
        sending_video_count++;
      }
    }
    pthread_mutex_unlock(&g_client_manager_mutex);
    
    log_debug("No frames available for mixing (%d active, %d sending video, but 0 frames in buffers)", 
              active_client_count, sending_video_count);
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

  // Initialize audio mixer if audio is enabled
  if (opt_audio_enabled) {
    // Initialize the new advanced audio mixer for multi-user audio mixing
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      log_error("Failed to initialize audio mixer");
    } else {
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

  // Cleanup audio mixer if enabled
  if (opt_audio_enabled) {
    // Cleanup audio mixer
    if (g_audio_mixer) {
      mixer_destroy(g_audio_mixer);
      g_audio_mixer = NULL;
    }
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

    case PACKET_TYPE_IMAGE_FRAME: {
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
        log_debug("Ignoring video packet: len=%zu (too small)", len);
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
            // log_debug("Client %u audio buffer full, dropped %d samples", client->client_id, num_samples - written);
          } else {
            (void)0;
            // log_debug("Stored %d audio samples from client %u", num_samples, client->client_id);
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

  while (!g_should_exit && client->active && client->send_thread_running) {
    queued_packet_t *packet = NULL;

    // Try to get audio packet first (higher priority for low latency)
    if (client->audio_queue) {
      packet = packet_queue_try_dequeue(client->audio_queue);
    }

    // If no audio packet, try video
    if (!packet && client->video_queue) {
      packet = packet_queue_try_dequeue(client->video_queue);
    }

    // If still no packet, wait a bit for one
    if (!packet) {
      // Use blocking dequeue on audio queue with timeout
      if (client->audio_queue) {
        // This will block until a packet is available or queue is shutdown
        packet = packet_queue_dequeue(client->audio_queue);
      }

      // If audio queue returned NULL (shutdown), check video once more
      if (!packet && client->video_queue) {
        packet = packet_queue_try_dequeue(client->video_queue);
      }
    }

    // If we got a packet, send it
    if (packet) {
      // Send header
      ssize_t sent = send_with_timeout(client->socket, &packet->header, sizeof(packet->header), SEND_TIMEOUT);
      if (sent != sizeof(packet->header)) {
        log_error("Failed to send packet header to client %u: %zd/%zu bytes", client->client_id, sent,
                  sizeof(packet->header));
        packet_queue_free_packet(packet);
        break; // Socket error, exit thread
      }

      // Send payload if present
      if (packet->data_len > 0 && packet->data) {
        sent = send_with_timeout(client->socket, packet->data, packet->data_len, SEND_TIMEOUT);
        if (sent != (ssize_t)packet->data_len) {
          log_error("Failed to send packet payload to client %u: %zd/%zu bytes", client->client_id, sent,
                    packet->data_len);
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

    // Small sleep to prevent busy waiting if queues are empty
    if (!packet) {
      usleep(1000); // 1ms
    }
  }

  // Mark thread as stopped
  client->send_thread_running = false;
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

  // Create packet queues for outgoing data
  client->audio_queue = packet_queue_create(100); // Max 100 audio packets queued
  if (!client->audio_queue) {
    log_error("Failed to create audio queue for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->incoming_video_buffer = NULL;
    client->incoming_audio_buffer = NULL;
    pthread_mutex_unlock(&g_client_manager_mutex);
    return -1;
  }

  client->video_queue = packet_queue_create(MAX_FPS); // Max 30 video frames queued (1 second at 30fps)
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
  if (pthread_create(&client->send_thread, NULL, client_send_thread_func, client) != 0) {
    log_error("Failed to create send thread for client %u", client->client_id);
    // Note: remove_client will handle thread cleanup
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

      // Wait for send thread to exit if it's running
      if (client->send_thread_running) {
        // The shutdown signal above will cause the send thread to exit
        pthread_join(client->send_thread, NULL);
        log_debug("Send thread for client %u has terminated", client_id);
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

      // Store display name before clearing
      char display_name_copy[MAX_DISPLAY_NAME_LEN];
      strncpy(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);
      display_name_copy[MAX_DISPLAY_NAME_LEN - 1] = '\0';

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

      log_info("Removed client %u (%s), remaining clients: %d", client_id, display_name_copy, remaining_count);

      pthread_mutex_unlock(&g_client_manager_mutex);
      return 0;
    }
  }

  pthread_mutex_unlock(&g_client_manager_mutex);
  log_error("Client %u not found for removal", client_id);
  return -1;
}
