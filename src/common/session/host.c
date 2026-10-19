#include <ascii-chat/common.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/audio/recording.h>
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
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>
#include "session/display.h"
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/client.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common/protocol_constants.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/ringbuffer.h>
#include "session/audio.h"
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/palette.h>

#include <string.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>

/* ============================================================================
 * Session Host Constants
 * ============================================================================ */

/** @brief Default maximum clients */
#define SESSION_HOST_DEFAULT_MAX_CLIENTS PARTICIPANT_LIMIT

/* ============================================================================
 * Session Host Internal Types
 * ============================================================================ */

/**
 * @brief Internal client record structure
 */
typedef struct {
  participant_type_t participant_type; // NETWORK or MEMORY
  uint32_t client_id;
  network_connection_t *connection;
  char ip_address[64];
  int port;
  bool active;
  bool video_active;
  bool audio_active;
  uint64_t connected_at;
  uint16_t render_width;
  uint16_t render_height;
  render_mode_t render_mode;

  /** @brief Transport borrowed from the connection owner or browser bridge. */
  struct acip_transport *transport;

  /** @brief Incoming video frame buffer (for host render thread) */
  image_t *incoming_video;

  /** @brief Incoming audio ringbuffer (written by receive loop, read by render thread) */
  ringbuffer_t *incoming_audio;
} session_host_client_t;

static asciichat_error_t session_host_send_packet(session_host_client_t *client, packet_type_t type,
                                                  const void *payload, size_t payload_len) {
  if (client->transport) {
    return packet_send_via_transport(client->transport, type, payload, payload_len, 0);
  }
  return SET_ERRNO(ERROR_INVALID_STATE, "Session host client has no active transport");
}

/* ============================================================================
 * Session Host Context Structure
 * ============================================================================ */

/**
 * @brief Internal session host structure
 *
 * Contains server state, client list, and callback configuration.
 */
typedef struct session_host {
  acip_frame_workspace_t frame_workspace; // Protected by clients_mutex.
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

  /** @brief Display context for local frame rendering (optional, can be NULL) */
  struct session_display_ctx *display_context;

  /** @brief IPv4 listen socket */
  network_server_t *network_server;

  /** @brief IPv6 listen socket */
  asciichat_thread_t network_thread;
  bool network_thread_started;

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
  host->running = false;

  // Allocate client array
  host->clients = SAFE_CALLOC((size_t)host->max_clients, sizeof(session_host_client_t), session_host_client_t *);

  host->client_count = 0;
  host->next_client_id = 1;

  // Initialize mutex
  if (mutex_init(&host->clients_mutex, "host_clients") != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize clients mutex");
    SAFE_FREE(host->clients);
    acip_frame_workspace_destroy(&host->frame_workspace);
    SAFE_FREE(host);
    return NULL;
  }

  host->initialized = true;
  return host;
}

