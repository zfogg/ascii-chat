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
#include "ascii_simd.h"
#include "common.h"
#include "image2ascii/simd/common.h"
#include "network.h"
#include "options.h"
#include "packet_queue.h"
#include "buffer_pool.h"
#include "ringbuffer.h"
#include "audio.h"
#include "terminal_detect.h"
#include "aspect_ratio.h"
#include "mixer.h"
#include "hashtable.h"
#include "palette.h"

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

  // Terminal capabilities (for rendering appropriate ASCII frames)
  terminal_capabilities_t terminal_caps;
  bool has_terminal_caps; // Whether we've received terminal capabilities from this client

  // NEW: Per-client palette cache
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

  // NEW: Per-client rendering threads
  pthread_t video_render_thread;
  pthread_t audio_render_thread;
  bool video_render_thread_running;
  bool audio_render_thread_running;

  // NEW: Per-client processing state
  struct timespec last_video_render_time;
  struct timespec last_audio_render_time;

  // NEW: Per-client synchronization
  pthread_mutex_t client_state_mutex;
  // CONCURRENCY FIX: Per-client cached frame protection for concurrent video generation
  pthread_mutex_t cached_frame_mutex;
  // THREAD-SAFE FRAMEBUFFER: Per-client video buffer mutex for concurrent access
  pthread_mutex_t video_buffer_mutex;
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
// NEW: Reader-writer lock for better concurrency (multiple readers, exclusive writers)
static pthread_rwlock_t g_client_manager_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* ============================================================================
 * Audio Mixing System
 * ============================================================================
 */

// Global audio mixer using new advanced mixer system
static mixer_t *g_audio_mixer = NULL;

// Statistics logging thread
static pthread_t g_stats_logger_thread;
static bool g_stats_logger_thread_created = false;

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
// NEW: Per-client rendering thread functions
void *client_video_render_thread_func(void *arg);
void *client_audio_render_thread_func(void *arg);

// Statistics logging thread function
void *stats_logger_thread_func(void *arg);

// Per-client video mixing functions
char *create_mixed_ascii_frame_for_client(uint32_t target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *out_size);
int queue_ascii_frame_for_client(client_info_t *client, const char *ascii_frame, size_t frame_size);

// Client management functions
int add_client(int socket, const char *client_ip, int port);
int remove_client(uint32_t client_id);
// NEW: Per-client render thread lifecycle functions
int create_client_render_threads(client_info_t *client);
int destroy_client_render_threads(client_info_t *client);

// PERFORMANCE OPTIMIZATION: Global client list change counter for cache invalidation

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

  // Use log system in signal handler - it has its own safety mechanisms
  log_info("SIGTERM received - shutting down server...");

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
    pthread_rwlock_rdlock(&g_client_manager_rwlock);
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
    pthread_rwlock_unlock(&g_client_manager_rwlock);

    log_info("Active clients: %d, Audio: %d, Video: %d", active_clients, clients_with_audio, clients_with_video);
    log_info("Blank frames sent: %llu", (unsigned long long)g_blank_frames_sent);

    // Log hash table statistics
    if (g_client_manager.client_hashtable) {
      hashtable_print_stats(g_client_manager.client_hashtable, "Client Lookup");
    }

    // Log per-client buffer pool stats if they have local pools
    pthread_rwlock_rdlock(&g_client_manager_rwlock);
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
    pthread_rwlock_unlock(&g_client_manager_rwlock);
  }

  log_info("Statistics logger thread stopped");
  return NULL;
}

/* ============================================================================
 * Video Mixing Functions
 * ============================================================================
 */

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

