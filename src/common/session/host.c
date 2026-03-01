/**
 * @file session/host.c
 * @brief 🏠 Server-side session hosting implementation
 * @ingroup session
 *
 * Implements the session host abstraction for server-side client management
 * and session coordination.
 *
 * NOTE: This is a stub implementation that provides the API structure.
 * Full implementation will integrate with existing server code in a future phase.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "session/host.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/client.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/ringbuffer.h>
#include "session/audio.h"
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/ascii/common.h>

#include <string.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>

/* ============================================================================
 * Session Host Constants
 * ============================================================================ */

/** @brief Default maximum clients */
#define SESSION_HOST_DEFAULT_MAX_CLIENTS 32

/* ============================================================================
 * Session Host Internal Types
 * ============================================================================ */

/**
 * @brief Internal client record structure
 */
typedef struct {
  participant_type_t participant_type; // NETWORK or MEMORY
  uint32_t client_id;
  socket_t socket;
  char ip_address[64];
  int port;
  bool active;
  bool video_active;
  bool audio_active;
  uint64_t connected_at;

  /** @brief Alternative transport (WebRTC, WebSocket, etc.) - NULL if using socket only */
  struct acip_transport *transport;

  /** @brief Incoming video frame buffer (for host render thread) */
  image_t *incoming_video;

  /** @brief Incoming audio ringbuffer (written by receive loop, read by render thread) */
  ringbuffer_t *incoming_audio;
} session_host_client_t;

/* ============================================================================
 * Session Host Context Structure
 * ============================================================================ */

/**
 * @brief Internal session host structure
 *
 * Contains server state, client list, and callback configuration.
 */
typedef struct session_host {
  /** @brief Port to listen on */
  int port;

  /** @brief IPv4 bind address */
  char ipv4_address[64];

  /** @brief IPv6 bind address */
  char ipv6_address[64];

  /** @brief Maximum clients */
  int max_clients;

  /** @brief Encryption enabled */
  bool encryption_enabled;

  /** @brief Key path */
  char key_path[512];

  /** @brief Password */
  char password[256];

  /** @brief Event callbacks */
  session_host_callbacks_t callbacks;

  /** @brief User data for callbacks */
  void *user_data;

  /** @brief IPv4 listen socket */
  socket_t socket_v4;

  /** @brief IPv6 listen socket */
  socket_t socket_v6;

  /** @brief Server is running */
  bool running;

  /** @brief Client array */
  session_host_client_t *clients;

  /** @brief Current client count */
  int client_count;

  /** @brief Next client ID counter */
  uint32_t next_client_id;

  /** @brief Client list mutex */
  mutex_t clients_mutex;

  /** @brief Accept thread handle */
  asciichat_thread_t accept_thread;

  /** @brief Accept thread is running */
  bool accept_thread_running;

  /** @brief Receive thread handle */
  asciichat_thread_t receive_thread;

  /** @brief Receive thread is running */
  bool receive_thread_running;

  /** @brief Render thread handle (for video mixing and audio distribution) */
  asciichat_thread_t render_thread;

  /** @brief Render thread is running */
  bool render_thread_running;

  /** @brief Audio context for mixing (host only) */
  session_audio_ctx_t *audio_ctx;

  /** @brief Opus decoder for decoding incoming Opus audio */
  opus_codec_t *opus_decoder;

  /** @brief Opus encoder for encoding mixed audio for broadcast */
  opus_codec_t *opus_encoder;

  /** @brief Context is initialized */
  bool initialized;
} session_host_t;

/* ============================================================================
 * Session Host Lifecycle Functions
 * ============================================================================ */

session_host_t *session_host_create(const session_host_config_t *config) {
  if (!config) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_host_create: NULL config");
    return NULL;
  }

  // Allocate host
  session_host_t *host = SAFE_CALLOC(1, sizeof(session_host_t), session_host_t *);

  // Copy configuration
  host->port = config->port > 0 ? config->port : OPT_PORT_INT_DEFAULT;
  host->max_clients = config->max_clients > 0 ? config->max_clients : SESSION_HOST_DEFAULT_MAX_CLIENTS;
  host->encryption_enabled = config->encryption_enabled;

  if (config->ipv4_address) {
    SAFE_STRNCPY(host->ipv4_address, config->ipv4_address, sizeof(host->ipv4_address));
  }

  if (config->ipv6_address) {
    SAFE_STRNCPY(host->ipv6_address, config->ipv6_address, sizeof(host->ipv6_address));
  }

  if (config->key_path) {
    SAFE_STRNCPY(host->key_path, config->key_path, sizeof(host->key_path));
  }

  if (config->password) {
    SAFE_STRNCPY(host->password, config->password, sizeof(host->password));
  }

  host->callbacks = config->callbacks;
  host->user_data = config->user_data;

  // Initialize sockets to invalid
  host->socket_v4 = INVALID_SOCKET_VALUE;
  host->socket_v6 = INVALID_SOCKET_VALUE;
  host->running = false;

  // Allocate client array
  host->clients = SAFE_CALLOC((size_t)host->max_clients, sizeof(session_host_client_t), session_host_client_t *);

  host->client_count = 0;
  host->next_client_id = 1;

  // Initialize mutex
  if (mutex_init(&host->clients_mutex, "host_clients") != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize clients mutex");
    SAFE_FREE(host->clients);
    SAFE_FREE(host);
    return NULL;
  }

  host->initialized = true;
  return host;
}