asciichat_error_t session_host_destroy(session_host_t *host) {
  if (!host)
    return ASCIICHAT_OK;
  asciichat_error_t result = session_host_stop(host);
  if (result != ASCIICHAT_OK)
    return result;

  // Clean up audio resources
  if (host->audio_ctx) {
#ifndef __EMSCRIPTEN__
    session_audio_destroy(host->audio_ctx);
#endif
    host->audio_ctx = NULL;
  }
  if (host->opus_decoder) {
    opus_codec_destroy(host->opus_decoder);
    host->opus_decoder = NULL;
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
  acip_frame_workspace_destroy(&host->frame_workspace);
  SAFE_FREE(host);
  return ASCIICHAT_OK;
}

/* ============================================================================
 * Session Host Server Control Functions
 * ============================================================================ */

asciichat_error_t session_host_receive_packet(session_host_t *host, uint32_t client_id, packet_type_t ptype,
                                              const void *data, size_t len) {
  if (!host || !host->initialized || (!data && len))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid host packet");
  acip_transport_t *client_transport = session_host_get_client_transport(host, client_id);
  switch (ptype) {
  case PACKET_TYPE_IMAGE_FRAME:
    // Client sent a video frame - parse and store in incoming buffer
    if (data && len >= sizeof(image_frame_packet_t)) {
      const image_frame_packet_t *frame_hdr = (const image_frame_packet_t *)data;
      const uint8_t *pixel_data = (const uint8_t *)data + sizeof(image_frame_packet_t);
      size_t pixel_data_size = len - sizeof(image_frame_packet_t);
      uint32_t width = NET_TO_HOST_U32(frame_hdr->width);
      uint32_t height = NET_TO_HOST_U32(frame_hdr->height);
      uint32_t pixel_format = NET_TO_HOST_U32(frame_hdr->pixel_format);
      size_t bytes_per_pixel = (pixel_format == PIXEL_FORMAT_RGBA || pixel_format == PIXEL_FORMAT_BGRA) ? 4 : 3;
      size_t expected_size = 0;
      size_t pixel_count = 0;

      if (pixel_format < PIXEL_FORMAT_RGB || pixel_format > PIXEL_FORMAT_BGRA || width == 0 || height == 0 ||
          width > INT_MAX || height > INT_MAX ||
          checked_size_mul((size_t)width, (size_t)height, &pixel_count) != ASCIICHAT_OK ||
          checked_size_mul(pixel_count, bytes_per_pixel, &expected_size) != ASCIICHAT_OK ||
          expected_size != pixel_data_size) {
        log_warn("Discarding malformed video frame from client %u (%ux%u, format %u, payload %zu)", client_id, width,
                 height, pixel_format, pixel_data_size);
      } else {
        // Frame dimensions and pixel format are network-order fields. Resize
        // this participant's image when its capture dimensions change.
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].active && host->clients[j].client_id == client_id && host->clients[j].incoming_video) {
            image_t *img = host->clients[j].incoming_video;
            if (img->w != (int)width || img->h != (int)height) {
              image_t *resized = image_new(width, height);
              if (!resized) {
                log_warn("Could not allocate %ux%u video frame for client %u", width, height, client_id);
                break;
              }
              image_destroy(img);
              host->clients[j].incoming_video = img = resized;
            }

            uint8_t *dst = (uint8_t *)img->pixels;
            if (pixel_format == PIXEL_FORMAT_RGB) {
              memcpy(dst, pixel_data, pixel_count * 3);
            } else if (pixel_format == PIXEL_FORMAT_BGR) {
              for (size_t p = 0; p < pixel_count; p++) {
                dst[p * 3] = pixel_data[p * 3 + 2];
                dst[p * 3 + 1] = pixel_data[p * 3 + 1];
                dst[p * 3 + 2] = pixel_data[p * 3];
              }
            } else {
              const bool bgra = pixel_format == PIXEL_FORMAT_BGRA;
              for (size_t p = 0; p < pixel_count; p++) {
                const uint8_t *src = pixel_data + p * 4;
                dst[p * 3] = src[bgra ? 2 : 0];
                dst[p * 3 + 1] = src[1];
                dst[p * 3 + 2] = src[bgra ? 0 : 2];
              }
            }
            log_debug_every(500 * US_PER_MS_INT, "Frame received from client %u (%ux%u, format %u)", client_id, width,
                            height, pixel_format);
            break;
          }
        }
        mutex_unlock(&host->clients_mutex);
      }
    }
    break;

  case PACKET_TYPE_AUDIO_OPUS_BATCH:
    // Client sent Opus-encoded audio batch
    if (data && len > 16) { // Must have at least header
      const uint8_t *batch_data = (const uint8_t *)data;
      // Parse header: sample_rate (4), frame_duration (4), frame_count (4), reserved (4)
      (void)batch_data[0]; // Avoid unused variable warning if we don't use sample_rate/frame_duration
      uint32_t batch_frame_count_net = 0;
      memcpy(&batch_frame_count_net, batch_data + 8, sizeof(batch_frame_count_net));
      uint32_t batch_frame_count = NET_TO_HOST_U32(batch_frame_count_net);

      size_t frame_sizes_len = (size_t)batch_frame_count * sizeof(uint16_t);
      if (batch_frame_count > 0 && batch_frame_count <= 1000 && 16 + frame_sizes_len <= len) {
        const uint8_t *frame_sizes = batch_data + 16;
        const uint8_t *opus_frames = batch_data + 16 + (batch_frame_count * sizeof(uint16_t));
        const uint8_t *batch_end = batch_data + len;

        // Find client and decode audio
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].client_id == client_id && host->clients[j].incoming_audio && host->opus_decoder) {
            // Decode each Opus frame and write to ringbuffer
            const uint8_t *current_frame = opus_frames;
            for (uint32_t k = 0; k < batch_frame_count; k++) {
              uint16_t frame_size_net = 0;
              memcpy(&frame_size_net, frame_sizes + k * sizeof(frame_size_net), sizeof(frame_size_net));
              uint16_t frame_size = NET_TO_HOST_U16(frame_size_net);
              if ((size_t)(batch_end - current_frame) < frame_size) {
                break;
              }
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

  case PACKET_TYPE_CLIENT_CAPABILITIES:
    if (data && len >= sizeof(terminal_capabilities_packet_t)) {
      terminal_capabilities_packet_t caps;
      memcpy(&caps, data, sizeof(caps));
      uint16_t width = NET_TO_HOST_U16(caps.width);
      uint16_t height = NET_TO_HOST_U16(caps.height);
      if (width > 0 && height > 0) {
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].active && host->clients[j].client_id == client_id) {
            host->clients[j].render_width = width;
            host->clients[j].render_height = height;
            uint32_t render_mode = NET_TO_HOST_U32(caps.render_mode);
            host->clients[j].render_mode =
                render_mode <= RENDER_MODE_HALF_BLOCK ? (render_mode_t)render_mode : RENDER_MODE_FOREGROUND;
            log_info("Client %u requested render dimensions %ux%u", client_id, width, height);
            break;
          }
        }
        mutex_unlock(&host->clients_mutex);
      }
    }
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
    if (client_transport) {
      packet_send_via_transport(client_transport, PACKET_TYPE_PONG, NULL, 0, 0);
    }
    break;

  case PACKET_TYPE_CLIENT_LEAVE:
    log_info("Client %u requested disconnect", client_id);
    // Mark for removal (will be handled separately to avoid deadlock)
    break;

  default:
    log_warn("Unknown packet type %u from client %u", ptype, client_id);
    break;
  }

  return ASCIICHAT_OK;
}

