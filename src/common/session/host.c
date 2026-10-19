#include <ascii-chat/network/acip/settings.h>
#include <ascii-chat/video/anim/digital_rain.h>
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
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "session/host.h"
#include "session/display.h"
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/client.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/listener.h>
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
  char ip_address[64];
  int port;
  bool active;
  bool video_active;
  bool audio_active;
  uint64_t connected_at;
  uint16_t render_width;
  uint16_t render_height;
  acip_render_settings_t rendering;
  digital_rain_t *rain;
  uint64_t next_video_deadline;
  double rain_time;

  /** @brief Active ACIP transport. Externally supplied transports are borrowed. */
  struct acip_transport *transport;
  acip_transport_ownership_t transport_ownership;

  /** @brief Incoming video frame buffer (for host render thread) */
  image_t *incoming_video;

  /** @brief Incoming audio ringbuffer (written by receive loop, read by render thread) */
  ringbuffer_t *incoming_audio;
} session_host_client_t;

static void host_client_release_transport(session_host_client_t *client) {
  if (client->rain) {
    digital_rain_destroy(client->rain);
    client->rain = NULL;
  }
  if (client->transport_ownership == ACIP_TRANSPORT_OWNED)
    acip_transport_destroy(client->transport);
  client->transport = NULL;
  client->transport_ownership = ACIP_TRANSPORT_BORROWED;
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
  session_host_render_target_t render_target;
  bool display_logging_disabled;
  uint64_t snapshot_first_frame_ns;

  /** @brief Native connection admission; accepted peers are ACIP transports. */
  acip_listener_t *listener;

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
    SET_ERRNO(ERROR_INVALID_PARAM, "NULL config");
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

void session_host_destroy(session_host_t *host) {
  if (!host) {
    return;
  }

  // Stop if running
  if (host->running) {
    session_host_stop(host);
    if (host->running)
      return;
  }

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

  acip_listener_destroy(host->listener);
  host->listener = NULL;

  // Free client array and per-client resources
  if (host->clients) {
    for (int i = 0; i < host->max_clients; i++) {
      host_client_release_transport(&host->clients[i]);
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
}

/* ============================================================================
 * Session Host Server Control Functions
 * ============================================================================ */

static void *accept_loop_thread(void *arg) {
  session_host_t *host = arg;
  while (host->accept_thread_running && host->running) {
    acip_transport_t *transport = NULL;
    char address[64];
    int port = 0;
    asciichat_error_t result = acip_listener_accept(host->listener, &transport, address, sizeof(address), &port);
    if (result != ASCIICHAT_OK) {
      log_error("Transport admission failed: %d", result);
      break;
    }
    if (transport && !session_host_add_client(host, transport, ACIP_TRANSPORT_OWNED, address, port))
      acip_transport_destroy(transport);
    platform_sleep_ms(1);
  }
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
                  host->clients[j].audio_active = true;
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
  case PACKET_TYPE_ACIP_SETTINGS_SYNC: {
    uint8_t reply[ACIP_SETTINGS_MAX_SIZE];
    size_t reply_length = 0;
    asciichat_error_t result = ASCIICHAT_OK;
    session_host_client_t *target = NULL;
    mutex_lock(&host->clients_mutex);
    for (int j = 0; j < host->max_clients; ++j) {
      if (host->clients[j].active && host->clients[j].client_id == client_id) {
        target = &host->clients[j];
        break;
      }
    }
    if (!target)
      result = SET_ERRNO(ERROR_INVALID_STATE, "Unknown settings participant");
    else if (ptype == PACKET_TYPE_CLIENT_CAPABILITIES) {
      if (!data || len != sizeof(terminal_capabilities_packet_t))
        result = SET_ERRNO(ERROR_INVALID_PARAM, "Invalid capabilities");
      else {
        terminal_capabilities_packet_t packet;
        memcpy(&packet, data, sizeof(packet));
        if (target->rendering.width) {
          // Codec advertisements cannot change an established viewer's settings.
          mutex_unlock(&host->clients_mutex);
          return ASCIICHAT_OK;
        }
        result = acip_render_settings_capabilities(&target->rendering, &packet);
        if (result == ASCIICHAT_OK) {
          uint8_t query[ACIP_SETTINGS_HEADER_SIZE];
          size_t query_length;
          acip_settings_header_t header = {.schema = 1, .expected_revision = UINT32_MAX};
          result = acip_settings_encode(&header, "", query, sizeof(query), &query_length);
          if (result == ASCIICHAT_OK)
            result =
                acip_settings_request(&target->rendering, query, query_length, reply, sizeof(reply), &reply_length);
        }
      }
    } else if (!target->rendering.revision)
      result = SET_ERRNO(ERROR_INVALID_STATE, "Settings received before capabilities");
    else
      result = acip_settings_request(&target->rendering, data, len, reply, sizeof(reply), &reply_length);
    if (target && result == ASCIICHAT_OK) {
      target->render_width = target->rendering.width;
      target->render_height = target->rendering.height;
      if (reply_length)
        result = packet_send_via_transport(target->transport, PACKET_TYPE_ACIP_SETTINGS_ACK, reply, reply_length, 0);
    }
    mutex_unlock(&host->clients_mutex);
    return result;
  }

  case PACKET_TYPE_STREAM_STOP:
    log_info("Client %u stopped streaming", client_id);
    mutex_lock(&host->clients_mutex);
    for (int j = 0; j < host->max_clients; j++) {
      if (host->clients[j].client_id == client_id) {
        host->clients[j].video_active = false;
        host->clients[j].audio_active = false;
        break;
      }
    }
    mutex_unlock(&host->clients_mutex);
    break;

  case PACKET_TYPE_PING:
    // Respond with PONG
    log_debug_every(NS_PER_MS_INT, "PING from client %u", client_id);
    acip_send_pong(client_transport);
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

static void *receive_loop_thread(void *arg) {
  session_host_t *host = arg;
  while (host->receive_thread_running && host->running) {
    mutex_lock(&host->clients_mutex);
    for (int i = 0; i < host->max_clients; ++i) {
      session_host_client_t *client = &host->clients[i];
      acip_transport_t *transport = client->transport;
      if (!client->active || !transport || !acip_transport_has_pending_data(transport))
        continue;
      packet_type_t type;
      void *payload = NULL, *allocation = NULL;
      size_t size = 0;
      uint32_t id = client->client_id;
      asciichat_error_t result = packet_receive_via_transport(transport, &type, &payload, &size, &allocation);
      mutex_unlock(&host->clients_mutex);
      if (result == ASCIICHAT_OK)
        session_host_receive_packet(host, id, type, payload, size);
      else if (result != ERROR_NETWORK_TIMEOUT) {
        log_debug("Client %u transport disconnected: %d", id, result);
        session_host_remove_client(host, id);
      }
      buffer_release(allocation);
      mutex_lock(&host->clients_mutex);
    }
    mutex_unlock(&host->clients_mutex);
    platform_sleep_ms(1);
  }
  return NULL;
}

static char *host_create_video_grid(session_host_t *host, int count, int width, int height,
                                    const terminal_capabilities_t *caps, const char *palette, size_t *size) {
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
    sources[index].frame_data = ascii_convert_with_capabilities(host->clients[i].incoming_video, cell_width,
                                                                cell_height, caps, false, false, palette);
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

static void host_render_video(session_host_t *host, bool waiting_for_peer) {
  mutex_lock(&host->clients_mutex);
  int count = 0;
  for (int i = 0; i < host->max_clients; ++i)
    if (host->clients[i].active && host->clients[i].video_active && host->clients[i].incoming_video)
      ++count;

  if (count > 0) {
    typedef struct {
      int width, height;
      terminal_capabilities_t caps;
      const char *palette;
      char *frame;
      size_t size;
    } host_grid_t;
    // Cache each viewport and rendering configuration for this frame.
    host_grid_t *grids = SAFE_CALLOC((size_t)host->max_clients + 1, sizeof(*grids), host_grid_t *);
    int grid_count = 0;
    for (int i = 0; i <= host->max_clients; ++i) {
      bool local_display = i == host->max_clients;
      int width = 80, height = 24;
      terminal_capabilities_t caps = {0};
      const char *palette = NULL;
      session_host_client_t *recipient = local_display ? NULL : &host->clients[i];
      if (local_display) {
        if (waiting_for_peer)
          continue;
        if (host->render_target.client_id) {
          for (int j = 0; j < host->max_clients; ++j) {
            if (host->clients[j].active && host->clients[j].client_id == host->render_target.client_id) {
              recipient = &host->clients[j];
              break;
            }
          }
          if (!recipient)
            continue;
        } else {
          session_host_render_config_t config = {0};
          if (!host->render_target.get_config ||
              host->render_target.get_config(host->render_target.context, &config) != ASCIICHAT_OK || !config.palette ||
              config.width <= 0 || config.height <= 0)
            continue;
          caps = config.caps;
          palette = config.palette;
          width = config.width;
          height = config.height;
        }
      } else {
        if (recipient->client_id == host->render_target.client_id)
          continue;
      }
      if (recipient) {
        if (!recipient->active || !recipient->transport)
          continue;
        if (!recipient->rendering.revision)
          continue;
        caps = recipient->rendering.caps;
        palette = recipient->rendering.palette;
        if (recipient->render_width > 0 && recipient->render_height > 0) {
          width = recipient->render_width;
          height = recipient->render_height;
        }
      }
      if (!palette)
        continue;
      if (!caps.utf8_support) {
        if (caps.render_mode == RENDER_MODE_HALF_BLOCK)
          caps.render_mode = RENDER_MODE_FOREGROUND;
        if (palette_requires_utf8_encoding(palette, strlen(palette)))
          palette = get_builtin_palette(PALETTE_STANDARD)->chars;
      }
      uint64_t now = time_get_ns();
      if (recipient && caps.desired_fps > 0) {
        uint64_t interval = NS_PER_SEC_INT / caps.desired_fps;
        if (recipient->next_video_deadline && now < recipient->next_video_deadline)
          continue;
        if (!recipient->next_video_deadline || now - recipient->next_video_deadline > interval * 2)
          recipient->next_video_deadline = now;
        recipient->next_video_deadline += interval;
      }
      int cached = 0;
      while (cached < grid_count &&
             (grids[cached].width != width || grids[cached].height != height ||
              grids[cached].caps.render_mode != caps.render_mode ||
              grids[cached].caps.color_level != caps.color_level ||
              grids[cached].caps.utf8_support != caps.utf8_support ||
              grids[cached].caps.color_filter != caps.color_filter || strcmp(grids[cached].palette, palette) != 0))
        ++cached;
      if (cached == grid_count) {
        grids[cached].width = width;
        grids[cached].height = height;
        grids[cached].caps = caps;
        grids[cached].palette = palette;
        grids[cached].frame = host_create_video_grid(host, count, width, height, &caps, palette, &grids[cached].size);
        ++grid_count;
      }
      host_grid_t *grid = &grids[cached];
      if (!grid->frame)
        continue;
      char *rain_frame = NULL;
      const char *output = grid->frame;
      size_t output_size = grid->size;
      if (recipient) {
        if (caps.capabilities & TERM_CAP_MATRIX_RAIN) {
          if (recipient->rain && (recipient->rain->num_columns != width || recipient->rain->num_rows != height)) {
            digital_rain_destroy(recipient->rain);
            recipient->rain = NULL;
          }
          if (!recipient->rain) {
            recipient->rain = digital_rain_init(width, height);
            recipient->rain_time = (double)now / NS_PER_SEC_INT;
          }
          if (recipient->rain) {
            double seconds = (double)now / NS_PER_SEC_INT;
            digital_rain_set_color_from_filter(recipient->rain, caps.color_filter);
            rain_frame = digital_rain_apply(recipient->rain, grid->frame, (float)(seconds - recipient->rain_time));
            recipient->rain_time = seconds;
            if (rain_frame) {
              output = rain_frame;
              output_size = strlen(rain_frame);
            }
          }
        } else if (recipient->rain) {
          digital_rain_destroy(recipient->rain);
          recipient->rain = NULL;
        }
      }
      if (!recipient) {
        host->render_target.render_frame(host->render_target.context, output, output_size, width, height);
      } else {
        acip_send_ascii_frame(recipient->transport, &host->frame_workspace, output, output_size, width, height,
                              "session-host");
      }
      SAFE_FREE(rain_frame);
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
  host_render_video(host, false);
  return ASCIICHAT_OK;
}

asciichat_error_t session_host_render_audio(session_host_t *host) {
  if (!host || !host->initialized)
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
  if (!host->opus_decoder)
    host->opus_decoder = opus_codec_create_decoder(48000);
  if (!host->opus_encoder)
    host->opus_encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
  if (!host->opus_decoder || !host->opus_encoder)
    return SET_ERRNO(ERROR_AUDIO, "Failed to initialize host audio codecs");
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
#ifndef __EMSCRIPTEN__
  audio_recording_submit(AUDIO_RECORDING_REMOTE, mixed_audio, 960, time_get_ns());
#endif

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
        asciichat_error_t audio_result =
            acip_send_audio_opus_batch(host->clients[i].transport, opus_buffer, opus_len, frame_sizes, 1, 48000, 20);
        if (audio_result != ASCIICHAT_OK) {
          log_warn("Failed to send mixed audio to client %u", host->clients[i].client_id);
        }
      }
    }
    mutex_unlock(&host->clients_mutex);
  }

  return ASCIICHAT_OK;
}

static void *host_render_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
    return NULL;
  }

  log_info("Host render thread started");

  bool waiting_for_peer = GET_OPTION(detected_mode) == MODE_DISCOVERY && !GET_OPTION(snapshot_mode) &&
                          GET_OPTION(splash_screen) && terminal_is_interactive() && terminal_is_stdout_tty();
  host->snapshot_first_frame_ns = 0;
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
      host_render_video(host, waiting_for_peer);
      last_video_render_ns = now_ns;
    }

    // Complete snapshots only after a local frame, outside the client lock.
    if (host->snapshot_first_frame_ns > 0 &&
        (double)(time_get_ns() - host->snapshot_first_frame_ns) / NS_PER_SEC_INT >= GET_OPTION(snapshot_delay)) {
      APP_CALLBACK_VOID(signal_exit);
      break;
    }

    // Audio consumes one 20 ms Opus frame per tick.
    if (time_elapsed_ns(last_audio_render_ns, now_ns) >= NS_PER_MS_INT * 20) {
      // Mix audio from all participants
      // 1. Read samples from each participant's incoming_audio ringbuffer
      // 2. Mix into output buffer (simple addition with clipping)
      // 3. Encode with Opus
      // 4. Broadcast mixed audio via av_send_audio_opus_batch()

      if (session_host_render_audio(host) != ASCIICHAT_OK)
        log_warn("Host audio rendering failed");

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

asciichat_error_t session_host_start(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
  }

  if (host->running) {
    return ASCIICHAT_OK; // Already running
  }
  if (host->port > UINT16_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Host port is outside the transport endpoint range");

  const char *address = host->ipv4_address;
  asciichat_error_t result =
      acip_listener_open(address[0] ? address : NULL, host->port, host->max_clients, &host->listener);
  if (result != ASCIICHAT_OK) {
    if (host->callbacks.on_error)
      host->callbacks.on_error(host, result, "Failed to open transport listener", host->user_data);
    return result;
  }
  host->running = true;
  host->accept_thread_running = true;
  result = asciichat_thread_create(&host->accept_thread, "host_accept", accept_loop_thread, host);
  if (result != ASCIICHAT_OK) {
    host->accept_thread_running = false;
    session_host_stop(host);
    if (host->callbacks.on_error)
      host->callbacks.on_error(host, ERROR_THREAD, "Failed to start transport admission", host->user_data);
    return SET_ERRNO(ERROR_THREAD, "Failed to start transport admission");
  }
  host->receive_thread_running = true;
  result = asciichat_thread_create(&host->receive_thread, "host_recv", receive_loop_thread, host);
  if (result != ASCIICHAT_OK) {
    host->receive_thread_running = false;
    session_host_stop(host);
    if (host->callbacks.on_error)
      host->callbacks.on_error(host, ERROR_THREAD, "Failed to start transport reception", host->user_data);
    return SET_ERRNO(ERROR_THREAD, "Failed to start transport reception");
  }

  // Spawn render thread (optional - can be started later)
  // This thread handles video mixing and audio distribution
  return ASCIICHAT_OK;
}

void session_host_stop(session_host_t *host) {
  if (!host || !host->initialized || !host->running) {
    return;
  }

  if (acip_listener_stop(host->listener) != ASCIICHAT_OK)
    return;
  host->running = false;

  // Stop render thread if running
  if (host->render_thread_running) {
    host->render_thread_running = false;
    asciichat_thread_join(&host->render_thread, NULL);
    log_info("Render thread joined");
  }

  // Stop transport reception before releasing clients.
  if (host->receive_thread_running) {
    host->receive_thread_running = false;
    asciichat_thread_join(&host->receive_thread, NULL);
    log_info("Receive loop thread joined");
  }

  // Join admission after the listener has stopped.
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

      host_client_release_transport(&host->clients[i]);

      host->clients[i].active = false;
    }
  }
  host->client_count = 0;
  mutex_unlock(&host->clients_mutex);

  acip_listener_destroy(host->listener);
  host->listener = NULL;

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

uint32_t session_host_add_client(session_host_t *host, acip_transport_t *transport,
                                 acip_transport_ownership_t ownership, const char *ip, int port) {
  if (!host || !host->initialized) {
    return 0;
  }

  if ((ownership != ACIP_TRANSPORT_BORROWED && ownership != ACIP_TRANSPORT_OWNED) ||
      (!transport && ownership == ACIP_TRANSPORT_OWNED)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport ownership");
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
      if (ip) {
        SAFE_STRNCPY(host->clients[i].ip_address, ip, sizeof(host->clients[i].ip_address));
      }
      host->clients[i].port = port;
      host->clients[i].active = true;
      host->clients[i].video_active = false;
      host->clients[i].audio_active = false;
      host->clients[i].connected_at = (uint64_t)time(NULL);
      host->clients[i].transport = NULL;
      host->clients[i].transport_ownership = ACIP_TRANSPORT_BORROWED;
      host->clients[i].render_width = 0;
      host->clients[i].render_height = 0;
      memset(&host->clients[i].rendering, 0, sizeof(host->clients[i].rendering));
      host->clients[i].rendering.fps_limit = 60;
      host->clients[i].next_video_deadline = 0;

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

      host->clients[i].transport = transport;
      host->clients[i].transport_ownership = ownership;
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
      host->clients[i].transport_ownership = ACIP_TRANSPORT_BORROWED;

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
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameters");
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
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameters");
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
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
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

      host_client_release_transport(&host->clients[i]);

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
      if (host->render_target.client_id == client_id)
        host->render_target = (session_host_render_target_t){0};

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Client not found: %u", client_id);
}

asciichat_error_t session_host_find_client(session_host_t *host, uint32_t client_id, session_host_client_info_t *info) {
  if (!host || !host->initialized || !info) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameter");
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
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameter");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "not running");
  }

  // Broadcast ASCII frame to all connected clients
  size_t frame_len = strlen(frame);
  asciichat_error_t result = ASCIICHAT_OK;

  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].transport) {
      asciichat_error_t send_result = acip_send_ascii_frame(host->clients[i].transport, &host->frame_workspace, frame,
                                                            frame_len, 80, 24, "session-host");
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
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameter");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "not running");
  }

  // Send ASCII frame to specific client
  size_t frame_len = strlen(frame);

  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].client_id == client_id && host->clients[i].active) {
      asciichat_error_t result = host->clients[i].transport
                                     ? acip_send_ascii_frame(host->clients[i].transport, &host->frame_workspace, frame,
                                                             frame_len, 80, 24, "session-host")
                                     : SET_ERRNO(ERROR_INVALID_STATE, "Client %u has no transport", client_id);
      mutex_unlock(&host->clients_mutex);
      return result;
    }
  }
  mutex_unlock(&host->clients_mutex);

  return SET_ERRNO(ERROR_NOT_FOUND, "client %u not found", client_id);
}