// NEW: Per-client frame generation function
// Generates an ASCII frame specifically for one target client with their preferences
char *create_mixed_ascii_frame_for_client(uint32_t target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *out_size) {
  (void)wants_stretch; // Unused - we always handle aspect ratio ourselves
  if (!out_size || width == 0 || height == 0) {
    log_error("Invalid parameters for create_mixed_ascii_frame_for_client: width=%u, height=%u, out_size=%p", width,
              height, out_size);
    return NULL;
  }

  // Collect all active image sources (same logic as global version)
  typedef struct {
    image_t *image;
    uint32_t client_id;
  } image_source_t;

  image_source_t sources[MAX_CLIENTS];
  int source_count = 0;

  // CONCURRENCY FIX: Now using READ lock since framebuffer operations are thread-safe
  // framebuffer_read_multi_frame() now uses internal mutex for thread safety
  // Multiple video generation threads can safely access client list concurrently
  pthread_rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

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
      // Same logic as global version but with per-client optimizations
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
          frames_to_read = (int)occupancy - 1; // Read all but keep latest
          if (frames_to_read > 20)
            frames_to_read = 20; // Cap to avoid excessive processing
          if (frames_to_read < 1)
            frames_to_read = 1; // Always read at least 1
        }

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

        if (got_new_frame) {
          // CONCURRENCY FIX: Lock only THIS client's cached frame data
          pthread_mutex_lock(&client->cached_frame_mutex);

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
          } else {
            log_error("Failed to allocate cache buffer for client %u", client->client_id);
            client->has_cached_frame = false;
          }

          pthread_mutex_unlock(&client->cached_frame_mutex);
        }
      }

      // Use either the new frame or the cached frame
      multi_source_frame_t *frame_to_use = NULL;
      bool using_cached_frame = false;
      if (got_new_frame) {
        frame_to_use = &current_frame;
      } else {
        // CONCURRENCY FIX: Lock THIS client's cached frame for reading
        pthread_mutex_lock(&client->cached_frame_mutex);
        if (client->has_cached_frame) {
          frame_to_use = &client->last_valid_frame;
          using_cached_frame = true;
        } else {
          // No cached frame - unlock immediately since we won't use it
          pthread_mutex_unlock(&client->cached_frame_mutex);
        }
        // Note: If using_cached_frame=true, we keep the lock held while using the data
      }

      if (frame_to_use && frame_to_use->data && frame_to_use->size > sizeof(uint32_t) * 2) {
        // Parse the image data
        // Format: [width:4][height:4][rgb_data:w*h*3]
        uint32_t img_width = ntohl(*(uint32_t *)frame_to_use->data);
        uint32_t img_height = ntohl(*(uint32_t *)(frame_to_use->data + sizeof(uint32_t)));

        // Validate dimensions are reasonable (max 4K resolution)
        if (img_width == 0 || img_width > 4096 || img_height == 0 || img_height > 4096) {
          log_error("Per-client: Invalid image dimensions from client %u: %ux%u (data may be corrupted)",
                    client->client_id, img_width, img_height);
          // Don't free cached frame, just skip this client
          if (got_new_frame) {
            buffer_pool_free(current_frame.data, current_frame.size);
          }
          continue;
        }

        // Validate that the frame size matches expected size
        size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
        if (frame_to_use->size != expected_size) {
          log_error("Per-client: Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image",
                    client->client_id, frame_to_use->size, expected_size, img_width, img_height);
          // Don't free cached frame, just skip this client
          if (got_new_frame) {
            buffer_pool_free(current_frame.data, current_frame.size);
          }
          continue;
        }

        // Extract pixel data
        rgb_t *pixels = (rgb_t *)(frame_to_use->data + sizeof(uint32_t) * 2);

        // Create image from buffer pool for consistent video pipeline management
        image_t *img = image_new_from_pool(img_width, img_height);
        if (img) {
          memcpy(img->pixels, pixels, (size_t)img_width * (size_t)img_height * sizeof(rgb_t));
          sources[source_count].image = img;
          sources[source_count].client_id = client->client_id;
          source_count++;
        }

        // Free the current frame data if we got a new one (cached frame persists)
        // The framebuffer allocates this data via buffer_pool_alloc() and we must free it
        if (got_new_frame && current_frame.data) {
          buffer_pool_free(current_frame.data, current_frame.size);
          current_frame.data = NULL; // Prevent double-free
        }
      }

      // CONCURRENCY FIX: Unlock cached frame mutex if we were using cached data
      if (using_cached_frame) {
        pthread_mutex_unlock(&client->cached_frame_mutex);
      }
    }
  }
  pthread_rwlock_unlock(&g_client_manager_rwlock);

  // No active video sources - don't generate placeholder frames
  if (source_count == 0) {
    log_debug("Per-client %u: No video sources available - returning NULL frame", target_client_id);
    *out_size = 0;
    return NULL;
  }

  // Create composite image for multiple sources with grid layout (same logic as global version)
  image_t *composite = NULL;

  if (source_count == 1 && sources[0].image) {
    // Single source - check if target client wants half-block mode for 2x resolution
    client_info_t *target_client = find_client_by_id_fast(target_client_id);
    bool use_half_block = target_client && target_client->has_terminal_caps &&
                          target_client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK;

    int composite_width_px, composite_height_px;

    if (use_half_block) {
      // Half-block mode: use full terminal dimensions for 2x resolution
      composite_width_px = width;
      composite_height_px = height * 2;
    } else {
      // Normal modes: use aspect-ratio fitted dimensions
      calculate_fit_dimensions_pixel(sources[0].image->w, sources[0].image->h, width, height, &composite_width_px,
                                     &composite_height_px);
    }

    // Create composite from buffer pool for consistent memory management
    composite = image_new_from_pool(composite_width_px, composite_height_px);
    if (!composite) {
      log_error("Per-client %u: Failed to create composite image", target_client_id);
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        image_destroy_to_pool(sources[i].image);
      }
      return NULL;
    }

    image_clear(composite);

    if (use_half_block) {
      // Half-block mode: manual aspect ratio and centering to preserve 2x resolution
      float src_aspect = (float)sources[0].image->w / (float)sources[0].image->h;
      float target_aspect = (float)composite_width_px / (float)composite_height_px;

      int fitted_width, fitted_height;
      if (src_aspect > target_aspect) {
        // Source is wider - fit to width
        fitted_width = composite_width_px;
        fitted_height = (int)(composite_width_px / src_aspect);
      } else {
        // Source is taller - fit to height
        fitted_height = composite_height_px;
        fitted_width = (int)(composite_height_px * src_aspect);
      }

      // Calculate centering offsets
      int x_offset = (composite_width_px - fitted_width) / 2;
      int y_offset = (composite_height_px - fitted_height) / 2;

      // Create fitted image from buffer pool
      image_t *fitted = image_new_from_pool(fitted_width, fitted_height);
      if (fitted) {
        image_resize(sources[0].image, fitted);

        // Copy fitted image to center of composite
        for (int y = 0; y < fitted_height; y++) {
          for (int x = 0; x < fitted_width; x++) {
            int src_idx = y * fitted_width + x;
            int dst_x = x_offset + x;
            int dst_y = y_offset + y;
            int dst_idx = dst_y * composite_width_px + dst_x;

            if (dst_x >= 0 && dst_x < composite_width_px && dst_y >= 0 && dst_y < composite_height_px) {
              composite->pixels[dst_idx] = fitted->pixels[src_idx];
            }
          }
        }
        image_destroy_to_pool(fitted);
      }
    } else {
      // Normal modes: simple resize to fit calculated dimensions
      image_resize(sources[0].image, composite);
    }
  } else if (source_count > 1) {
    // Multiple sources - create grid layout
    client_info_t *target_client = find_client_by_id_fast(target_client_id);
    bool use_half_block_multi = target_client && target_client->has_terminal_caps &&
                                target_client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK;

    int composite_width_px = width;
    int composite_height_px = use_half_block_multi ? height * 2 : height;

    composite = image_new_from_pool(composite_width_px, composite_height_px);
    if (!composite) {
      log_error("Per-client %u: Failed to create composite image", target_client_id);
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        image_destroy_to_pool(sources[i].image);
      }
      return NULL;
    }

    image_clear(composite);

    // Calculate grid dimensions based on source count
    int grid_cols = (source_count == 2) ? 2 : (source_count <= 4) ? 2 : 3;
    int grid_rows = (source_count + grid_cols - 1) / grid_cols;

    // Calculate cell dimensions in characters (for layout)
    int cell_width_chars = width / grid_cols;
    int cell_height_chars = height / grid_rows;

    // Convert cell dimensions to pixels for image operations
    int cell_width_px = cell_width_chars;       // 1:1 mapping
    int cell_height_px = cell_height_chars * 2; // 2:1 mapping to match composite dimensions

    // Place each source in the grid
    for (int i = 0; i < source_count && i < 9; i++) { // Max 9 sources in 3x3 grid
      if (!sources[i].image)
        continue;

      int row = i / grid_cols;
      int col = i % grid_cols;

      // Calculate cell position in pixels
      int cell_x_offset_px = col * cell_width_px;
      int cell_y_offset_px = row * cell_height_px;

      float src_aspect = (float)sources[i].image->w / (float)sources[i].image->h;
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

      // Ensure target dimensions don't exceed cell dimensions
      if (target_width_px > cell_width_px)
        target_width_px = cell_width_px;
      if (target_height_px > cell_height_px)
        target_height_px = cell_height_px;

      // Create resized image with standard allocation
      image_t *resized = image_new_from_pool(target_width_px, target_height_px);
      if (resized) {
        image_resize(sources[i].image, resized);

        // Center the resized image within the cell (pixel coordinates)
        int x_padding_px = (cell_width_px - target_width_px) / 2;
        int y_padding_px = (cell_height_px - target_height_px) / 2;

        // Copy resized image to composite with proper bounds checking
        for (int y = 0; y < target_height_px; y++) {
          for (int x = 0; x < target_width_px; x++) {
            int src_idx = y * target_width_px + x;
            int dst_x = cell_x_offset_px + x_padding_px + x;
            int dst_y = cell_y_offset_px + y_padding_px + y;

            // CRITICAL FIX: Use correct stride for composite image
            int dst_idx = dst_y * composite_width_px + dst_x;

            // Bounds checking with correct composite dimensions
            if (src_idx >= 0 && src_idx < resized->w * resized->h && dst_idx >= 0 &&
                dst_idx < composite->w * composite->h && dst_x >= 0 && dst_x < composite_width_px && dst_y >= 0 &&
                dst_y < composite_height_px) {
              composite->pixels[dst_idx] = resized->pixels[src_idx];
            }
          }
        }

        image_destroy_to_pool(resized);
      }
    }
  } else {
    // No sources, create empty composite with standard allocation
    composite = image_new_from_pool(width, height * 2);
    if (!composite) {
      log_error("Per-client %u: Failed to create empty image", target_client_id);
      *out_size = 0;
      return NULL;
    }
    image_clear(composite);
  }

  // Find the target client to get their terminal capabilities
  client_info_t *target_client = find_client_by_id_fast(target_client_id);
  char *ascii_frame = NULL;

  if (target_client && target_client->has_terminal_caps) {
    // Use capability-aware ASCII conversion for better terminal compatibility
    pthread_mutex_lock(&target_client->client_state_mutex);
    terminal_capabilities_t caps_snapshot = target_client->terminal_caps;
    pthread_mutex_unlock(&target_client->client_state_mutex);

    if (target_client->client_palette_initialized) {
      // Render with client's custom palette using enhanced capabilities
      if (caps_snapshot.render_mode == RENDER_MODE_HALF_BLOCK) {
        ascii_frame = ascii_convert_with_capabilities(composite, width, height * 2, &caps_snapshot, true, false,
                                                      target_client->client_palette_chars,
                                                      target_client->client_luminance_palette);
      } else {
        ascii_frame = ascii_convert_with_capabilities(composite, width, height, &caps_snapshot, true, false,
                                                      target_client->client_palette_chars,
                                                      target_client->client_luminance_palette);
      }
    } else {
      // Client palette not initialized - this is an error condition
      log_error("Client %u palette not initialized - cannot render frame", target_client_id);
      ascii_frame = NULL;
    }
  } else {
    // Don't send frames until we receive client capabilities - saves bandwidth and CPU
    log_debug("Per-client %u: Waiting for terminal capabilities before sending frames (no capabilities received yet)",
              target_client_id);
    ascii_frame = NULL;
  }

  if (ascii_frame) {
    *out_size = strlen(ascii_frame);
    // log_debug("Per-client %u: Generated ASCII frame: %zu bytes, %ux%u, color=%s", target_client_id, *out_size, width,
    //           height, wants_color ? "yes" : "no");
  } else {
    log_error("Per-client %u: Failed to convert image to ASCII", target_client_id);
    *out_size = 0;
  }

  // Clean up using standard deallocation
  image_destroy_to_pool(composite);
  for (int i = 0; i < source_count; i++) {
    if (sources[i].image) {
      image_destroy_to_pool(sources[i].image);
    }
  }

  return ascii_frame;
}