static char *host_create_video_grid(session_host_t *host, int count, int width, int height, render_mode_t render_mode,
                                    size_t *size) {
  int columns, rows;
  if (ascii_grid_layout(count, width, height, &columns, &rows) != ASCIICHAT_OK)
    return NULL;
  int cell_width = (width - columns + 1) / columns;
  int cell_height = (height - rows + 1) / rows;
  // Match ascii_create_grid's first-source fallback for very small viewports.
  bool fallback = count > 1 && (cell_width < 10 || cell_height < 3);
  if (fallback) {
    cell_width = width;
    cell_height = height;
  }
  ascii_frame_source_t *sources = SAFE_CALLOC((size_t)count, sizeof(*sources), ascii_frame_source_t *);
  int index = 0;
  for (int i = 0; i < host->max_clients; ++i) {
    if (!host->clients[i].active || !host->clients[i].video_active || !host->clients[i].incoming_video)
      continue;
    uint64_t start = time_get_ns();
    terminal_capabilities_t caps = {0};
    caps.color_level = TERM_COLOR_TRUECOLOR;
    caps.utf8_support = true;
    caps.render_mode = render_mode;
    caps.wants_background = render_mode == RENDER_MODE_BACKGROUND;
    sources[index].frame_data = ascii_convert_with_capabilities(
        host->clients[i].incoming_video, cell_width, cell_height, &caps, false, false, PALETTE_CHARS_STANDARD);
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_ASCII_CONVERT, time_get_ns() - start);
    stats_counter_add(stats_runtime_scope(),
                      sources[index].frame_data ? STATS_COUNTER_FRAMES_CONVERTED : STATS_COUNTER_FRAMES_DROPPED, 1);
    sources[index].frame_size = sources[index].frame_data ? strlen(sources[index].frame_data) : 0;
    ++index;
    if (fallback)
      break;
  }
  char *grid = ascii_create_grid(sources, count, width, height, size);
  for (int i = 0; i < count; ++i)
    SAFE_FREE(sources[i].frame_data);
  SAFE_FREE(sources);
  return grid;
}