/* ============================================================================
 * Session Host Display Functions
 * ============================================================================ */

asciichat_error_t session_host_set_render_target(session_host_t *host, const session_host_render_target_t *target) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
  }
  if (target) {
    bool has_backend = target->get_config || target->render_frame;
    bool complete_backend = target->get_config && target->render_frame;
    bool valid_target = target->client_id ? !has_backend : complete_backend;
    if (!valid_target)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Local rendering requires a client or a complete display backend");
  }

  mutex_lock(&host->clients_mutex);
  if (target && target->client_id) {
    bool found = false;
    for (int i = 0; i < host->max_clients; ++i)
      if (host->clients[i].active && host->clients[i].client_id == target->client_id)
        found = true;
    if (!found) {
      mutex_unlock(&host->clients_mutex);
      return SET_ERRNO(ERROR_NOT_FOUND, "Local display client not found: %u", target->client_id);
    }
  }
  host->render_target = target ? *target : (session_host_render_target_t){0};
  mutex_unlock(&host->clients_mutex);
  return ASCIICHAT_OK;
}

#ifndef __EMSCRIPTEN__
static asciichat_error_t native_display_config(void *context, session_host_render_config_t *config) {
  session_host_t *host = context;
  const terminal_capabilities_t *caps = session_display_get_caps(host->display_context);
  if (!caps)
    return SET_ERRNO(ERROR_INVALID_STATE, "Local terminal display is not initialized");
  config->caps = *caps;
  config->caps.render_mode = GET_OPTION(render_mode);
  config->caps.wants_background = config->caps.render_mode == RENDER_MODE_BACKGROUND;
  if (GET_OPTION(color_mode) != TERM_COLOR_AUTO)
    config->caps.color_level = GET_OPTION(color_mode);
  config->caps.color_filter = GET_OPTION(color_filter);
  config->palette = session_display_get_palette_chars(host->display_context);
  config->width = GET_OPTION(width) > 0 ? GET_OPTION(width) : 80;
  config->height = GET_OPTION(height) > 0 ? GET_OPTION(height) : 24;
  return ASCIICHAT_OK;
}