// NEW: Helper function to queue ASCII frame for a specific client
// Packages frame with proper packet structure and queues to client's video_queue
int queue_ascii_frame_for_client(client_info_t *client, const char *ascii_frame, size_t frame_size) {
  if (!client || !ascii_frame || frame_size == 0) {
    log_error("Invalid parameters for queue_ascii_frame_for_client");
    return -1;
  }

  if (!client->video_queue) {
    log_error("Client %u has no video queue", client->client_id);
    return -1;
  }

  // Create ASCII frame packet header
  ascii_frame_packet_t frame_header = {.width = htonl(client->width),
                                       .height = htonl(client->height),
                                       .original_size = htonl((uint32_t)frame_size),
                                       .compressed_size = htonl(0), // Not using compression for per-client frames yet
                                       .checksum = htonl(asciichat_crc32(ascii_frame, frame_size)),
                                       .flags = htonl(client->wants_color ? FRAME_FLAG_HAS_COLOR : 0)};

  // Allocate buffer for complete packet (header + data)
  size_t packet_size = sizeof(ascii_frame_packet_t) + frame_size;
  char *packet_buffer = malloc(packet_size);
  if (!packet_buffer) {
    log_error("Failed to allocate packet buffer for client %u", client->client_id);
    return -1;
  }

  // Copy header and frame data into single buffer
  memcpy(packet_buffer, &frame_header, sizeof(ascii_frame_packet_t));
  memcpy(packet_buffer + sizeof(ascii_frame_packet_t), ascii_frame, frame_size);

  // Queue the complete frame as a single packet
  int result = packet_queue_enqueue(client->video_queue, PACKET_TYPE_ASCII_FRAME, packet_buffer, packet_size, 0, true);

  // Free the temporary packet buffer (packet_queue_enqueue copies data when copy=true)
  free(packet_buffer);

  if (result < 0) {
    log_debug("Failed to queue ASCII frame for client %u: queue full or shutdown", client->client_id);
    return -1;
  }

  return 0;
}