void session_host_destroy(session_host_t *host) {
  if (!host) {
    return;
  }

  // Stop if running
  if (host->running) {
    session_host_stop(host);
  }

  // Clean up audio resources
  if (host->audio_ctx) {
    session_audio_destroy(host->audio_ctx);
    host->audio_ctx = NULL;
  }
  if (host->opus_decoder) {
    opus_codec_destroy(host->opus_decoder);
    host->opus_decoder = NULL;
  }

  // Close sockets
  if (host->socket_v4 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
  }
  if (host->socket_v6 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v6);
    host->socket_v6 = INVALID_SOCKET_VALUE;
  }

  // Free client array and per-client resources
  if (host->clients) {
    for (int i = 0; i < host->max_clients; i++) {
      if (host->clients[i].incoming_video) {
        image_destroy(host->clients[i].incoming_video);
        host->clients[i].incoming_video = NULL;
      }
      if (host->clients[i].incoming_audio) {
        ringbuffer_destroy(host->clients[i].incoming_audio);
        host->clients[i].incoming_audio = NULL;
      }
    }
    SAFE_FREE(host->clients);
  }

  // Destroy mutex
  mutex_destroy(&host->clients_mutex);

  // Clear sensitive data
  memset(host->password, 0, sizeof(host->password));

  host->initialized = false;
  SAFE_FREE(host);
}

/* ============================================================================
 * Session Host Server Control Functions
 * ============================================================================ */

/**
 * @brief Create and bind a listening socket on the given address and port
 * @return Socket on success, INVALID_SOCKET_VALUE on failure
 */
static socket_t create_listen_socket(const char *address, int port) {
  struct addrinfo hints, *result = NULL, *rp = NULL;
  char port_str[16];

  if (!address)
    address = "0.0.0.0";

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  int s = getaddrinfo(address, port_str, &hints, &result);
  if (s != 0) {
    SET_ERRNO(ERROR_NETWORK, "getaddrinfo failed: %s", gai_strerror(s));
    return INVALID_SOCKET_VALUE;
  }

  socket_t listen_sock = INVALID_SOCKET_VALUE;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    listen_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (listen_sock == INVALID_SOCKET_VALUE) {
      continue;
    }

    // Set SO_REUSEADDR to allow rebinding quickly after restart
    int reuse = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) {
      log_warn("setsockopt SO_REUSEADDR failed");
    }

    if (bind(listen_sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
      break; // Success
    }

    socket_close(listen_sock);
    listen_sock = INVALID_SOCKET_VALUE;
  }

  freeaddrinfo(result);

  if (listen_sock == INVALID_SOCKET_VALUE) {
    SET_ERRNO_SYS(ERROR_NETWORK_BIND, "Failed to bind listen socket on %s:%d", address, port);
    return INVALID_SOCKET_VALUE;
  }

  if (listen(listen_sock, SOMAXCONN) != 0) {
    SET_ERRNO_SYS(ERROR_NETWORK_BIND, "listen() failed on %s:%d", address, port);
    socket_close(listen_sock);
    return INVALID_SOCKET_VALUE;
  }

  return listen_sock;
}

/**
 * @brief Accept loop thread - continuously accept incoming client connections
 * @param arg session_host_t pointer
 * @return NULL
 *
 * This thread runs in a loop accepting connections on the listen socket.
 * For each accepted connection, it adds the client to the host and invokes callbacks.
 */
static void *accept_loop_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host)
    return NULL;

  log_info("Accept loop started");

  // Use select() to handle accept with timeout
  struct timeval tv;
  fd_set readfds;
  int max_fd;

  while (host->accept_thread_running && host->running) {
    // Set timeout to 1 second
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    max_fd = 0;

    if (host->socket_v4 != INVALID_SOCKET_VALUE) {
      FD_SET(host->socket_v4, &readfds);
      max_fd = (int)host->socket_v4;
    }

    // Wait for incoming connections
    int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
    if (activity < 0) {
      if (errno != EINTR) {
        log_error("select() failed");
      }
      continue;
    }

    if (activity == 0) {
      // Timeout - check if we should exit
      continue;
    }

    // Check for incoming connections
    if (host->socket_v4 != INVALID_SOCKET_VALUE && FD_ISSET(host->socket_v4, &readfds)) {
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = sizeof(client_addr);

      // Accept incoming connection
      socket_t client_socket = accept(host->socket_v4, (struct sockaddr *)&client_addr, &client_addr_len);
      if (client_socket == INVALID_SOCKET_VALUE) {
        log_warn("accept() failed");
        continue;
      }

      // Get client IP and port
      char client_ip[64];
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
      int client_port = ntohs(client_addr.sin_port);

      log_info("New connection from %s:%d", client_ip, client_port);

      // Add client to host
      uint32_t client_id = session_host_add_client(host, client_socket, client_ip, client_port);
      if (client_id == 0) {
        log_error("Failed to add client");
        socket_close(client_socket);
      }
    }
  }

  log_info("Accept loop stopped");
  return NULL;
}

/**
 * @brief Receive loop thread - continuously receive packets from connected clients
 * @param arg session_host_t pointer
 * @return NULL
 *
 * This thread runs in a loop listening for packets from all connected clients.
 * When a packet arrives, it processes it based on the packet type.
 */
