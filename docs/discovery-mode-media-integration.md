# Discovery Mode Media Integration Design

## Architecture Overview

Discovery mode media integration follows the existing ascii-chat patterns to maximize code reuse and maintain consistency. The architecture cleanly separates **participant-side capture** from **host-side rendering** through the packet protocol.

### Key Design Principles

1. **Reuse, don't duplicate** - Leverage existing `session_capture_ctx_t`, mixer APIs, and frame generation logic
2. **Per-role threading** - Participants: capture threads | Host: render threads
3. **Atomic state** - No expensive locking in media hot paths
4. **Clean packet protocol** - VIDEO_FRAME and AUDIO packets define the contract
5. **Memory efficient** - Use buffer pools and aligned allocation like existing code

---

## Part 1: Participant-Side Media Capture

### Design: Reuse session_capture_ctx_t

Rather than creating new media APIs, participants will use the existing unified media source abstraction:

```c
// Inside session_participant struct (new fields)
struct session_participant {
  // ... existing fields ...

  // Media capture contexts (reuse existing abstractions)
  session_capture_ctx_t *video_capture;  // NULL if disabled
  session_audio_capture_t *audio_capture;  // NULL if disabled

  // Media thread handles
  asciichat_thread_t video_capture_thread;
  asciichat_thread_t audio_capture_thread;
  bool video_capture_running;
  bool audio_capture_running;
};
```

### Video Capture Thread

```c
/**
 * @brief Video capture thread - captures and transmits frames to host
 *
 * PATTERN (similar to client_video_capture in src/client/capture.c):
 * 1. Read frame from webcam via session_capture_read_frame()
 * 2. Process for transmission (resize to bandwidth-efficient size)
 * 3. Send IMAGE_FRAME packet to host
 * 4. Sleep to maintain target FPS (60 for discovery mode)
 * 5. Repeat until stopped
 *
 * REUSE:
 * - session_capture_ctx_t handles all media sources (webcam, file, test)
 * - Automatic FPS limiting via session_capture_sleep_for_fps()
 * - Network-optimal resizing via session_capture_process_for_transmission()
 */
static void *participant_video_capture_thread(void *arg) {
  session_participant_t *p = (session_participant_t *)arg;

  log_info("Video capture started");

  while (p->video_capture_running && p->connected) {
    // Capture frame (blocks until available)
    image_t *raw_frame = session_capture_read_frame(p->video_capture);
    if (!raw_frame) {
      continue;
    }

    // Process for network transmission (resize if needed)
    image_t *processed = session_capture_process_for_transmission(p->video_capture, raw_frame);

    // Send to host via IMAGE_FRAME packet
    if (processed) {
      asciichat_error_t err = packet_send_image_frame(p->socket, p->client_id, processed->data,
                                                       processed->width, processed->height);
      if (err != ASCIICHAT_OK) {
        log_warn("Failed to send video frame: %d", err);
      }
      image_destroy(processed);
    }

    // Sleep to maintain frame rate (adaptive FPS)
    session_capture_sleep_for_fps(p->video_capture);
  }

  log_info("Video capture stopped");
  return NULL;
}
```

### Audio Capture Thread

```c
/**
 * @brief Audio capture thread - captures and transmits audio to host
 *
 * PATTERN (similar to src/client/audio.c):
 * 1. Read audio samples from microphone (20ms chunks)
 * 2. Process through audio pipeline (AEC, filters, Opus encode)
 * 3. Queue AUDIO packets for asynchronous transmission
 * 4. Repeat until stopped
 *
 * REUSE:
 * - session_audio_capture_t abstracts PortAudio
 * - audio_encode_opus() handles compression
 * - Async queueing prevents blocking the capture thread
 */
static void *participant_audio_capture_thread(void *arg) {
  session_participant_t *p = (session_participant_t *)arg;

  log_info("Audio capture started");

  float sample_buffer[960];  // 20ms @ 48kHz
  uint8_t opus_buffer[1000];

  while (p->audio_capture_running && p->connected) {
    // Read microphone samples (20ms)
    int samples_read = session_audio_capture_read(p->audio_capture, sample_buffer, 960);
    if (samples_read <= 0) {
      continue;
    }

    // Encode to Opus (lossy compression for bandwidth)
    int opus_len = audio_encode_opus(sample_buffer, samples_read, opus_buffer, sizeof(opus_buffer));
    if (opus_len > 0) {
      asciichat_error_t err = packet_send_audio_opus(p->socket, p->client_id, opus_buffer, opus_len);
      if (err != ASCIICHAT_OK) {
        log_warn("Failed to send audio packet: %d", err);
      }
    }
  }

  log_info("Audio capture stopped");
  return NULL;
}
```

### Participant API Additions