/* ============================================================================
 * Main Server Logic
 * ============================================================================
 */

int main(int argc, char *argv[]) {
  options_init(argc, argv, false);

  // Initialize logging - use specified log file or default
  const char *log_filename = (strlen(opt_log_file) > 0) ? opt_log_file : "server.log";
  log_init(log_filename, LOG_DEBUG);

  // Initialize palette based on command line options
  const char *custom_chars = opt_palette_custom_set ? opt_palette_custom : NULL;
  if (apply_palette_config(opt_palette_type, custom_chars) != 0) {
    log_error("Failed to apply palette configuration");
    return 1;
  }

  // Handle quiet mode - disable terminal output when opt_quiet is enabled
  log_set_terminal_output(!opt_quiet);
#ifdef DEBUG_MEMORY
  debug_memory_set_quiet_mode(opt_quiet);
#endif

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

  log_info("SERVER: Initializing luminance palette...");
  ascii_simd_init();
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

  // Initialize audio mixer if audio is enabled
  if (opt_audio_enabled) {
    log_info("SERVER: Initializing audio mixer for per-client audio rendering...");
    g_audio_mixer = mixer_create(MAX_CLIENTS, AUDIO_SAMPLE_RATE);
    if (!g_audio_mixer) {
      log_error("Failed to initialize audio mixer");
    } else {
      log_info("SERVER: Audio mixer initialized successfully for per-client audio rendering");
    }
  } else {
    log_info("SERVER: Audio disabled, skipping audio mixer initialization");
  }

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
    // SAFETY FIX: Collect client IDs under lock, then process without lock to prevent infinite loops
    typedef struct {
      uint32_t client_id;
      pthread_t receive_thread;
    } cleanup_task_t;

    cleanup_task_t cleanup_tasks[MAX_CLIENTS];
    int cleanup_count = 0;

    pthread_rwlock_rdlock(&g_client_manager_rwlock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      // Check if this client has been marked inactive by its receive thread
      if (client->client_id != 0 && !client->active && client->receive_thread != 0) {
        // Collect cleanup task
        cleanup_tasks[cleanup_count].client_id = client->client_id;
        cleanup_tasks[cleanup_count].receive_thread = client->receive_thread;
        cleanup_count++;

        // Clear the thread handle immediately to avoid double-join
        client->receive_thread = 0;
      }
    }
    pthread_rwlock_unlock(&g_client_manager_rwlock);

    // Process cleanup tasks without holding lock (prevents infinite loops)
    for (int i = 0; i < cleanup_count; i++) {
      log_info("Cleaning up disconnected client %u", cleanup_tasks[i].client_id);
      // Wait for receive thread to finish
      pthread_join(cleanup_tasks[i].receive_thread, NULL);
      // Remove the client and clean up resources
      remove_client(cleanup_tasks[i].client_id);
    }

    // Accept network connection with timeout
    // log_debug("Calling accept_with_timeout on fd=%d with timeout=%d", listenfd, ACCEPT_TIMEOUT);
    int client_sock = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    int saved_errno = errno; // Capture errno immediately to prevent corruption
    // log_debug("accept_with_timeout returned: client_sock=%d, errno=%d (%s)", client_sock, saved_errno,
    //           client_sock < 0 ? strerror(saved_errno) : "success");
    if (client_sock < 0) {
      if (saved_errno == ETIMEDOUT) {
        // Timeout is normal, just continue
        // log_debug("Accept timed out after %d seconds, continuing loop", ACCEPT_TIMEOUT);
#ifdef DEBUG_MEMORY
        // debug_memory_report();
#endif
        continue;
      }
      if (saved_errno == EINTR) {
        // Interrupted by signal - check if we should exit
        log_debug("accept() interrupted by signal");
        if (g_should_exit) {
          break;
        }
        continue;
      }
      // log_error("Network accept failed: %s", network_error_string(saved_errno));
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
  pthread_rwlock_wrlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket > 0) {
      log_debug("Closing socket for client %u to interrupt receive thread", g_client_manager.clients[i].client_id);
      shutdown(g_client_manager.clients[i].socket, SHUT_RDWR);
      close(g_client_manager.clients[i].socket);
      g_client_manager.clients[i].socket = -1;
    }
  }
  pthread_rwlock_unlock(&g_client_manager_rwlock);

  // Wait for stats logger thread to finish
  if (g_stats_logger_thread_created) {
    log_info("Waiting for stats logger thread to finish...");
    pthread_join(g_stats_logger_thread, NULL);
    log_info("Stats logger thread stopped");
    g_stats_logger_thread_created = false;
  }

  // Cleanup cached frames
  cleanup_frame_cache();

  // Clean up all connected clients
  log_info("Cleaning up connected clients...");

  // First, close all client sockets to interrupt receive threads
  pthread_rwlock_wrlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->active && client->socket > 0) {
      log_debug("Closing socket for client %u to interrupt receive thread", client->client_id);
      shutdown(client->socket, SHUT_RDWR);
      close(client->socket);
      client->socket = 0; // Mark as closed
    }
  }
  pthread_rwlock_unlock(&g_client_manager_rwlock);

  // Now clean up client resources
  log_info("Scanning for clients to clean up (active or with allocated resources)...");
  // SAFETY FIX: Collect client IDs under lock, then process without lock to prevent infinite loops

  uint32_t active_clients_to_remove[MAX_CLIENTS];
  uint32_t inactive_clients_to_remove[MAX_CLIENTS];
  int active_clients_found = 0;
  int inactive_clients_with_resources = 0;

  pthread_rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->client_id != 0) { // Client slot has been used
      bool has_resources = (client->audio_queue != NULL || client->video_queue != NULL ||
                            client->incoming_video_buffer != NULL || client->incoming_audio_buffer != NULL);

      if (client->active) {
        active_clients_to_remove[active_clients_found] = client->client_id;
        active_clients_found++;
      } else if (has_resources) {
        inactive_clients_to_remove[inactive_clients_with_resources] = client->client_id;
        inactive_clients_with_resources++;
      }
    }
  }
  pthread_rwlock_unlock(&g_client_manager_rwlock);

  // Process removals without holding lock (prevents infinite loops and lock contention)
  for (int i = 0; i < active_clients_found; i++) {
    log_info("Found active client %u during shutdown cleanup", active_clients_to_remove[i]);
    remove_client(active_clients_to_remove[i]);
  }

  for (int i = 0; i < inactive_clients_with_resources; i++) {
    log_info("Found inactive client %u with allocated resources - cleaning up", inactive_clients_to_remove[i]);
    remove_client(inactive_clients_to_remove[i]);
  }
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

  // Cleanup audio mixer if it was created
  if (g_audio_mixer) {
    log_info("Cleaning up audio mixer...");
    mixer_destroy(g_audio_mixer);
    g_audio_mixer = NULL;
    log_info("Audio mixer cleanup complete");
  }

  log_info("Server shutdown complete");

  // Cleanup client manager resources
  if (g_client_manager.client_hashtable) {
    hashtable_destroy(g_client_manager.client_hashtable);
    g_client_manager.client_hashtable = NULL;
  }

  // No emergency cleanup needed - proper resource management handles cleanup

  // Clean up SIMD caches before other cleanup
  simd_caches_destroy_all();
  
  // Explicitly clean up global buffer pool before atexit handlers
  // This ensures any malloc fallbacks are freed while pool tracking is still active
  data_buffer_pool_cleanup_global();

  // Destroy mutexes (do this before log_destroy in case logging uses them)
  pthread_mutex_destroy(&g_stats_mutex);
  pthread_mutex_destroy(&g_socket_mutex);
  pthread_rwlock_destroy(&g_client_manager_rwlock);

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
    if (frame_count[client->client_id % MAX_CLIENTS] % 25000 == 0) {
      char pretty[64];
      format_bytes_pretty(len, pretty, sizeof(pretty));
      log_debug("Client %u has sent %d IMAGE_FRAME packets (%s)", client->client_id,
                frame_count[client->client_id % MAX_CLIENTS], pretty);
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

// Handles batched audio samples from client (new efficient format)
static void handle_audio_batch_packet(client_info_t *client, const void *data, size_t len) {
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
      return;
    }

    if (total_samples > AUDIO_BATCH_SAMPLES * 2) { // Sanity check
      log_error("Audio batch too large from client %u: %u samples", client->client_id, total_samples);
      return;
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
      handle_audio_batch_packet(client, data, len);
      break;
    }

    case PACKET_TYPE_CLIENT_CAPABILITIES: {
      // Handle terminal capabilities from client
      if (len == sizeof(terminal_capabilities_packet_t)) {
        const terminal_capabilities_packet_t *caps = (const terminal_capabilities_packet_t *)data;

        pthread_mutex_lock(&client->client_state_mutex);

        // Convert from network byte order and store dimensions
        client->width = ntohs(caps->width);
        client->height = ntohs(caps->height);

        // Store terminal capabilities
        client->terminal_caps.capabilities = ntohl(caps->capabilities);
        client->terminal_caps.color_level = ntohl(caps->color_level);
        client->terminal_caps.color_count = ntohl(caps->color_count);
        client->terminal_caps.render_mode = ntohl(caps->render_mode);
        client->terminal_caps.detection_reliable = caps->detection_reliable;

        // Copy terminal type strings safely
        strncpy(client->terminal_caps.term_type, caps->term_type, sizeof(client->terminal_caps.term_type) - 1);
        client->terminal_caps.term_type[sizeof(client->terminal_caps.term_type) - 1] = '\0';

        strncpy(client->terminal_caps.colorterm, caps->colorterm, sizeof(client->terminal_caps.colorterm) - 1);
        client->terminal_caps.colorterm[sizeof(client->terminal_caps.colorterm) - 1] = '\0';

        // NEW: Store client's palette preferences
        client->terminal_caps.utf8_support = ntohl(caps->utf8_support);
        client->terminal_caps.palette_type = ntohl(caps->palette_type);
        strncpy(client->terminal_caps.palette_custom, caps->palette_custom,
                sizeof(client->terminal_caps.palette_custom) - 1);
        client->terminal_caps.palette_custom[sizeof(client->terminal_caps.palette_custom) - 1] = '\0';

        // Initialize client's per-client palette cache
        const char *custom_chars =
            (client->terminal_caps.palette_type == PALETTE_CUSTOM && client->terminal_caps.palette_custom[0])
                ? client->terminal_caps.palette_custom
                : NULL;

        if (initialize_client_palette((palette_type_t)client->terminal_caps.palette_type, custom_chars,
                                      client->client_palette_chars, &client->client_palette_len,
                                      client->client_luminance_palette) == 0) {
          client->client_palette_type = (palette_type_t)client->terminal_caps.palette_type;
          client->client_palette_initialized = true;
          log_info("Client %d palette initialized: type=%u, %zu chars, utf8=%u", client->client_id,
                   client->terminal_caps.palette_type, client->client_palette_len, client->terminal_caps.utf8_support);
        } else {
          log_error("Failed to initialize palette for client %d, using server default", client->client_id);
          client->client_palette_initialized = false;
        }

        // Mark that we have received capabilities for this client
        client->has_terminal_caps = true;

        // Update legacy wants_color field based on color capabilities
        client->wants_color = (client->terminal_caps.color_level > TERM_COLOR_NONE);

        pthread_mutex_unlock(&client->client_state_mutex);

        log_info("Client %u capabilities: %ux%u, color_level=%s (%u colors), caps=0x%x, term=%s, colorterm=%s, "
                 "render_mode=%s, reliable=%s",
                 client->client_id, client->width, client->height,
                 terminal_color_level_name(client->terminal_caps.color_level), client->terminal_caps.color_count,
                 client->terminal_caps.capabilities, client->terminal_caps.term_type, client->terminal_caps.colorterm,
                 (client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK
                      ? "half-block"
                      : (client->terminal_caps.render_mode == RENDER_MODE_BACKGROUND ? "background" : "foreground")),
                 client->terminal_caps.detection_reliable ? "yes" : "no");
      } else {
        log_error("Invalid client capabilities packet size: %u, expected %zu", len,
                  sizeof(terminal_capabilities_packet_t));
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
#ifdef DEBUG_NETWORK
          log_debug("Queued PONG response for client %u", client->client_id);
#endif
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
#ifdef DEBUG_NETWORK
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

// NEW: Per-client video rendering thread function
void *client_video_render_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in video render thread");
    return NULL;
  }

  log_info("Video render thread started for client %u (%s)", client->client_id, client->display_name);

  const int base_frame_interval_ms = 1000 / 60; // 60 FPS base rate
  struct timespec last_render_time;
  clock_gettime(CLOCK_MONOTONIC, &last_render_time);

  bool should_continue = true;
  while (should_continue && !g_should_exit) {
    // CRITICAL FIX: Check thread state with mutex protection
    pthread_mutex_lock(&client->client_state_mutex);
    should_continue = client->video_render_thread_running && client->active;
    pthread_mutex_unlock(&client->client_state_mutex);

    if (!should_continue) {
      break;
    }
    // Rate limiting
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_render_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_render_time.tv_nsec) / 1000000;

    if (elapsed_ms < base_frame_interval_ms) {
      interruptible_usleep((base_frame_interval_ms - elapsed_ms) * 1000);
      continue;
    }

    // CRITICAL FIX: Protect client state access with per-client mutex
    pthread_mutex_lock(&client->client_state_mutex);
    uint32_t client_id_snapshot = client->client_id;
    unsigned short width_snapshot = client->width;
    unsigned short height_snapshot = client->height;
    bool wants_stretch_snapshot = client->wants_stretch;
    bool active_snapshot = client->active;
    pthread_mutex_unlock(&client->client_state_mutex);

    // Check if client is still active after getting snapshot
    if (!active_snapshot) {
      break;
    }

    // Phase 2 IMPLEMENTED: Generate frame specifically for THIS client using snapshot data
    size_t frame_size = 0;
    char *ascii_frame = create_mixed_ascii_frame_for_client(client_id_snapshot, width_snapshot, height_snapshot,
                                                            wants_stretch_snapshot, &frame_size);

    // Phase 2 IMPLEMENTED: Queue frame for this specific client
    if (ascii_frame && frame_size > 0) {
      int queue_result = queue_ascii_frame_for_client(client, ascii_frame, frame_size);
      if (queue_result == 0) {
        // Successfully queued frame - log occasionally for monitoring
        static int success_count = 0;
        success_count++;
        if (success_count == 1 || success_count % (30 * 60) == 0) { // Log every ~4 seconds at 60fps
          char pretty_size[64];
          format_bytes_pretty(frame_size, pretty_size, sizeof(pretty_size));
          log_info("Per-client render: Successfully queued %d ASCII frames for client %u (%ux%u, %s)", success_count,
                   client->client_id, client->width, client->height, pretty_size);
        }
      }
      free(ascii_frame);
    } else {
      // No frame generated (probably no video sources) - this is normal, no error logging needed
      static int no_frame_count = 0;
      no_frame_count++;
      if (no_frame_count % 300 == 0) { // Log every ~10 seconds at 30fps
        log_debug("Per-client render: No video sources available for client %u (%d attempts)", client->client_id,
                  no_frame_count);
      }
    }

    last_render_time = current_time;
  }

  log_info("Video render thread stopped for client %u", client->client_id);
  return NULL;
}