static void *receive_loop_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host)
    return NULL;

  log_info("Receive loop started");

  struct timeval tv;
  fd_set readfds;
  socket_t max_fd;

  while (host->receive_thread_running && host->running) {
    // Set timeout to 1 second
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    max_fd = INVALID_SOCKET_VALUE;

    // Add all active client sockets to the set
    mutex_lock(&host->clients_mutex);
    for (int i = 0; i < host->max_clients; i++) {
      if (host->clients[i].active && host->clients[i].socket != INVALID_SOCKET_VALUE) {
        FD_SET(host->clients[i].socket, &readfds);
        if (max_fd == INVALID_SOCKET_VALUE || host->clients[i].socket > max_fd) {
          max_fd = host->clients[i].socket;
        }
      }
    }
    mutex_unlock(&host->clients_mutex);

    // If no clients, just wait for timeout
    if (max_fd == INVALID_SOCKET_VALUE) {
      continue;
    }

    // Wait for data on any client socket
    int activity = select((int)max_fd + 1, &readfds, NULL, NULL, &tv);
    if (activity < 0) {
      if (errno != EINTR) {
        log_error("select() failed in receive loop");
      }
      continue;
    }

    if (activity == 0) {
      // Timeout - check if we should exit
      continue;
    }

    // Check each client for incoming data
    mutex_lock(&host->clients_mutex);
    for (int i = 0; i < host->max_clients; i++) {
      if (!host->clients[i].active || host->clients[i].socket == INVALID_SOCKET_VALUE) {
        continue;
      }

      if (!FD_ISSET(host->clients[i].socket, &readfds)) {
        continue;
      }

      // Try to receive packet from this client
      packet_type_t ptype;
      void *data = NULL;
      size_t len = 0;
      asciichat_error_t result = packet_receive(host->clients[i].socket, &ptype, &data, &len);

      if (result != ASCIICHAT_OK) {
        log_warn("packet_receive failed from client %u: %d", host->clients[i].client_id, result);
        // Client disconnected or error - will be cleaned up by timeout mechanism
        continue;
      }

      // Process packet based on type
      uint32_t client_id = host->clients[i].client_id;
      socket_t client_socket = host->clients[i].socket;
      mutex_unlock(&host->clients_mutex);

      switch (ptype) {
      case PACKET_TYPE_IMAGE_FRAME:
        // Client sent a video frame - parse and store in incoming buffer
        if (data && len >= sizeof(image_frame_packet_t)) {
          const image_frame_packet_t *frame_hdr = (const image_frame_packet_t *)data;
          const uint8_t *pixel_data = (const uint8_t *)data + sizeof(image_frame_packet_t);
          size_t pixel_data_size = len - sizeof(image_frame_packet_t);

          // Find client and store frame in incoming_video buffer
          mutex_lock(&host->clients_mutex);
          for (int j = 0; j < host->max_clients; j++) {
            if (host->clients[j].client_id == client_id && host->clients[j].incoming_video) {
              // Store frame in the image buffer
              image_t *img = host->clients[j].incoming_video;
              if (img->w == (int)frame_hdr->width && img->h == (int)frame_hdr->height) {
                // Copy pixel data (RGB format)
                size_t expected_size = (size_t)frame_hdr->width * frame_hdr->height * 3;
                if (pixel_data_size >= expected_size) {
                  memcpy(img->pixels, pixel_data, expected_size);
                  log_debug_every(500 * US_PER_MS_INT, "Frame received from client %u (%ux%u)", client_id,
                                  frame_hdr->width, frame_hdr->height);
                }
              }
              break;
            }
          }
          mutex_unlock(&host->clients_mutex);
        }
        break;

      case PACKET_TYPE_AUDIO_OPUS_BATCH:
        // Client sent Opus-encoded audio batch
        if (data && len > 16) { // Must have at least header
          const uint8_t *batch_data = (const uint8_t *)data;
          // Parse header: sample_rate (4), frame_duration (4), frame_count (4), reserved (4)
          (void)batch_data[0]; // Avoid unused variable warning if we don't use sample_rate/frame_duration
          uint32_t batch_frame_count = *(const uint32_t *)(batch_data + 8);

          if (batch_frame_count > 0 && batch_frame_count <= 1000) {
            const uint16_t *frame_sizes = (const uint16_t *)(batch_data + 16);
            const uint8_t *opus_frames = batch_data + 16 + (batch_frame_count * sizeof(uint16_t));

            // Find client and decode audio
            mutex_lock(&host->clients_mutex);
            for (int j = 0; j < host->max_clients; j++) {
              if (host->clients[j].client_id == client_id && host->clients[j].incoming_audio && host->opus_decoder) {
                // Decode each Opus frame and write to ringbuffer
                const uint8_t *current_frame = opus_frames;
                for (uint32_t k = 0; k < batch_frame_count; k++) {
                  uint16_t frame_size = frame_sizes[k];
                  if (frame_size > 0) {
                    // Allocate buffer for decoded samples
                    float decoded_samples[960]; // Max 20ms @ 48kHz
                    int decoded_count =
                        opus_codec_decode(host->opus_decoder, current_frame, (int)frame_size, decoded_samples, 960);
                    if (decoded_count > 0) {
                      // Write samples to ringbuffer one at a time
                      for (int s = 0; s < decoded_count; s++) {
                        ringbuffer_write(host->clients[j].incoming_audio, &decoded_samples[s]);
                      }
                    }
                  }
                  current_frame += frame_size;
                }
                log_debug_every(NS_PER_MS_INT, "Audio batch received from client %u (%u frames)", client_id,
                                batch_frame_count);
                break;
              }
            }
            mutex_unlock(&host->clients_mutex);
          }
        }
        break;

      case PACKET_TYPE_STREAM_START:
        log_info("Client %u started streaming", client_id);
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].client_id == client_id) {
            host->clients[j].video_active = true;
            break;
          }
        }
        mutex_unlock(&host->clients_mutex);
        break;

      case PACKET_TYPE_STREAM_STOP:
        log_info("Client %u stopped streaming", client_id);
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].client_id == client_id) {
            host->clients[j].video_active = false;
            break;
          }
        }
        mutex_unlock(&host->clients_mutex);
        break;

      case PACKET_TYPE_PING:
        // Respond with PONG
        log_debug_every(NS_PER_MS_INT, "PING from client %u", client_id);
        packet_send(client_socket, PACKET_TYPE_PONG, NULL, 0);
        break;

      case PACKET_TYPE_CLIENT_LEAVE:
        log_info("Client %u requested disconnect", client_id);
        // Mark for removal (will be handled separately to avoid deadlock)
        break;

      default:
        log_warn("Unknown packet type %u from client %u", ptype, client_id);
        break;
      }

      // Free packet data
      if (data) {
        SAFE_FREE(data);
      }

      mutex_lock(&host->clients_mutex);
    }
    mutex_unlock(&host->clients_mutex);
  }

  log_info("Receive loop stopped");
  return NULL;
}