```c
/**
 * @brief Start video capture and transmission
 * @param p Participant handle
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t session_participant_start_video_capture(session_participant_t *p);

/**
 * @brief Stop video capture
 * @param p Participant handle
 */
void session_participant_stop_video_capture(session_participant_t *p);

/**
 * @brief Start audio capture and transmission
 * @param p Participant handle
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t session_participant_start_audio_capture(session_participant_t *p);

/**
 * @brief Stop audio capture
 * @param p Participant handle
 */
void session_participant_stop_audio_capture(session_participant_t *p);
```

---

## Part 2: Host-Side Media Rendering

### Design: Reuse create_mixed_ascii_frame_for_client Pattern

The host needs to:
1. Collect incoming video frames from all participants
2. Generate mixed ASCII frame (grid layout)
3. Broadcast back to all participants for display
4. Mix audio and transmit for playback

This mirrors the server mode pipeline but adapted for P2P.

### Host Structure Additions

```c
struct session_host {
  // ... existing fields ...

  // Media rendering state
  asciichat_thread_t render_thread;      // Single thread handles video+audio
  bool render_thread_running;

  // Per-participant incoming media buffers
  // (allocated in client management)
};

// Extended client structure (in host.c)
typedef struct {
  // ... existing fields ...

  // Incoming video buffer (written by receive_loop_thread)
  video_frame_buffer_t *incoming_video;

  // Incoming audio buffer (written by receive_loop_thread)
  ringbuffer_t *incoming_audio;
} session_host_client_t;
```

### Host Render Thread

```c
/**
 * @brief Host render thread - mixes media and broadcasts to participants
 *
 * PATTERN (reuses logic from server/render.c):
 * 1. Collect latest video frame from each participant
 * 2. Generate mixed ASCII frame using create_mixed_ascii_frame_for_client()
 * 3. Broadcast frame to all participants (for display)
 * 4. Mix audio from all participants
 * 5. Broadcast mixed audio to all participants
 * 6. Repeat at 60 FPS for video, 100 FPS for audio
 *
 * REUSE:
 * - create_mixed_ascii_frame_for_client() - handles grid layout and ASCII conversion
 * - mixer_process_excluding_source() - audio mixing logic
 * - Atomic frame buffer swaps (no expensive locking)
 */
static void *host_render_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;

  log_info("Host render thread started");

  uint64_t last_video_render = 0;
  uint64_t last_audio_render = 0;

  while (host->render_thread_running && host->running) {
    uint64_t now_ns = time_get_ns();

    // VIDEO RENDERING (60 FPS = 16.7ms)
    if (time_elapsed_ns(last_video_render, now_ns) > 16666666) {
      // Collect video frames from all participants
      uint32_t client_ids[32];
      int client_count = session_host_get_client_ids(host, client_ids, 32);

      if (client_count > 0) {
        // Generate mixed ASCII frame (similar to server mode)
        char *mixed_frame = generate_discovery_mixed_frame(host, client_ids, client_count);

        if (mixed_frame) {
          // Broadcast frame to all participants
          for (int i = 0; i < client_count; i++) {
            packet_send_ascii_frame(host, client_ids[i], mixed_frame);
          }
          SAFE_FREE(mixed_frame);
        }
      }

      last_video_render = now_ns;
    }

    // AUDIO RENDERING (100 FPS = 10ms)
    if (time_elapsed_ns(last_audio_render, now_ns) > 10000000) {
      // Mix audio from all participants
      float mixed_audio[960];  // 20ms @ 48kHz
      mix_participant_audio(host, mixed_audio, 960);

      // Encode and broadcast to all participants
      uint8_t opus_buffer[1000];
      int opus_len = audio_encode_opus(mixed_audio, 960, opus_buffer, sizeof(opus_buffer));

      if (opus_len > 0) {
        session_host_broadcast_audio(host, opus_buffer, opus_len);
      }

      last_audio_render = now_ns;
    }

    // Small sleep to prevent busy-loop
    platform_sleep_ns(1000000);  // 1ms sleep
  }

  log_info("Host render thread stopped");
  return NULL;
}
```

### Key Helper Functions

```c
/**
 * @brief Generate mixed ASCII frame from participant videos
 *
 * REUSE PATTERN from src/server/stream.c:create_mixed_ascii_frame_for_client()
 * - Collect all participant frames
 * - Calculate optimal grid layout (2x2, 3x3, etc based on participant count)
 * - Convert composite RGB to ASCII art
 * - Apply terminal capabilities (colors, palette)
 *
 * @return Malloc'd ASCII frame string (caller must free)
 */
static char *generate_discovery_mixed_frame(session_host_t *host, uint32_t *client_ids, int count) {
  // Implementation reuses:
  // - collect_video_sources() logic
  // - calculate_optimal_grid_layout() logic
  // - create_single/multi_source_composite() logic
  // - convert_composite_to_ascii() logic
  //
  // For simple case (1-2 participants): may not need full grid
  // For multiple: use same grid algorithm as server mode
}

/**
 * @brief Mix audio from all participants
 *
 * REUSE PATTERN from lib/mixer.c
 * - For each participant's audio buffer
 * - Read samples and accumulate into output
 * - Simple addition with clipping (or use mixer_process_excluding_source)
 */
static void mix_participant_audio(session_host_t *host, float *output, int samples) {
  memset(output, 0, samples * sizeof(float));

  // Lock client list briefly to get IDs
  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (!host->clients[i].active) continue;

    ringbuffer_t *audio_buf = host->clients[i].incoming_audio;
    if (!audio_buf) continue;

    // Read samples from this participant
    float participant_samples[960];
    int read = ringbuffer_read(audio_buf, participant_samples, samples);

    // Simple mix: add with clip to [-1, 1]
    for (int j = 0; j < read; j++) {
      output[j] += participant_samples[j];
      if (output[j] > 1.0f) output[j] = 1.0f;
      if (output[j] < -1.0f) output[j] = -1.0f;
    }
  }
  mutex_unlock(&host->clients_mutex);
}
```