// NEW: Per-client audio rendering thread function
void *client_audio_render_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in audio render thread");
    return NULL;
  }

  log_info("Audio render thread started for client %u (%s)", client->client_id, client->display_name);

  float mix_buffer[AUDIO_FRAMES_PER_BUFFER];

  bool should_continue = true;
  while (should_continue && !g_should_exit) {
    // CRITICAL FIX: Check thread state with mutex protection
    pthread_mutex_lock(&client->client_state_mutex);
    should_continue = client->audio_render_thread_running && client->active;
    pthread_mutex_unlock(&client->client_state_mutex);

    if (!should_continue) {
      break;
    }
    if (!g_audio_mixer) {
      interruptible_usleep(10000);
      continue;
    }

    // CRITICAL FIX: Protect client state access with per-client mutex
    pthread_mutex_lock(&client->client_state_mutex);
    uint32_t client_id_snapshot = client->client_id;
    bool active_snapshot = client->active;
    packet_queue_t *audio_queue_snapshot = client->audio_queue;
    pthread_mutex_unlock(&client->client_state_mutex);

    // Check if client is still active after getting snapshot
    if (!active_snapshot || !audio_queue_snapshot) {
      break;
    }

    // Create mix excluding THIS client's audio using snapshot data
    int samples_mixed =
        mixer_process_excluding_source(g_audio_mixer, mix_buffer, AUDIO_FRAMES_PER_BUFFER, client_id_snapshot);

    // Queue audio directly for this specific client using snapshot data
    if (samples_mixed > 0) {
      size_t data_size = AUDIO_FRAMES_PER_BUFFER * sizeof(float);
      int result = packet_queue_enqueue(audio_queue_snapshot, PACKET_TYPE_AUDIO, mix_buffer, data_size, 0, true);
      if (result < 0) {
        log_debug("Failed to queue audio for client %u", client_id_snapshot);
      }
    }

    // Audio mixing rate - 5.8ms to match buffer size
    interruptible_usleep(5800);
  }

  log_info("Audio render thread stopped for client %u", client->client_id);
  return NULL;
}

