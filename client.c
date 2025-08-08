#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <zlib.h>

#include "ascii.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "compression.h"
#include "audio.h"

static int sockfd = 0;
static volatile bool g_should_exit = false;
static volatile bool g_first_connection = true;
static volatile bool g_should_reconnect = false;

static volatile int last_frame_width = 0;
static volatile int last_frame_height = 0;

static audio_context_t g_audio_context = {0};

// Compression state
static volatile bool g_expecting_compressed_frame = false;
static compressed_frame_header_t g_compression_header = {0};
static pthread_t g_data_thread;
static bool g_data_thread_created = false;
static volatile bool g_data_thread_exited = false;

/* ============================================================================
 * Multi-User Client State
 * ============================================================================
 */

// Multi-user client state
static uint32_t g_my_client_id = 0;

// Remote client tracking (up to MAX_CLIENTS)
typedef struct {
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  bool is_active;
  time_t last_seen;
} remote_client_info_t;

static remote_client_info_t g_remote_clients[MAX_CLIENTS];
static int g_remote_client_count = 0;
static pthread_mutex_t g_remote_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Function Declarations
 * ============================================================================
 */

// Multi-user packet handlers
static void handle_client_list_packet(const void *data, size_t len);
static void handle_mixed_audio_packet(const void *data, size_t len);

// Local capture threads (declarations removed - functions will be implemented later)

static int close_socket(int socketfd) {
  if (socketfd > 0) {
    log_info("Closing socket connection");
    if (0 > (socketfd = close(socketfd))) {
      log_error("Failed to close socket: %s", network_error_string(errno));
      return -1;
    }
    return socketfd;
  }
  return 0; // Socket connection not found. Just return 0 as if we closed a socket.
}

static void shutdown_client() {
  if (close_socket(sockfd) < 0) {
    exit(ASCIICHAT_ERR_NETWORK);
  }
  sockfd = 0;

  // Clean up data reception thread
  if (g_data_thread_created) {
    log_info("Waiting for data reception thread to finish...");

    // Signal thread to stop first
    g_should_exit = true;

    // Stop audio playback to help thread exit
    if (opt_audio_enabled) {
      audio_stop_playback(&g_audio_context);
    }

    // Give thread a moment to exit gracefully
    usleep(100000); // 100ms

    // Force close socket to break any blocking recv() calls
    if (sockfd > 0) {
      close(sockfd);
      sockfd = 0;
    }

    // Wait for thread to exit
    int join_result = pthread_join(g_data_thread, NULL);
    if (join_result != 0) {
      log_error("Data thread join failed: %d", join_result);
    }

    g_data_thread_created = false;

    if (opt_audio_enabled) {
      audio_destroy(&g_audio_context);
    }
    log_info("Data reception thread joined and context destroyed");
  }

  ascii_write_destroy();
  log_info("Client shutdown complete");
  log_destroy(); // Destroy logging last
}

static void sigint_handler(int sigint) {
  (void)(sigint);
  printf("\nShutdown requested...\n");
  g_should_exit = true;

  // Close socket to interrupt recv operations
  close_socket(sockfd);
  sockfd = 0;
}

static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Terminal was resized, update dimensions and recalculate aspect ratio
  update_dimensions_to_terminal_size();

  // Send new size to server if connected
  if (sockfd > 0) {
    if (send_size_packet(sockfd, opt_width, opt_height) < 0) {
      log_warn("Failed to send size update to server: %s", network_error_string(errno));
    } else {
      log_debug("Sent size update to server: %ux%u", opt_width, opt_height);
    }
  }
}

#define MAX_RECONNECT_DELAY (5 * 1000 * 1000) // Maximum delay between reconnection attempts (microseconds)

static float get_reconnect_delay(unsigned int reconnect_attempt) {
  float delay = 0.01f + 0.2f * (reconnect_attempt - 1) * 1000 * 1000;
  if (delay > MAX_RECONNECT_DELAY)
    delay = (float)MAX_RECONNECT_DELAY;
  return delay;
}

// Audio volume control
#define AUDIO_VOLUME_BOOST 2.0f