static void host_render_video(session_host_t *host, bool waiting_for_peer, bool *logging_disabled_ptr,
                              uint64_t *snapshot_first_frame_ns_ptr) {
  mutex_lock(&host->clients_mutex);
  int count = 0;
  for (int i = 0; i < host->max_clients; ++i)
    if (host->clients[i].active && host->clients[i].video_active && host->clients[i].incoming_video)
      ++count;

  if (count > 0) {
    typedef struct {
      int width, height;
      render_mode_t render_mode;
      char *frame;
      size_t size;
    } host_grid_t;
    // Cache every distinct viewport for this frame, including the local display.
    host_grid_t *grids = SAFE_CALLOC((size_t)host->max_clients + 1, sizeof(*grids), host_grid_t *);
    int grid_count = 0;
    for (int i = 0; i <= host->max_clients; ++i) {
      bool local_display = i == host->max_clients;
      int width = 80, height = 24;
      render_mode_t render_mode = local_display ? GET_OPTION(render_mode) : host->clients[i].render_mode;
      if (local_display) {
        if (!host->display_context || waiting_for_peer)
          continue;
      } else {
        if (!host->clients[i].active || !host->clients[i].transport || !host->clients[i].render_width ||
            !host->clients[i].render_height)
          continue;
        if (host->clients[i].transport && host->clients[i].render_width > 0 && host->clients[i].render_height > 0) {
          width = host->clients[i].render_width;
          height = host->clients[i].render_height;
        }
      }
      int cached = 0;
      while (cached < grid_count && (grids[cached].width != width || grids[cached].height != height ||
                                     grids[cached].render_mode != render_mode))
        ++cached;
      if (cached == grid_count) {
        grids[cached].width = width;
        grids[cached].height = height;
        grids[cached].render_mode = render_mode;
        grids[cached].frame = host_create_video_grid(host, count, width, height, render_mode, &grids[cached].size);
        ++grid_count;
      }
      host_grid_t *grid = &grids[cached];
      if (!grid->frame)
        continue;
      if (local_display) {
#ifndef __EMSCRIPTEN__
        if (!*logging_disabled_ptr) {
          log_set_terminal_output(false);
          *logging_disabled_ptr = true;
        }
        session_display_render_frame(host->display_context, grid->frame);
        if (GET_OPTION(snapshot_mode) && *snapshot_first_frame_ns_ptr == 0 &&
            session_display_has_first_frame(host->display_context))
          *snapshot_first_frame_ns_ptr = time_get_ns();
#endif
      } else if (host->clients[i].transport) {
        acip_send_ascii_frame(host->clients[i].transport, &host->frame_workspace, grid->frame, grid->size, width,
                              height, "session-host");
      } else {
        session_host_send_packet(&host->clients[i], PACKET_TYPE_ASCII_FRAME, grid->frame, grid->size);
      }
    }
    for (int i = 0; i < grid_count; ++i)
      SAFE_FREE(grids[i].frame);
    SAFE_FREE(grids);
  }
  mutex_unlock(&host->clients_mutex);
}

asciichat_error_t session_host_render_video(session_host_t *host) {
  if (!host || !host->initialized)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session host");
  bool logging_disabled = false;
  uint64_t snapshot_first_frame_ns = 0;
  host_render_video(host, false, &logging_disabled, &snapshot_first_frame_ns);
  return ASCIICHAT_OK;
}