/**
 * @brief Host render thread - mixes media and broadcasts to participants
 *
 * DESIGN: Reuses patterns from server/render.c
 * - Collects video frames from all participants (60 FPS)
 * - Broadcasts mixed ASCII frame to all participants
 * - Mixes audio from all participants (100 FPS)
 * - Broadcasts mixed audio to all participants
 */
static void *host_render_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host) {
    SET_ERRNO(ERROR_INVALID_PARAM, "host_render_thread: invalid host");
    return NULL;
  }

  log_info("Host render thread started");

  uint64_t last_video_render_ns = 0;
  uint64_t last_audio_render_ns = 0;

  while (host->render_thread_running && host->running) {
    uint64_t now_ns = time_get_ns();

    // VIDEO RENDERING (60 FPS = 16.7ms)
    if (time_elapsed_ns(last_video_render_ns, now_ns) >= NS_PER_MS_INT * 16) {
      // Collect video frames from all participants and create grid layout
      mutex_lock(&host->clients_mutex);

      // Count active participants with video
      int active_video_count = 0;
      for (int i = 0; i < host->max_clients; i++) {
        if (host->clients[i].active && host->clients[i].video_active && host->clients[i].incoming_video) {
          active_video_count++;
        }
      }

      // If we have active participants, generate and broadcast grid
      if (active_video_count > 0) {
        // Allocate arrays for ASCII frames and sources
        char **ascii_frames = SAFE_MALLOC(active_video_count * sizeof(char *), char **);
        ascii_frame_source_t *sources =
            SAFE_MALLOC(active_video_count * sizeof(ascii_frame_source_t), ascii_frame_source_t *);

        if (ascii_frames && sources) {
          // Convert each incoming video frame to ASCII
          int frame_idx = 0;
          for (int i = 0; i < host->max_clients; i++) {
            if (host->clients[i].active && host->clients[i].video_active && host->clients[i].incoming_video) {
              image_t *img = host->clients[i].incoming_video;

              // Convert image to ASCII (80x24 for each frame in grid, monochrome for now)
              ascii_frames[frame_idx] =
                  ascii_convert(img, 80, 24, false, false, false, NULL, g_default_luminance_palette);
              if (ascii_frames[frame_idx]) {
                sources[frame_idx].frame_data = ascii_frames[frame_idx];
                sources[frame_idx].frame_size = strlen(ascii_frames[frame_idx]) + 1;
              } else {
                sources[frame_idx].frame_data = "";
                sources[frame_idx].frame_size = 1;
              }
              frame_idx++;
            }
          }

          // Create grid layout from all ASCII frames
          size_t grid_size = 0;
          char *grid_frame = ascii_create_grid(sources, active_video_count, 80, 24, &grid_size);

          if (grid_frame) {
            // Broadcast grid to all participants
            for (int i = 0; i < host->max_clients; i++) {
              if (host->clients[i].active && host->clients[i].socket != INVALID_SOCKET_VALUE) {
                packet_send(host->clients[i].socket, PACKET_TYPE_ASCII_FRAME, grid_frame, grid_size);
              }
            }
            SAFE_FREE(grid_frame);
          }

          // Free ASCII frames
          for (int i = 0; i < active_video_count; i++) {
            if (ascii_frames[i]) {
              SAFE_FREE(ascii_frames[i]);
            }
          }
        }

        SAFE_FREE(ascii_frames);
        SAFE_FREE(sources);
      }

      mutex_unlock(&host->clients_mutex);
      log_debug_every(NS_PER_MS_INT, "Video render cycle (%d active)", active_video_count);
      last_video_render_ns = now_ns;
    }

    // AUDIO RENDERING (100 FPS = 10ms)
    if (time_elapsed_ns(last_audio_render_ns, now_ns) >= NS_PER_MS_INT * 10) {
      // Mix audio from all participants
      // 1. Read samples from each participant's incoming_audio ringbuffer
      // 2. Mix into output buffer (simple addition with clipping)
      // 3. Encode with Opus
      // 4. Broadcast mixed audio via av_send_audio_opus_batch()

      if (host->audio_ctx && host->opus_encoder) {
        float mixed_audio[960]; // 20ms @ 48kHz
        memset(mixed_audio, 0, sizeof(mixed_audio));

        // Lock clients mutex to safely read audio buffers
        mutex_lock(&host->clients_mutex);
        for (int i = 0; i < host->max_clients; i++) {
          if (!host->clients[i].active || !host->clients[i].audio_active) {
            continue;
          }

          if (!host->clients[i].incoming_audio) {
            continue;
          }

          // Read audio samples one-by-one from this participant's ringbuffer
          // ringbuffer_read() reads one element (1 float) per call
          for (int j = 0; j < 960; j++) {
            float sample = 0.0f;
            if (ringbuffer_read(host->clients[i].incoming_audio, &sample)) {
              // Successfully read a sample
              mixed_audio[j] += sample;
              // Clip to [-1.0, 1.0] to prevent distortion
              if (mixed_audio[j] > 1.0f) {
                mixed_audio[j] = 1.0f;
              } else if (mixed_audio[j] < -1.0f) {
                mixed_audio[j] = -1.0f;
              }
            }
            // If ringbuffer is empty, sample remains 0.0f and that's fine
          }
        }
        mutex_unlock(&host->clients_mutex);

        // Encode to Opus
        uint8_t opus_buffer[1000];
        size_t opus_len = opus_codec_encode(host->opus_encoder, mixed_audio, 960, opus_buffer, sizeof(opus_buffer));

        if (opus_len > 0) {
          // Broadcast mixed audio to all participants
          uint16_t frame_sizes[1] = {(uint16_t)opus_len};
          av_send_audio_opus_batch(host->socket_v4, opus_buffer, opus_len, frame_sizes, 48000, 20, 1, NULL);
        }
      }

      log_debug_every(NS_PER_MS_INT, "Audio render cycle");
      last_audio_render_ns = now_ns;
    }

    // Small sleep to prevent busy-loop
    platform_sleep_ms(1);
  }

  log_info("Host render thread stopped");
  return NULL;
}