static void handle_audio_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data || len == 0) {
    return;
  }

  int num_samples = (int)(len / sizeof(float));
  if (num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_warn("Audio packet too large: %d samples", num_samples);
    return;
  }

  // Copy and apply volume boost
  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];
  const float *samples = (const float *)data;

  for (int i = 0; i < num_samples; i++) {
    audio_buffer[i] = samples[i] * AUDIO_VOLUME_BOOST;
    // Clamp to prevent distortion
    if (audio_buffer[i] > 1.0f)
      audio_buffer[i] = 1.0f;
    if (audio_buffer[i] < -1.0f)
      audio_buffer[i] = -1.0f;
  }

  audio_write_samples(&g_audio_context, audio_buffer, num_samples);
#ifdef AUDIO_DEBUG
  log_debug("Processed %d audio samples", num_samples);
#endif
}

static void maybe_size_update(unsigned short width, unsigned short height) {
  // Handle terminal size changes
  if (width != last_frame_width || height != last_frame_height) {
    console_clear();
    last_frame_width = width;
    last_frame_height = height;
    log_info("Server updated frame size to: %ux%u", width, height);
  }
}

static void handle_video_header_packet(const void *data, size_t len) {
  if (!data || len != sizeof(compressed_frame_header_t)) {
    log_warn("Invalid video header packet size: %zu", len);
    return;
  }

  memcpy(&g_compression_header, data, sizeof(compressed_frame_header_t));

  // Validate magic number
  if (g_compression_header.magic != COMPRESSION_FRAME_MAGIC) {
    log_error("Invalid compression magic: 0x%08x", g_compression_header.magic);
    return;
  }

  g_expecting_compressed_frame = true;
#ifdef COMPRESSION_DEBUG
  log_debug("Received compression header: %s, original_size=%u, compressed_size=%u",
            g_compression_header.compressed_size == 0 ? "uncompressed" : "compressed",
            g_compression_header.original_size, g_compression_header.compressed_size);
#endif
}

static void handle_video_packet(const void *data, size_t len) {
  if (!data || len == 0) {
    return;
  }

  char *frame_data = NULL;

  if (g_expecting_compressed_frame) {
    // This is compressed/uncompressed frame data with header
    g_expecting_compressed_frame = false;

    if (g_compression_header.compressed_size == 0) {
      // Uncompressed frame
      if (len != g_compression_header.original_size) {
        log_error("Uncompressed frame size mismatch: expected %u, got %zu", g_compression_header.original_size, len);
        return;
      }

      SAFE_MALLOC(frame_data, len + 1, char *);
      memcpy(frame_data, data, len);
      frame_data[len] = '\0';

    } else {
      // Compressed frame - decompress it
      if (len != g_compression_header.compressed_size) {
        log_error("Compressed frame size mismatch: expected %u, got %zu", g_compression_header.compressed_size, len);
        return;
      }

      SAFE_MALLOC(frame_data, g_compression_header.original_size + 1, char *);

      // Decompress using zlib
      uLongf decompressed_size = g_compression_header.original_size;
      int result = uncompress((Bytef *)frame_data, &decompressed_size, (const Bytef *)data, len);

      if (result != Z_OK || decompressed_size != g_compression_header.original_size) {
        log_error("Decompression failed: zlib error %d, size %lu vs expected %u", result, decompressed_size,
                  g_compression_header.original_size);
        free(frame_data);
        return;
      }

      frame_data[g_compression_header.original_size] = '\0';
#ifdef COMPRESSION_DEBUG
      log_debug("Decompressed frame: %zu -> %u bytes", len, g_compression_header.original_size);
#endif
    }

    // Verify checksum
    uint32_t received_checksum = calculate_crc32(frame_data, g_compression_header.original_size);
    if (received_checksum != g_compression_header.checksum) {
      log_error("Frame checksum mismatch: expected 0x%08x, got 0x%08x", g_compression_header.checksum,
                received_checksum);
      free(frame_data);
      return;
    }

  } else {
    // Legacy uncompressed frame (fallback for old protocol)
    SAFE_MALLOC(frame_data, len + 1, char *);

    memcpy(frame_data, data, len);
    frame_data[len] = '\0';
  }

  // Process the frame
  if (strcmp(frame_data, ASCIICHAT_WEBCAM_ERROR_STRING) == 0) {
    log_error("Server reported webcam failure: %s", frame_data);
    usleep(1000 * 1000);
  } else {
    ascii_write(frame_data);
  }

  maybe_size_update(opt_width, opt_height);

  free(frame_data);
}