static void *host_render_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host) {
    SET_ERRNO(ERROR_INVALID_PARAM, "host_render_thread: invalid host");
    return NULL;
  }

  log_info("Host render thread started");

  bool waiting_for_peer = GET_OPTION(detected_mode) == MODE_DISCOVERY && !GET_OPTION(snapshot_mode) &&
                          GET_OPTION(splash_screen) && terminal_is_interactive() && terminal_is_stdout_tty();
  bool logging_disabled = false;
  uint64_t snapshot_first_frame_ns = 0;
  uint64_t last_video_render_ns = 0;
  uint64_t last_audio_render_ns = 0;

  while (host->render_thread_running && host->running) {
    uint64_t now_ns = time_get_ns();
    if (waiting_for_peer) {
      bool peer_ready = false;
      mutex_lock(&host->clients_mutex);
      for (int i = 0; i < host->max_clients; ++i) {
        session_host_client_t *client = &host->clients[i];
        // Capabilities establish protocol readiness without requiring camera frames.
        if (client->active && client->participant_type == PARTICIPANT_TYPE_NETWORK && client->render_width > 0 &&
            client->render_height > 0) {
          peer_ready = true;
          break;
        }
      }
      mutex_unlock(&host->clients_mutex);
      if (peer_ready) {
        splash_intro_done();
        splash_wait_for_animation();
        waiting_for_peer = false;
      }
    }

    // VIDEO RENDERING (60 FPS = 16.7ms)
    if (time_elapsed_ns(last_video_render_ns, now_ns) >= NS_PER_MS_INT * 16) {
      host_render_video(host, waiting_for_peer, &logging_disabled, &snapshot_first_frame_ns);
      last_video_render_ns = now_ns;
    }

    // Complete snapshots only after a local frame, outside the client lock.
    if (snapshot_first_frame_ns > 0 &&
        (double)(time_get_ns() - snapshot_first_frame_ns) / NS_PER_SEC_INT >= GET_OPTION(snapshot_delay)) {
      APP_CALLBACK_VOID(signal_exit);
      break;
    }

    // AUDIO RENDERING (100 FPS = 10ms)
    if (time_elapsed_ns(last_audio_render_ns, now_ns) >= NS_PER_MS_INT * 10) {
      // Mix audio from all participants
      // 1. Read samples from each participant's incoming_audio ringbuffer
      // 2. Mix into output buffer (simple addition with clipping)
      // 3. Encode with Opus
      // 4. Broadcast mixed audio via av_send_audio_opus_batch()

      if (host->audio_ctx && host->opus_encoder) {
        uint64_t mix_start = time_get_ns();
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

        stats_duration_record(stats_runtime_scope(), STATS_DURATION_AUDIO_MIX, time_get_ns() - mix_start);
        audio_recording_submit(AUDIO_RECORDING_REMOTE, mixed_audio, 960, time_get_ns());

        // Encode to Opus
        uint8_t opus_buffer[1000];
        size_t opus_len = opus_codec_encode(host->opus_encoder, mixed_audio, 960, opus_buffer, sizeof(opus_buffer));

        // Only broadcast audio if we have network-connected clients (not memory participants)
        if (opus_len > 0) {
          uint16_t frame_sizes[1] = {(uint16_t)opus_len};
          mutex_lock(&host->clients_mutex);
          for (int i = 0; i < host->max_clients; i++) {
            if (!host->clients[i].active) {
              continue;
            }
            if (host->clients[i].transport) {
              asciichat_error_t audio_result = acip_send_audio_opus_batch(host->clients[i].transport, opus_buffer,
                                                                          opus_len, frame_sizes, 1, 48000, 20);
              if (audio_result != ASCIICHAT_OK) {
                log_warn("Failed to send mixed audio to WebRTC client %u", host->clients[i].client_id);
              }
            }
          }
          mutex_unlock(&host->clients_mutex);
        }
      }

      log_debug_every(NS_PER_MS_INT, "Audio render cycle");
      last_audio_render_ns = now_ns;
    }

    // Small sleep to prevent busy-loop
    APP_CALLBACK_VOID(platform_pump_events);
    platform_sleep_ms(1);
  }

  log_info("Host render thread stopped");
  return NULL;
}