// NEW: Per-client render thread lifecycle functions
int create_client_render_threads(client_info_t *client) {
  if (!client) {
    log_error("Cannot create render threads for NULL client");
    return -1;
  }

  // Initialize per-client mutex
  if (pthread_mutex_init(&client->client_state_mutex, NULL) != 0) {
    log_error("Failed to initialize client state mutex for client %u", client->client_id);
    return -1;
  }

  // CONCURRENCY FIX: Initialize per-client cached frame mutex
  if (pthread_mutex_init(&client->cached_frame_mutex, NULL) != 0) {
    log_error("Failed to initialize cached frame mutex for client %u", client->client_id);
    pthread_mutex_destroy(&client->client_state_mutex);
    return -1;
  }

  // THREAD-SAFE FRAMEBUFFER: Initialize per-client video buffer mutex
  if (pthread_mutex_init(&client->video_buffer_mutex, NULL) != 0) {
    log_error("Failed to initialize video buffer mutex for client %u", client->client_id);
    pthread_mutex_destroy(&client->cached_frame_mutex);
    pthread_mutex_destroy(&client->client_state_mutex);
    return -1;
  }

  // Initialize render thread control flags
  client->video_render_thread_running = false;
  client->audio_render_thread_running = false;

  // Create video rendering thread
  if (pthread_create(&client->video_render_thread, NULL, client_video_render_thread_func, client) != 0) {
    log_error("Failed to create video render thread for client %u", client->client_id);
    pthread_mutex_destroy(&client->video_buffer_mutex);
    pthread_mutex_destroy(&client->cached_frame_mutex);
    pthread_mutex_destroy(&client->client_state_mutex);
    return -1;
  }

  // CRITICAL FIX: Protect thread_running flag with mutex
  pthread_mutex_lock(&client->client_state_mutex);
  client->video_render_thread_running = true;
  pthread_mutex_unlock(&client->client_state_mutex);

  // Create audio rendering thread
  if (pthread_create(&client->audio_render_thread, NULL, client_audio_render_thread_func, client) != 0) {
    log_error("Failed to create audio render thread for client %u", client->client_id);
    // Clean up video thread
    pthread_mutex_lock(&client->client_state_mutex);
    client->video_render_thread_running = false;
    pthread_mutex_unlock(&client->client_state_mutex);
    pthread_cancel(client->video_render_thread);
    pthread_join(client->video_render_thread, NULL);
    pthread_mutex_destroy(&client->video_buffer_mutex);
    pthread_mutex_destroy(&client->cached_frame_mutex);
    pthread_mutex_destroy(&client->client_state_mutex);
    return -1;
  }

  // CRITICAL FIX: Protect thread_running flag with mutex
  pthread_mutex_lock(&client->client_state_mutex);
  client->audio_render_thread_running = true;
  pthread_mutex_unlock(&client->client_state_mutex);

  log_info("Created render threads for client %u", client->client_id);
  return 0;
}