asciichat_error_t session_host_start(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_start: invalid host");
  }

  if (host->running) {
    return ASCIICHAT_OK; // Already running
  }

  // Create listen socket(s)
  // For simplicity, we bind to IPv4 if specified, otherwise default to 0.0.0.0
  const char *bind_address = host->ipv4_address[0] ? host->ipv4_address : "0.0.0.0";

  host->socket_v4 = create_listen_socket(bind_address, host->port);
  if (host->socket_v4 == INVALID_SOCKET_VALUE) {
    log_error("Failed to create IPv4 listen socket");
    if (host->callbacks.on_error) {
      host->callbacks.on_error(host, ERROR_NETWORK_BIND, "Failed to create listen socket", host->user_data);
    }
    return GET_ERRNO();
  }

  host->running = true;
  log_info("Session host listening on %s:%d", bind_address, host->port);

  // Spawn accept loop thread
  host->accept_thread_running = true;
  if (asciichat_thread_create(&host->accept_thread, "accept", accept_loop_thread, host) != 0) {
    log_error("Failed to spawn accept loop thread");
    host->accept_thread_running = false;
    if (host->callbacks.on_error) {
      host->callbacks.on_error(host, ERROR_THREAD, "Failed to spawn accept loop thread", host->user_data);
    }
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
    host->running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn accept loop thread");
  }

  // Spawn receive loop thread
  host->receive_thread_running = true;
  if (asciichat_thread_create(&host->receive_thread, "host_recv", receive_loop_thread, host) != 0) {
    log_error("Failed to spawn receive loop thread");
    host->receive_thread_running = false;
    host->accept_thread_running = false;
    asciichat_thread_join(&host->accept_thread, NULL);
    if (host->callbacks.on_error) {
      host->callbacks.on_error(host, ERROR_THREAD, "Failed to spawn receive loop thread", host->user_data);
    }
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
    host->running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn receive loop thread");
  }

  // Spawn render thread (optional - can be started later)
  // This thread handles video mixing and audio distribution
  return ASCIICHAT_OK;
}

void session_host_stop(session_host_t *host) {
  if (!host || !host->initialized || !host->running) {
    return;
  }

  // Stop render thread if running
  if (host->render_thread_running) {
    host->render_thread_running = false;
    asciichat_thread_join(&host->render_thread, NULL);
    log_info("Render thread joined");
  }

  // Stop receive loop thread (reads from client sockets)
  if (host->receive_thread_running) {
    host->receive_thread_running = false;
    asciichat_thread_join(&host->receive_thread, NULL);
    log_info("Receive loop thread joined");
  }

  // Stop accept loop thread (before closing listen socket)
  if (host->accept_thread_running) {
    host->accept_thread_running = false;
    asciichat_thread_join(&host->accept_thread, NULL);
    log_info("Accept loop thread joined");
  }

  // Disconnect all clients
  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active) {
      // Invoke callback
      if (host->callbacks.on_client_leave) {
        host->callbacks.on_client_leave(host, host->clients[i].client_id, host->user_data);
      }

      // Close socket
      if (host->clients[i].socket != INVALID_SOCKET_VALUE) {
        socket_close(host->clients[i].socket);
        host->clients[i].socket = INVALID_SOCKET_VALUE;
      }

      host->clients[i].active = false;
    }
  }
  host->client_count = 0;
  mutex_unlock(&host->clients_mutex);

  // Close listen sockets
  if (host->socket_v4 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
  }
  if (host->socket_v6 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v6);
    host->socket_v6 = INVALID_SOCKET_VALUE;
  }

  host->running = false;
}

