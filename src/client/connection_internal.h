#pragma once
#include "connection.h"

#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/fps.h>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/network/connection_attempt.h>
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

typedef enum {
  CLIENT_CONNECTION_STARTING,
  CLIENT_CONNECTION_RUNNING,
  CLIENT_CONNECTION_STOPPING,
  CLIENT_CONNECTION_STOPPED,
} client_connection_state_t;

/* Mutable state belongs to one attempt. Application devices and UI outlive it. */
struct client_connection {
  connection_attempt_context_t attempt;
  int receive_frame_count;
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
  atomic_t state;
  thread_id_t owner_thread;
  bool audio_queue_mutex_initialized;
  bool audio_queue_cond_initialized;
  atomic_t stop_requested;
  socket_t sockfd;
  atomic_t connection_active;
  atomic_t remote_error;
  atomic_t connection_lost;
  uint32_t my_client_id;
  char server_ip[256];
  int server_port;
  mutex_t send_mutex;
  crypto_handshake_context_t crypto_ctx;
  h265_encoder_t *volatile video_encoder;
  atomic_t data_thread_exited;
  atomic_t frames_rendered;
  atomic_t snapshot_first_frame_time_ns;
  uint32_t last_active_count;
  bool server_state_initialized;
  bool should_clear_before_next_frame;
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
  atomic_t audio_sender_should_exit;
  atomic_t audio_sender_exited;
  bool ping_thread_created;
  atomic_t ping_thread_exited;
  lifecycle_t crypto_lc;
  uint8_t crypto_mode;
  thread_pool_t *client_worker_pool;
  client_audio_pipeline_t *audio_pipeline;
};