int destroy_client_render_threads(client_info_t *client) {
  if (!client) {
    log_error("Cannot destroy render threads for NULL client");
    return -1;
  }

  log_debug("Destroying render threads for client %u", client->client_id);

  // Signal threads to stop - CRITICAL FIX: Protect with mutex
  pthread_mutex_lock(&client->client_state_mutex);
  client->video_render_thread_running = false;
  client->audio_render_thread_running = false;
  pthread_mutex_unlock(&client->client_state_mutex);

  // Wake up any sleeping threads using shutdown condition
  pthread_mutex_lock(&g_shutdown_mutex);
  pthread_cond_broadcast(&g_shutdown_cond);
  pthread_mutex_unlock(&g_shutdown_mutex);

  // Wait for threads to finish (deterministic cleanup)
  if (client->video_render_thread) {
    int result = pthread_join(client->video_render_thread, NULL);
    if (result == 0) {
#ifdef DEBUG_THREADS
      log_debug("Video render thread joined for client %u", client->client_id);
#endif
    } else {
      log_error("Failed to join video render thread for client %u: %s", client->client_id, strerror(result));
    }
    client->video_render_thread = 0;
  }

  if (client->audio_render_thread) {
    int result = pthread_join(client->audio_render_thread, NULL);
    if (result == 0) {
#ifdef DEBUG_THREADS
      log_debug("Audio render thread joined for client %u", client->client_id);
#endif
    } else {
      log_error("Failed to join audio render thread for client %u: %s", client->client_id, strerror(result));
    }
    client->audio_render_thread = 0;
  }

  // Destroy per-client mutex
  pthread_mutex_destroy(&client->client_state_mutex);

  log_debug("Successfully destroyed render threads for client %u", client->client_id);
  return 0;
}