bool session_host_is_running(session_host_t *host) {
  if (!host || !host->initialized) {
    return false;
  }
  return host->running;
}

/* ============================================================================
 * Session Host Client Management Functions
 * ============================================================================ */

uint32_t session_host_add_client(session_host_t *host, socket_t socket, const char *ip, int port) {
  if (!host || !host->initialized) {
    return 0;
  }

  mutex_lock(&host->clients_mutex);

  // Check if we have room
  if (host->client_count >= host->max_clients) {
    mutex_unlock(&host->clients_mutex);
    SET_ERRNO(ERROR_SESSION_FULL, "Maximum clients reached");
    return 0;
  }

  // Find empty slot
  for (int i = 0; i < host->max_clients; i++) {
    if (!host->clients[i].active) {
      host->clients[i].participant_type = PARTICIPANT_TYPE_NETWORK;
      host->clients[i].client_id = host->next_client_id++;
      host->clients[i].socket = socket;
      if (ip) {
        SAFE_STRNCPY(host->clients[i].ip_address, ip, sizeof(host->clients[i].ip_address));
      }
      host->clients[i].port = port;
      host->clients[i].active = true;
      host->clients[i].video_active = false;
      host->clients[i].audio_active = false;
      host->clients[i].connected_at = (uint64_t)time(NULL);
      host->clients[i].transport = NULL; // No alternative transport initially

      // Allocate media buffers
      host->clients[i].incoming_video = image_new(480, 270);                        // Network-optimal size (HD preview)
      host->clients[i].incoming_audio = ringbuffer_create(sizeof(float), 960 * 10); // ~200ms buffer @ 48kHz

      if (!host->clients[i].incoming_video || !host->clients[i].incoming_audio) {
        // Cleanup on allocation failure
        if (host->clients[i].incoming_video) {
          image_destroy(host->clients[i].incoming_video);
          host->clients[i].incoming_video = NULL;
        }
        if (host->clients[i].incoming_audio) {
          ringbuffer_destroy(host->clients[i].incoming_audio);
          host->clients[i].incoming_audio = NULL;
        }
        host->clients[i].active = false;
        mutex_unlock(&host->clients_mutex);
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate media buffers for client");
        return 0;
      }

      uint32_t client_id = host->clients[i].client_id;
      host->client_count++;

      mutex_unlock(&host->clients_mutex);

      // Invoke callback
      if (host->callbacks.on_client_join) {
        host->callbacks.on_client_join(host, client_id, host->user_data);
      }

      return client_id;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return 0;
}

uint32_t session_host_add_memory_participant(session_host_t *host) {
  if (!host || !host->initialized) {
    return 0;
  }

  mutex_lock(&host->clients_mutex);

  // Check if we have room
  if (host->client_count >= host->max_clients) {
    mutex_unlock(&host->clients_mutex);
    SET_ERRNO(ERROR_SESSION_FULL, "Maximum clients reached");
    return 0;
  }

  // Check if memory participant already exists (only one allowed)
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].participant_type == PARTICIPANT_TYPE_MEMORY) {
      mutex_unlock(&host->clients_mutex);
      SET_ERRNO(ERROR_INVALID_PARAM, "Memory participant already exists");
      return 0;
    }
  }

  // Find empty slot
  for (int i = 0; i < host->max_clients; i++) {
    if (!host->clients[i].active) {
      host->clients[i].participant_type = PARTICIPANT_TYPE_MEMORY;
      host->clients[i].client_id = host->next_client_id++;
      host->clients[i].socket = INVALID_SOCKET_VALUE; // No socket for memory participant
      SAFE_STRNCPY(host->clients[i].ip_address, "memory", sizeof(host->clients[i].ip_address));
      host->clients[i].port = 0;
      host->clients[i].active = true;
      host->clients[i].video_active = false;
      host->clients[i].audio_active = false;
      host->clients[i].connected_at = (uint64_t)time(NULL);
      host->clients[i].transport = NULL;

      // Allocate media buffers (same as network clients)
      host->clients[i].incoming_video = image_new(480, 270);
      host->clients[i].incoming_audio = ringbuffer_create(sizeof(float), 960 * 10);

      if (!host->clients[i].incoming_video || !host->clients[i].incoming_audio) {
        if (host->clients[i].incoming_video) {
          image_destroy(host->clients[i].incoming_video);
          host->clients[i].incoming_video = NULL;
        }
        if (host->clients[i].incoming_audio) {
          ringbuffer_destroy(host->clients[i].incoming_audio);
          host->clients[i].incoming_audio = NULL;
        }
        host->clients[i].active = false;
        mutex_unlock(&host->clients_mutex);
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate media buffers for memory participant");
        return 0;
      }

      uint32_t participant_id = host->clients[i].client_id;
      host->client_count++;

      mutex_unlock(&host->clients_mutex);

      log_info("Added memory participant with ID %u", participant_id);

      // Invoke callback
      if (host->callbacks.on_client_join) {
        host->callbacks.on_client_join(host, participant_id, host->user_data);
      }

      return participant_id;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return 0;
}

asciichat_error_t session_host_inject_frame(session_host_t *host, uint32_t participant_id, const image_t *frame) {
  if (!host || !host->initialized || !frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_inject_frame: invalid parameters");
  }

  mutex_lock(&host->clients_mutex);

  // Find memory participant
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == participant_id &&
        host->clients[i].participant_type == PARTICIPANT_TYPE_MEMORY) {

      if (!host->clients[i].incoming_video) {
        mutex_unlock(&host->clients_mutex);
        return SET_ERRNO(ERROR_INVALID_STATE, "Memory participant has no video buffer");
      }

      // Copy frame data into buffer
      image_t *dest = host->clients[i].incoming_video;
      if (dest->w != frame->w || dest->h != frame->h) {
        // Reallocate if size changed
        image_destroy(dest);
        dest = image_new(frame->w, frame->h);
        if (!dest) {
          host->clients[i].incoming_video = NULL;
          mutex_unlock(&host->clients_mutex);
          return SET_ERRNO(ERROR_MEMORY, "Failed to reallocate video buffer");
        }
        host->clients[i].incoming_video = dest;
      }

      // Copy pixel data
      memcpy(dest->pixels, frame->pixels, frame->w * frame->h * sizeof(rgb_pixel_t));

      host->clients[i].video_active = true;

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Memory participant not found");
}

asciichat_error_t session_host_inject_audio(session_host_t *host, uint32_t participant_id, const float *samples,
                                            size_t count) {
  if (!host || !host->initialized || !samples || count == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_inject_audio: invalid parameters");
  }

  mutex_lock(&host->clients_mutex);

  // Find memory participant
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == participant_id &&
        host->clients[i].participant_type == PARTICIPANT_TYPE_MEMORY) {

      if (!host->clients[i].incoming_audio) {
        mutex_unlock(&host->clients_mutex);
        return SET_ERRNO(ERROR_INVALID_STATE, "Memory participant has no audio buffer");
      }

      // Write samples to ringbuffer (one at a time)
      size_t written = 0;
      for (size_t j = 0; j < count; j++) {
        if (ringbuffer_write(host->clients[i].incoming_audio, &samples[j])) {
          written++;
        } else {
          break; // Buffer full
        }
      }

      if (written < count) {
        log_warn_every(NS_PER_MS_INT, "Audio ringbuffer full, dropped %zu samples", count - written);
      }

      host->clients[i].audio_active = true;

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Memory participant not found");
}

asciichat_error_t session_host_remove_client(session_host_t *host, uint32_t client_id) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_remove_client: invalid host");
  }

  mutex_lock(&host->clients_mutex);

  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      // Invoke callback before removing
      mutex_unlock(&host->clients_mutex);
      if (host->callbacks.on_client_leave) {
        host->callbacks.on_client_leave(host, client_id, host->user_data);
      }
      mutex_lock(&host->clients_mutex);

      // Close socket
      if (host->clients[i].socket != INVALID_SOCKET_VALUE) {
        socket_close(host->clients[i].socket);
        host->clients[i].socket = INVALID_SOCKET_VALUE;
      }

      // Clean up media buffers
      if (host->clients[i].incoming_video) {
        image_destroy(host->clients[i].incoming_video);
        host->clients[i].incoming_video = NULL;
      }
      if (host->clients[i].incoming_audio) {
        ringbuffer_destroy(host->clients[i].incoming_audio);
        host->clients[i].incoming_audio = NULL;
      }

      host->clients[i].active = false;
      host->client_count--;

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Client not found: %u", client_id);
}