static void *data_reception_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Data reception thread started");
#endif

  while (!g_should_exit) {
    if (sockfd == 0) {
      usleep(10 * 1000);
      continue;
    }

    packet_type_t type;
    void *data;
    size_t len;

    int result = receive_packet(sockfd, &type, &data, &len);
    if (result < 0) {
      log_error("Failed to receive packet");
      break;
    } else if (result == 0) {
      log_info("Server closed connection");
      break;
    }

    switch (type) {
    case PACKET_TYPE_VIDEO_HEADER:
      handle_video_header_packet(data, len);
      break;

    case PACKET_TYPE_VIDEO:
      handle_video_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(data, len);
      break;

    case PACKET_TYPE_PING:
      // Respond with PONG (implement later if needed)
      log_debug("Received PING");
      break;

    // Multi-user protocol packets
    case PACKET_TYPE_CLIENT_LIST:
      handle_client_list_packet(data, len);
      break;

    case PACKET_TYPE_MIXED_AUDIO:
      handle_mixed_audio_packet(data, len);
      break;

    default:
      log_warn("Unknown packet type: %d", type);
      break;
    }

    free(data);
    // usleep(1 * 1000); // TODO: figure out if I need this. I don't think I do.
  }

#ifdef DEBUG_THREADS
  log_debug("Data reception thread stopped");
#endif
  g_data_thread_exited = true;
  return NULL;
}

/* ============================================================================
 * Multi-User Packet Handlers
 * ============================================================================
 */

static void handle_client_list_packet(const void *data, size_t len) {
  if (!data || len != sizeof(client_list_packet_t)) {
    log_error("Invalid client list packet size: %zu", len);
    return;
  }

  const client_list_packet_t *client_list = (const client_list_packet_t *)data;

  pthread_mutex_lock(&g_remote_clients_mutex);

  // Clear existing remote clients
  memset(g_remote_clients, 0, sizeof(g_remote_clients));
  g_remote_client_count = 0;

  // Add clients from server list (excluding ourselves)
  for (uint32_t i = 0; i < client_list->client_count && i < MAX_CLIENTS; i++) {
    const client_info_packet_t *client_info = &client_list->clients[i];

    if (client_info->client_id != g_my_client_id && g_remote_client_count < MAX_CLIENTS) {
      remote_client_info_t *remote = &g_remote_clients[g_remote_client_count];
      remote->client_id = client_info->client_id;
      strncpy(remote->display_name, client_info->display_name, MAX_DISPLAY_NAME_LEN - 1);
      remote->display_name[MAX_DISPLAY_NAME_LEN - 1] = '\0';
      remote->is_active = true;
      remote->last_seen = time(NULL);
      g_remote_client_count++;
    }
  }

  pthread_mutex_unlock(&g_remote_clients_mutex);

  log_info("Updated client list: %d remote clients connected", g_remote_client_count);
  for (int i = 0; i < g_remote_client_count; i++) {
    log_info("  - Client %u: %s", g_remote_clients[i].client_id, g_remote_clients[i].display_name);
  }
}

static void handle_mixed_audio_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data || len == 0) {
    return;
  }

  int num_samples = (int)(len / sizeof(float));
  if (num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_warn("Mixed audio packet too large: %d samples", num_samples);
    return;
  }

  // Process mixed audio from multiple clients
  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];
  const float *samples = (const float *)data;

  for (int i = 0; i < num_samples; i++) {
    audio_buffer[i] = samples[i] * AUDIO_VOLUME_BOOST;
    // Clamp to prevent distortion
    if (audio_buffer[i] > 1.0f)
      audio_buffer[i] = 1.0f;
    if (audio_buffer[i] < -1.0f)
      audio_buffer[i] = -1.0f;
  }

  audio_write_samples(&g_audio_context, audio_buffer, num_samples);
  log_debug("Processed %d mixed audio samples from multiple clients", num_samples);
}

/* ============================================================================
 * Multi-User Local Capture Threads (Future Implementation)
 * ============================================================================
 */

// NOTE: Local capture thread functions will be implemented in future commits
// when actual local video/audio capture functionality is added to clients.