### Host API Additions

```c
/**
 * @brief Start rendering thread (video mixing and audio distribution)
 * @param host Host handle
 * @return ASCIICHAT_OK on success
 *
 * Called automatically when first participant joins, or explicitly by user.
 * Handles:
 * - Collecting participant video frames
 * - Generating mixed ASCII frame in grid layout
 * - Broadcasting mixed frame to all participants
 * - Mixing and broadcasting audio
 */
asciichat_error_t session_host_start_render(session_host_t *host);

/**
 * @brief Stop rendering thread
 * @param host Host handle
 */
void session_host_stop_render(session_host_t *host);
```

---

## Part 3: Receive Loop Integration

The existing receive loop in `session_host` needs to handle incoming frames and audio:

```c
// In receive_loop_thread(), add to packet handling:

case PACKET_TYPE_IMAGE_FRAME:
  // Store in client's incoming_video buffer
  video_frame_t *frame = (video_frame_t *)data;
  video_frame_buffer_t *incoming = host->clients[i].incoming_video;

  video_frame_t *write_buf = video_frame_begin_write(incoming);
  memcpy(write_buf->data, frame->data, frame->width * frame->height * 3);
  write_buf->width = frame->width;
  write_buf->height = frame->height;
  write_buf->size = frame->width * frame->height * 3;
  video_frame_commit(incoming);

  log_debug_every(500000, "Video frame from client %u", client_id);
  break;

case PACKET_TYPE_AUDIO:
  // Decode Opus and store in client's audio buffer
  float sample_buffer[960];
  int samples = audio_decode_opus(data, len, sample_buffer, 960);

  ringbuffer_t *audio_buf = host->clients[i].incoming_audio;
  ringbuffer_write(audio_buf, sample_buffer, samples);

  log_debug_every(1000000, "Audio from client %u", client_id);
  break;
```

---

## Part 4: Integration Points with Discovery State Machine

In `src/discovery/session.c`, when transitioning to ACTIVE state:

```c
case DISCOVERY_STATE_ACTIVE:
  // Host role: Start rendering
  if (session->is_host && session->host_ctx) {
    session_host_start_render(session->host_ctx);
    session_participant_start_video_capture(session->participant_ctx);
    session_participant_start_audio_capture(session->participant_ctx);
  }

  // Participant role: Start capturing
  if (!session->is_host && session->participant_ctx) {
    session_participant_start_video_capture(session->participant_ctx);
    session_participant_start_audio_capture(session->participant_ctx);
  }

  // Both roles can now display received frames via on_frame_received callback
  break;
```

---

## Part 5: Memory and Resource Management

### Buffer Allocation

```c
// Video frame buffers (per participant, on both sides)
video_frame_buffer_t *incoming = video_frame_buffer_create(
  480, 270,  // Network-optimal size (HD preview)
  VIDEO_FORMAT_RGB24
);

// Audio buffers (per participant)
ringbuffer_t *audio = ringbuffer_create(
  960 * sizeof(float) * 10  // ~200ms buffer at 48kHz
);
```

### Cleanup

```c
// In session_host_remove_client():
if (client->incoming_video) {
  video_frame_buffer_destroy(client->incoming_video);
  client->incoming_video = NULL;
}
if (client->incoming_audio) {
  ringbuffer_destroy(client->incoming_audio);
  client->incoming_audio = NULL;
}

// In session_participant_destroy():
if (p->video_capture) {
  session_capture_destroy(p->video_capture);
  p->video_capture = NULL;
}
if (p->audio_capture) {
  session_audio_capture_destroy(p->audio_capture);
  p->audio_capture = NULL;
}
```

---

## Summary: Why This Design is Clean

1. **Zero duplication** - Uses `session_capture_ctx_t`, mixer, frame generation from existing code
2. **Consistent patterns** - Mirrors server/client threading model
3. **Scalable** - Works from 1v1 to N-way calls without redesign
4. **Efficient** - Atomic buffers, no contention in hot paths
5. **Maintainable** - Clear separation of concerns (capture → render → display)
6. **Testable** - Each component can be tested independently

The architecture follows the principle: **Extract the essence, not the implementation.**