static void native_display_frame(void *context, const char *frame, size_t size, int width, int height) {
  (void)size;
  (void)width;
  (void)height;
  session_host_t *host = context;
  if (!host->display_logging_disabled) {
    log_set_terminal_output(false);
    host->display_logging_disabled = true;
  }
  session_display_render_frame(host->display_context, frame);
  if (GET_OPTION(snapshot_mode) && host->snapshot_first_frame_ns == 0 &&
      session_display_has_first_frame(host->display_context))
    host->snapshot_first_frame_ns = time_get_ns();
}

asciichat_error_t session_host_set_display(session_host_t *host, struct session_display_ctx *display) {
  if (!host || !host->initialized)
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
  mutex_lock(&host->clients_mutex);
  host->display_context = display;
  host->render_target = display ? (session_host_render_target_t){.get_config = native_display_config,
                                                                 .render_frame = native_display_frame,
                                                                 .context = host}
                                : (session_host_render_target_t){0};
  mutex_unlock(&host->clients_mutex);
  return ASCIICHAT_OK;
}
#endif

/* ============================================================================
 * Session Host Render Thread Functions
 * ============================================================================ */

asciichat_error_t session_host_start_render(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid host");
  }

  // Discovery mode starts the shared host renderer without passing through
  // server/main.c, which normally initializes the ASCII lookup palettes.
  ascii_simd_init();

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "not running");
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
                                                    acip_transport_t *transport, acip_transport_ownership_t ownership) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Host is NULL or not initialized");
  }

  if ((ownership != ACIP_TRANSPORT_BORROWED && ownership != ACIP_TRANSPORT_OWNED) ||
      (!transport && ownership == ACIP_TRANSPORT_OWNED))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport ownership");

  mutex_lock(&host->clients_mutex);

  // Find client with matching ID
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      log_info("Setting transport=%p for client %u (was=%p)", transport, client_id, host->clients[i].transport);

      if (host->clients[i].transport == transport && host->clients[i].transport_ownership != ownership) {
        mutex_unlock(&host->clients_mutex);
        return SET_ERRNO(ERROR_INVALID_STATE, "Cannot change ownership of a registered transport");
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
      if (transport) {
        asciichat_error_t result = acip_send_server_state(transport, &state);
        if (result != ASCIICHAT_OK) {
          mutex_unlock(&host->clients_mutex);
          return result;
        }
      }
      if (host->clients[i].transport != transport)
        host_client_release_transport(&host->clients[i]);
      host->clients[i].transport = transport;
      host->clients[i].transport_ownership = transport ? ownership : ACIP_TRANSPORT_BORROWED;
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