int main(int argc, char *argv[]) {
  log_init("client.log", LOG_DEBUG);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat client starting...");

  options_init(argc, argv);
  char *address = opt_address;
  int port = strtoint(opt_port);

  struct sockaddr_in serv_addr;

  // Cleanup nicely on Ctrl+C.
  signal(SIGINT, sigint_handler);

  // Handle terminal resize events
  signal(SIGWINCH, sigwinch_handler);

  // Initialize ASCII output for this connection
  ascii_write_init();

  // Initialize audio if enabled
  if (opt_audio_enabled) {
    if (audio_init(&g_audio_context) != 0) {
      log_fatal("Failed to initialize audio system");
      ascii_write_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    if (audio_start_playback(&g_audio_context) != 0) {
      log_error("Failed to start audio playback");
      audio_destroy(&g_audio_context);
      ascii_write_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    log_info("Audio system initialized and playback started");
  }

  /* Connection and reconnection loop */
  int reconnect_attempt = 0;

  while (!g_should_exit) {
    if (g_should_reconnect) {
      // Connection broken - will loop back to reconnection logic
      log_info("Connection terminated, preparing to reconnect...");
      if (reconnect_attempt == 0) {
        console_clear();
      }
      reconnect_attempt++;
    }

    if (g_first_connection || g_should_reconnect) {
      // Close any existing socket before attempting new connection
      if (0 > (sockfd = close_socket(sockfd))) {
        exit(ASCIICHAT_ERR_NETWORK);
      }

      if (reconnect_attempt > 0) {
        float delay = get_reconnect_delay(reconnect_attempt);
        log_info("Reconnection attempt #%d to %s:%d in %.2f seconds...", reconnect_attempt, address, port,
                 delay / 1000 / 1000);
        usleep(delay);
      } else {
        log_info("Connecting to %s:%d", address, port);
      }

      // try to open a socket
      if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        log_error("Error: could not create socket: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }

      // reserve memory space to store IP address
      memset(&serv_addr, '0', sizeof(serv_addr));

      // set type of address to IPV4
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons(port);

      // an error occurred when trying to set server address and port number
      if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
        log_error("Error: couldn't set the server address and port number: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }

      // Try to connect to the server
      if (!connect_with_timeout(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr), CONNECT_TIMEOUT)) {
        log_warn("Connection failed: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }

      // Connection successful!
      printf("Connected successfully!\n");
      log_info("Connected to server %s:%d", address, port);
      reconnect_attempt = 0; // Reset reconnection counter on successful connection

      // Send initial terminal size to server
      if (send_size_packet(sockfd, opt_width, opt_height) < 0) {
        log_error("Failed to send initial size to server: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }
      log_info("Sent initial size to server: %ux%u", opt_width, opt_height);
      maybe_size_update(opt_width, opt_height);

      // Send client join packet for multi-user support
      uint32_t my_capabilities = CLIENT_CAP_VIDEO; // Basic video capability
      if (opt_audio_enabled) {
        my_capabilities |= CLIENT_CAP_AUDIO; // Add audio if enabled
      }

      char my_display_name[MAX_DISPLAY_NAME_LEN];
      snprintf(my_display_name, sizeof(my_display_name), "ClientUser");

      if (send_client_join_packet(sockfd, my_display_name, my_capabilities) < 0) {
        log_error("Failed to send client join packet: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }
      log_info("Sent client join packet with capabilities: video=%s, audio=%s",
               (my_capabilities & CLIENT_CAP_VIDEO) ? "yes" : "no",
               (my_capabilities & CLIENT_CAP_AUDIO) ? "yes" : "no");

      // Set socket keepalive to detect broken connections
      if (set_socket_keepalive(sockfd) < 0) {
        log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
      }

      // Start data reception thread
      g_data_thread_exited = false; // Reset exit flag for new connection
      if (pthread_create(&g_data_thread, NULL, data_reception_thread_func, NULL) != 0) {
        log_error("Failed to create data reception thread");
        g_should_reconnect = true;
        continue;
      } else {
        g_data_thread_created = true;
      }

      g_first_connection = false;
      g_should_reconnect = false;
    }

    // Connection monitoring loop - wait for connection to break or shutdown
    while (!g_should_exit && sockfd > 0) {
      // Check if data thread has exited (indicates connection lost)
      if (g_data_thread_exited) {
        log_info("Data thread exited, connection lost");
        break;
      }

      usleep(100 * 1000); // 0.1 second
    }

    if (g_should_exit) {
      log_info("Shutdown requested, exiting...");
      break;
    }

    // Connection broken, attempt to reconnect
    log_info("Connection lost. Attempting to reconnect...");
    g_should_reconnect = true;

    // Clean up data thread for this connection
    if (g_data_thread_created) {
      pthread_join(g_data_thread, NULL);
      g_data_thread_created = false;
    }
  }

  shutdown_client();
  return 0;
}
