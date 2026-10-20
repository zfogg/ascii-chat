#pragma once
#include "connection.h"
#include "session/connection.h"
#include "session/protocol.h"
#include "session/capture.h"
#include "session/display.h"
#include <ascii-chat/audio/audio.h>

#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/fps.h>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/video/h265/encoder.h>

typedef struct {
  uint8_t data[8 * 4000]; // Max 8 frames * ~500 bytes each (with safety margin)
  size_t size;
  uint16_t frame_sizes[8];
  int frame_count;
} audio_send_packet_t;

/** Ring buffer queue for async audio packets */
#define AUDIO_SEND_QUEUE_SIZE 32

/* Mutable state belongs to one attempt. Application devices and UI outlive it. */
struct client_connection {
  network_connection_t *network;
  acip_transport_t *transport;
  // Application resources are borrowed explicitly by this connection.
  session_capture_ctx_t *capture_context;
  session_display_ctx_t *display_context;
  audio_context_t *audio_context;
  atomic_t display_first_frame;
  int audio_receive_count;
  fps_t frame_fps_tracker;
  bool frame_fps_tracker_initialized;
  uint32_t frame_last_width;
  uint32_t frame_last_height;
  uint64_t frame_first_frame_time_ns;
  bool frame_first_frame_recorded;
  int frame_snapshot_frame_count;
  uint64_t frame_last_debug_log_ns;
  bool frame_first_frame_rendered;
  bool send_mutex_initialized;
  session_connection_t lifecycle;
  bool audio_queue_mutex_initialized;
  bool audio_queue_cond_initialized;
  atomic_t stop_requested;
  atomic_t connection_active;
  atomic_t remote_error;
  atomic_t connection_lost;
  uint32_t my_client_id;
  char server_ip[256];
  int server_port;
  mutex_t send_mutex;
  crypto_handshake_context_t *crypto_ctx; // Borrowed from network until its users join.
  h265_encoder_t *volatile video_encoder;
  buffer_workspace_t encoded_video_workspace; // Owned exclusively by the capture worker.
  buffer_workspace_t decoded_ascii_workspace; // Owned exclusively by the receive worker.
  atomic_t data_thread_exited;
  atomic_t frames_rendered;
  atomic_t snapshot_first_frame_time_ns;
  session_protocol_t protocol;
  terminal_size_t advertised_dimensions; // Last dimensions successfully sent to this server.
  bool capture_thread_created;
  atomic_t capture_thread_exited;
  bool audio_capture_thread_created;
  atomic_t audio_capture_thread_exited;
  audio_send_packet_t audio_send_queue[AUDIO_SEND_QUEUE_SIZE];
  int audio_send_queue_head;
  int audio_send_queue_tail;
  mutex_t audio_send_queue_mutex;
  cond_t audio_send_queue_cond;
  lifecycle_t audio_send_queue_lc;
  bool audio_sender_thread_created;
  bool audio_send_direct; // The transport already owns an asynchronous outbound queue.
  atomic_t audio_sender_should_exit;
  atomic_t audio_sender_exited;
  bool ping_thread_created;
  atomic_t ping_thread_exited;
  uint8_t crypto_mode;
  thread_pool_t *client_worker_pool;
  client_audio_pipeline_t *audio_pipeline;
};