#ifndef __EMSCRIPTEN__
typedef struct {
  uint32_t client_id;
  network_negotiation_t negotiation;
  private_key_t identity;
} host_connection_t;
static asciichat_error_t host_connection_open(network_connection_t *connection, void *arg) {
  session_host_t *host = arg;
  host_connection_t *client = SAFE_CALLOC(1, sizeof(*client), host_connection_t *);
  if (!client)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate host connection");
  network_connection_set_context(connection, client);
  client->client_id =
      session_host_add_client(host, network_connection_transport(connection), network_connection_peer(connection), 0);
  if (!client->client_id)
    return SET_ERRNO(ERROR_SESSION_FULL, "Host has no participant capacity");
  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; ++i)
    if (host->clients[i].client_id == client->client_id)
      host->clients[i].connection = connection;
  mutex_unlock(&host->clients_mutex);
  if (network_connection_state(connection) == NETWORK_READY)
    return ASCIICHAT_OK;
  asciichat_error_t result = network_connection_prepare_security(connection, true, host->password);
  if (result != ASCIICHAT_OK)
    return result;
  crypto_handshake_context_t *ctx = network_connection_handshake(connection);
  client->identity = ctx->server_private_key;
  client->negotiation = (network_negotiation_t){.protocol = NETWORK_PROTOCOL_MEDIA,
                                                .handshake = ctx,
                                                .identity_keys = &client->identity,
                                                .identity_key_count = client->identity.type == KEY_TYPE_ED25519 ? 1 : 0,
                                                .whitelist = ctx->client_whitelist,
                                                .whitelist_count = ctx->num_whitelisted_clients,
                                                .initialized = true,
                                                .password = host->password,
                                                .secure = network_connection_secure(connection),
                                                .require_auth = ctx->has_password || ctx->require_client_auth,
                                                .require_identity = ctx->require_client_auth,
                                                .require_encryption = GET_OPTION(encrypt_enabled),
                                                .forbid_encryption = GET_OPTION(no_encrypt)};
  return ASCIICHAT_OK;
}
static asciichat_error_t host_connection_packet(network_connection_t *connection, packet_type_t type,
                                                const void *payload, size_t length, void *arg) {
  host_connection_t *client = network_connection_context(connection);
  if (network_connection_state(connection) == NETWORK_NEGOTIATING) {
    asciichat_error_t result = network_negotiation_receive(
        &client->negotiation, network_connection_transport(connection), type, payload, length);
    if (result == ASCIICHAT_OK && client->negotiation.ready)
      network_connection_ready(connection);
    return result;
  }
  return session_host_receive_packet(arg, client->client_id, type, payload, length);
}
static asciichat_error_t host_connection_closing(network_connection_t *connection, void *arg) {
  session_host_t *host = arg;
  host_connection_t *client = network_connection_context(connection);
  if (!client || !client->client_id)
    return ASCIICHAT_OK;
  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; ++i)
    if (host->clients[i].client_id == client->client_id)
      host->clients[i].connection = NULL;
  mutex_unlock(&host->clients_mutex);
  return session_host_remove_client(host, client->client_id);
}
static void host_connection_closed(network_connection_t *connection, void *arg) {
  (void)arg;
  host_connection_t *client = network_connection_context(connection);
  SAFE_FREE(client);
}
static void *host_network_run(void *arg) {
  session_host_t *host = arg;
  network_server_run(host->network_server);
  return NULL;
}
#endif
asciichat_error_t session_host_start(session_host_t *host) {
  if (!host || !host->initialized)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session host");
  if (host->running)
    return ASCIICHAT_OK;
#ifdef __EMSCRIPTEN__
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Browser hosts accept browser-managed connections");
#else
  network_server_config_t config = {
      .port = host->port,
      .ipv4_address = host->ipv4_address[0] ? host->ipv4_address : "0.0.0.0",
      .bind_ipv4 = true,
      .max_connections = host->max_clients,
      .partial_timeout_ms = 30000,
      .callbacks = {host_connection_open, host_connection_packet, host_connection_closing, host_connection_closed},
      .user_data = host};
  asciichat_error_t result = network_server_create(&config, &host->network_server);
  if (result == ASCIICHAT_OK)
    result = network_server_start(host->network_server);
  if (result == ASCIICHAT_OK)
    result = asciichat_thread_create(&host->network_thread, "host_network", host_network_run, host);
  if (result != ASCIICHAT_OK) {
    network_server_destroy(&host->network_server);
    return result;
  }
  host->network_thread_started = true;
  host->running = true;
  return ASCIICHAT_OK;
#endif
}
asciichat_error_t session_host_stop(session_host_t *host) {
  if (!host || !host->initialized)
    return ASCIICHAT_OK;
  if (host->render_thread_running) {
    host->render_thread_running = false;
    asciichat_error_t result = asciichat_thread_join(&host->render_thread, NULL);
    if (result != ASCIICHAT_OK) {
      host->render_thread_running = true;
      return result;
    }
  }
#ifndef __EMSCRIPTEN__
  network_server_request_stop(host->network_server);
  if (host->network_thread_started) {
    asciichat_error_t result = asciichat_thread_join(&host->network_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    host->network_thread_started = false;
  }
  asciichat_error_t result = network_server_stop(host->network_server);
  if (result != ASCIICHAT_OK)
    return result;
  network_server_destroy(&host->network_server);
#endif
  host->running = false;
  return ASCIICHAT_OK;
}
asciichat_error_t session_host_accept_peer(session_host_t *host, acip_transport_t *transport) {
#ifndef __EMSCRIPTEN__
  if (!host || !host->network_server)
    return SET_ERRNO(ERROR_INVALID_STATE, "Host network is not running");
  return network_server_adopt(host->network_server, transport, "peer", true);
#else
  (void)host;
  (void)transport;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Browser peers use the browser host bridge");
#endif
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

uint32_t session_host_add_client(session_host_t *host, acip_transport_t *transport, const char *ip, int port) {
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
      host->clients[i].connection = NULL;
      if (ip) {
        SAFE_STRNCPY(host->clients[i].ip_address, ip, sizeof(host->clients[i].ip_address));
      }
      host->clients[i].port = port;
      host->clients[i].active = true;
      host->clients[i].video_active = false;
      host->clients[i].audio_active = false;
      host->clients[i].connected_at = (uint64_t)time(NULL);
      host->clients[i].transport = transport;
      host->clients[i].render_width = 0;
      host->clients[i].render_height = 0;
      host->clients[i].render_mode = RENDER_MODE_FOREGROUND;

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
      SAFE_STRNCPY(host->clients[i].ip_address, "memory", sizeof(host->clients[i].ip_address));
      host->clients[i].port = 0;
      host->clients[i].active = true;
      host->clients[i].video_active = true; // Memory participants always have active video/audio
      host->clients[i].audio_active = true;
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

#ifndef __EMSCRIPTEN__
      if (host->clients[i].connection) {
        network_connection_request_stop(host->clients[i].connection);
        mutex_unlock(&host->clients_mutex);
        return ASCIICHAT_OK;
      }
#endif

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
    if (host->clients[i].active && host->clients[i].transport) {
      asciichat_error_t send_result = acip_send_ascii_frame(host->clients[i].transport, &host->frame_workspace, frame,
                                                            frame_len - 1, 80, 24, "session-host");
      if (send_result != ASCIICHAT_OK) {
        log_warn("Failed to send ASCII frame to client %u", host->clients[i].client_id);
        result = send_result;
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
    if (host->clients[i].client_id == client_id && host->clients[i].active) {
      asciichat_error_t result = host->clients[i].transport
                                     ? acip_send_ascii_frame(host->clients[i].transport, &host->frame_workspace, frame,
                                                             frame_len - 1, 80, 24, "session-host")
                                     : ERROR_NOT_FOUND;
      mutex_unlock(&host->clients_mutex);
      return result;
    }
  }
  mutex_unlock(&host->clients_mutex);

  return SET_ERRNO(ERROR_NOT_FOUND, "session_host_send_frame: client %u not found", client_id);
}

/* ============================================================================
 * Session Host Display Functions
 * ============================================================================ */

asciichat_error_t session_host_set_display(session_host_t *host, struct session_display_ctx *display) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_set_display: invalid host");
  }

  host->display_context = display;
  return ASCIICHAT_OK;
}

/* ============================================================================
 * Session Host Render Thread Functions
 * ============================================================================ */

asciichat_error_t session_host_start_render(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_start_render: invalid host");
  }

  // Discovery mode starts the shared host renderer without passing through
  // server/main.c, which normally initializes the ASCII lookup palettes.
  ascii_simd_init();

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
#ifndef __EMSCRIPTEN__
      session_audio_destroy(host->audio_ctx);
#endif
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
#ifndef __EMSCRIPTEN__
      session_audio_destroy(host->audio_ctx);
#endif
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
#ifndef __EMSCRIPTEN__
    session_audio_destroy(host->audio_ctx);
#endif
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

      server_state_packet_t state = {0};
      if (transport) {
        uint32_t active_count = 0;
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].active) {
            active_count++;
          }
        }
        state.connected_client_count = HOST_TO_NET_U32(active_count);
        state.active_client_count = HOST_TO_NET_U32(active_count);
      }
      mutex_unlock(&host->clients_mutex);

      if (transport) {
        // Discovery sessions bypass the native server handshake. Notify the
        // browser after registering its transport so it can start media.
        asciichat_error_t send_result = acip_send_server_state(transport, &state);
        if (send_result != ASCIICHAT_OK) {
          return send_result;
        }
      }
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