asciichat_error_t session_host_find_client(session_host_t *host, uint32_t client_id, session_host_client_info_t *info) {
  if (!host || !host->initialized || !info) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_find_client: invalid parameter");
  }

  mutex_lock(&host->clients_mutex);

  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      info->client_id = host->clients[i].client_id;
      SAFE_STRNCPY(info->ip_address, host->clients[i].ip_address, sizeof(info->ip_address));
      info->port = host->clients[i].port;
      info->video_active = host->clients[i].video_active;
      info->audio_active = host->clients[i].audio_active;
      info->connected_at = host->clients[i].connected_at;

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Client not found: %u", client_id);
}

int session_host_get_client_count(session_host_t *host) {
  if (!host || !host->initialized) {
    return 0;
  }
  return host->client_count;
}

int session_host_get_client_ids(session_host_t *host, uint32_t *ids, int max_ids) {
  if (!host || !host->initialized || !ids || max_ids <= 0) {
    return 0;
  }

  mutex_lock(&host->clients_mutex);

  int count = 0;
  for (int i = 0; i < host->max_clients && count < max_ids; i++) {
    if (host->clients[i].active) {
      ids[count++] = host->clients[i].client_id;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return count;
}

/* ============================================================================
 * Session Host Broadcast Functions
 * ============================================================================ */

asciichat_error_t session_host_broadcast_frame(session_host_t *host, const char *frame) {
  if (!host || !host->initialized || !frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_broadcast_frame: invalid parameter");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_host_broadcast_frame: not running");
  }

  // Broadcast ASCII frame to all connected clients
  size_t frame_len = strlen(frame) + 1; // Include null terminator
  asciichat_error_t result = ASCIICHAT_OK;

  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].socket != INVALID_SOCKET_VALUE) {
      asciichat_error_t send_result =
          packet_send(host->clients[i].socket, PACKET_TYPE_ASCII_FRAME, (const void *)frame, frame_len);
      if (send_result != ASCIICHAT_OK) {
        log_warn("Failed to send ASCII frame to client %u", host->clients[i].client_id);
        result = send_result; // Store error but continue broadcasting to other clients
      }
    }
  }
  mutex_unlock(&host->clients_mutex);

  return result;
}