// Client management functions
int add_client(int socket, const char *client_ip, int port) {
  pthread_rwlock_wrlock(&g_client_manager_rwlock);

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
    pthread_rwlock_unlock(&g_client_manager_rwlock);
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
    pthread_rwlock_unlock(&g_client_manager_rwlock);
    return -1;
  }

  // Create individual audio buffer for this client
  client->incoming_audio_buffer = audio_ring_buffer_create();
  if (!client->incoming_audio_buffer) {
    log_error("Failed to create audio buffer for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    client->incoming_video_buffer = NULL;
    pthread_rwlock_unlock(&g_client_manager_rwlock);
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
    pthread_rwlock_unlock(&g_client_manager_rwlock);
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
    pthread_rwlock_unlock(&g_client_manager_rwlock);
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

  pthread_rwlock_unlock(&g_client_manager_rwlock);

  // Start threads for this client
  if (pthread_create(&client->receive_thread, NULL, client_receive_thread_func, client) != 0) {
    log_error("Failed to create receive thread for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  // Start send thread for this client
  if (pthread_create(&client->send_thread, NULL, client_send_thread_func, client) != 0) {
    log_error("Failed to create send thread for client %u", client->client_id);
    // Join the receive thread before cleaning up to prevent race conditions
    pthread_join(client->receive_thread, NULL);
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
  } else {
#ifdef DEBUG_NETWORK
    log_info("Queued initial server state for client %u: %u connected clients", client->client_id,
             state.connected_client_count);
#endif
  }

  // NEW: Create per-client rendering threads
  if (create_client_render_threads(client) != 0) {
    log_error("Failed to create render threads for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  return client->client_id;
}

int remove_client(uint32_t client_id) {
  pthread_rwlock_wrlock(&g_client_manager_rwlock);

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

      // NEW: Destroy per-client render threads
      destroy_client_render_threads(client);

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

      // Store display name before clearing
      char display_name_copy[MAX_DISPLAY_NAME_LEN];
      SAFE_STRNCPY(display_name_copy, client->display_name, MAX_DISPLAY_NAME_LEN - 1);

      // CONCURRENCY FIX: Destroy per-client cached frame mutex
      pthread_mutex_destroy(&client->cached_frame_mutex);
      // THREAD-SAFE FRAMEBUFFER: Destroy per-client video buffer mutex
      pthread_mutex_destroy(&client->video_buffer_mutex);
      pthread_mutex_destroy(&client->client_state_mutex);

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

      pthread_rwlock_unlock(&g_client_manager_rwlock);
      return 0;
    }
  }

  pthread_rwlock_unlock(&g_client_manager_rwlock);

  log_error("Client %u not found for removal", client_id);
  return -1;
}