asciichat_error_t session_host_send_frame(session_host_t *host, uint32_t client_id, const char *frame) {
  if (!host || !host->initialized || !frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_send_frame: invalid parameter");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_host_send_frame: not running");
  }

  // Send ASCII frame to specific client
  size_t frame_len = strlen(frame) + 1; // Include null terminator

  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].client_id == client_id && host->clients[i].active &&
        host->clients[i].socket != INVALID_SOCKET_VALUE) {
      asciichat_error_t result =
          packet_send(host->clients[i].socket, PACKET_TYPE_ASCII_FRAME, (const void *)frame, frame_len);
      mutex_unlock(&host->clients_mutex);
      return result;
    }
  }
  mutex_unlock(&host->clients_mutex);

  return SET_ERRNO(ERROR_NOT_FOUND, "session_host_send_frame: client %u not found", client_id);
}

/* ============================================================================
 * Session Host Render Thread Functions
 * ============================================================================ */

asciichat_error_t session_host_start_render(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_start_render: invalid host");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_host_start_render: not running");
  }

  if (host->render_thread_running) {
    return ASCIICHAT_OK; // Already running
  }

  // Create audio context for mixing (host mode = true)
  if (!host->audio_ctx) {
    host->audio_ctx = session_audio_create(true);
    if (!host->audio_ctx) {
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create audio context");
    }
  }

  // Create Opus decoder for decoding incoming Opus audio (48kHz)
  if (!host->opus_decoder) {
    host->opus_decoder = opus_codec_create_decoder(48000);
    if (!host->opus_decoder) {
      session_audio_destroy(host->audio_ctx);
      host->audio_ctx = NULL;
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create Opus decoder");
    }
  }

  // Create Opus encoder for encoding mixed audio for broadcast (48kHz, VOIP mode, 24kbps)
  if (!host->opus_encoder) {
    host->opus_encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
    if (!host->opus_encoder) {
      opus_codec_destroy(host->opus_decoder);
      host->opus_decoder = NULL;
      session_audio_destroy(host->audio_ctx);
      host->audio_ctx = NULL;
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create Opus encoder");
    }
  }

  // Spawn render thread
  host->render_thread_running = true;
  if (asciichat_thread_create(&host->render_thread, "render", host_render_thread, host) != 0) {
    log_error("Failed to spawn render thread");
    host->render_thread_running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn render thread");
  }

  log_info("Host render thread started");
  return ASCIICHAT_OK;
}

void session_host_stop_render(session_host_t *host) {
  if (!host || !host->initialized) {
    return;
  }

  if (!host->render_thread_running) {
    return;
  }

  // Signal thread to stop
  host->render_thread_running = false;

  // Wait for thread to complete
  asciichat_thread_join(&host->render_thread, NULL);

  // Clean up audio resources
  if (host->audio_ctx) {
    session_audio_destroy(host->audio_ctx);
    host->audio_ctx = NULL;
  }
  if (host->opus_decoder) {
    opus_codec_destroy(host->opus_decoder);
    host->opus_decoder = NULL;
  }
  if (host->opus_encoder) {
    opus_codec_destroy(host->opus_encoder);
    host->opus_encoder = NULL;
  }

  log_info("Host render thread stopped");
}

/* ============================================================================
 * Session Host Transport Functions (WebRTC Integration)
 * ============================================================================ */

asciichat_error_t session_host_set_client_transport(session_host_t *host, uint32_t client_id,
                                                    acip_transport_t *transport) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Host is NULL or not initialized");
  }

  mutex_lock(&host->clients_mutex);

  // Find client with matching ID
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      log_info("session_host_set_client_transport: Setting transport=%p for client %u (was=%p)", transport, client_id,
               host->clients[i].transport);

      host->clients[i].transport = transport;

      if (transport) {
        log_info("WebRTC transport now active for client %u", client_id);
      } else {
        log_info("WebRTC transport cleared for client %u, reverting to socket", client_id);
      }

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  log_warn("Client %u not found", client_id);
  return SET_ERRNO(ERROR_NOT_FOUND, "Client not found");
}

acip_transport_t *session_host_get_client_transport(session_host_t *host, uint32_t client_id) {
  if (!host || !host->initialized) {
    return NULL;
  }

  mutex_lock(&host->clients_mutex);

  // Find client with matching ID
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      acip_transport_t *transport = host->clients[i].transport;
      mutex_unlock(&host->clients_mutex);
      return transport;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return NULL;
}

bool session_host_client_has_transport(session_host_t *host, uint32_t client_id) {
  if (!host || !host->initialized) {
    return false;
  }

  mutex_lock(&host->clients_mutex);

  // Find client with matching ID
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      bool has_transport = host->clients[i].transport != NULL;
      mutex_unlock(&host->clients_mutex);
      return has_transport;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return false;
}
