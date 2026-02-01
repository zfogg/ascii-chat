# Discovery Mode Media Integration Design

## Architecture Overview

Discovery mode uses **WebRTC DataChannel as the exclusive transport** for all media (video, audio) and control packets (NAT_STATE, NETWORK_QUALITY, MIGRATE). This eliminates the need for separate TCP connections while enabling peer-to-peer media flow and role migration.

### Media Transport Model

```
                  WebRTC DataChannel
                  (Single persistent connection)
                          ↓
    ┌─────────────────────────────────────┐
    │  Control Packets                    │
    │  - NAT_STATE (140): Host election   │
    │  - NETWORK_QUALITY (141): Bandwidth │
    │  - MIGRATE (142): Role swap         │
    ├─────────────────────────────────────┤
    │  Media Packets                      │
    │  - IMAGE_FRAME: Video data          │
    │  - AUDIO: Opus-compressed audio     │
    └─────────────────────────────────────┘
```

**Key Insight**: Same DataChannel carries both control and media. Host role can shift within the call without interruption by swapping sender/receiver roles for media frames.

### Key Design Principles

1. **DataChannel-exclusive transport** - All packets (control, media) over WebRTC, no TCP for media
2. **Role duality** - Host is both renderer AND participant (sends own frames + renders mixed frames)
3. **Background measurement** - Bandwidth tracked from actual media frames in real-time
4. **Atomic role transitions** - MIGRATE packet triggers role swap without connection reset
5. **Reuse core logic** - Leverage existing capture, render, mixer, and frame generation code
6. **Per-role threading** - Participants: capture threads | Host: render thread + participant capture
7. **Memory efficient** - Use buffer pools and aligned allocation like existing code

---

## Part 0: DataChannel as Transport Layer

### Why DataChannel Instead of TCP?

1. **Single connection** - No need for separate TCP + WebRTC
2. **Faster media start** - Media begins immediately after DataChannel opens
3. **Role migration** - Can swap host roles over same channel without reconnection
4. **Simpler NAT traversal** - One WebRTC negotiation handles all scenarios
5. **Ordered delivery** - DataChannel (default config) maintains message order
6. **Latency** - Media over same path as control packets (less routing variability)

### Packet Framing Over DataChannel

All packets maintain the existing format with magic + type + length + CRC but are sent as DataChannel messages:

```c
typedef struct {
  uint32_t magic;     // 0xDEADBEEF validation
  uint16_t type;      // packet_type_t (1-3 for frame, 140+ for control)
  uint32_t length;    // payload size
  uint32_t crc32;     // payload checksum
  uint32_t sender_id; // participant_id (16 bytes packed as UUID)
} __attribute__((packed)) packet_header_t;

// Sent as: [header (20 bytes)] [payload (length bytes)]
// Total per packet: 20 + length bytes
```

**DataChannel limits**:
- Max message size: 16KB (sufficient for compressed frames + overhead)
- Ordered delivery: enabled by default
- Backpressure: implement via ACK or send buffering

---

## Part 0.5: Background Bandwidth Measurement (Continuous During Call)

### How Bandwidth Measurement Works

Unlike Phase 1 (NAT detection), bandwidth is measured from **actual media frames** flowing through the DataChannel:

```c
// Per-participant measurement window
typedef struct {
  // Accumulation over 30-60 second window
  uint64_t bytes_received;          // Sum of all frame sizes received
  uint32_t frames_received;          // Count of frames successfully decoded
  uint32_t frames_expected;          // Expected count (based on FPS + duration)

  // RTT samples (from frame timestamps)
  uint16_t rtt_samples[100];         // Recent RTT measurements
  int rtt_sample_count;              // Number of valid samples

  // Time window boundaries
  uint64_t window_start_time_ms;     // When measurement started
  uint64_t window_end_time_ms;       // When to report (30-60s later)

  // Results after window closes
  uint32_t measured_upload_kbps;     // (bytes_received * 8) / (window_ms / 1000) / 1000
  uint16_t rtt_min_ms;
  uint16_t rtt_max_ms;
  uint16_t rtt_avg_ms;
  uint8_t jitter_ms;                 // max_rtt - min_rtt
  uint8_t packet_loss_pct;           // (expected - received) * 100 / expected
} bandwidth_measurement_t;
```

### Per-Frame Measurement Process

In the participant receive loop, **each frame carries a timestamp** that's used to compute RTT:

```c
// When receiving IMAGE_FRAME from host:
packet_header_t *pkt = (packet_header_t *)incoming_data;
frame_data_t *frame = (frame_data_t *)&pkt[1];

// Extract timestamp (part of frame header)
uint64_t frame_send_time_ms = frame->timestamp_ms;
uint64_t now_ms = time_get_ms();
uint16_t rtt_ms = now_ms - frame_send_time_ms;

// Add to accumulator
measurement->bytes_received += pkt->length - sizeof(frame_data_t);
measurement->frames_received++;
measurement->rtt_samples[measurement->rtt_sample_count++] = rtt_ms;

// Update window end if needed (still accumulating)
if (now_ms - measurement->window_start_time_ms >= 30000) {  // 30-60s window
  // Report is ready
  compute_bandwidth_stats(measurement);
  queue_network_quality_report(measurement);
  reset_measurement_window(measurement);
}
```

### Quality Scoring (Host-Side Only)

After collecting NETWORK_QUALITY reports from all participants (including self-measurement), the host scores each:

```c
uint32_t score_participant(const bandwidth_measurement_t *m) {
  // Simple weighted scoring
  uint32_t bandwidth_score = m->measured_upload_kbps / 10;  // Kbps → tens of Kbps
  uint32_t loss_score = 100 - m->packet_loss_pct;           // 0-100
  uint32_t latency_score = 100 - CLAMP(m->rtt_avg_ms, 0, 100);  // 0-100 (favor <100ms)

  uint32_t total = bandwidth_score + loss_score + latency_score;

  // Log for debugging
  log_info("Score: participant upload=%u kbps, loss=%u%%, rtt=%u ms = %u points",
           m->measured_upload_kbps, m->packet_loss_pct, m->rtt_avg_ms, total);

  return total;
}

// Migration trigger: 20% threshold
bool should_migrate(uint32_t current_host_score, uint32_t candidate_score) {
  // If candidate is 20%+ better: migrate
  return candidate_score > current_host_score * 1.2;  // or 0.8x comparison
}
```

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

### State Transitions and Media Flow

In `src/discovery/session.c`, when transitioning through discovery phases:

#### PHASE_1: NAT Negotiation (No Media Yet)

```c
case DISCOVERY_STATE_NAT_EXCHANGE:
  // Send NAT_STATE packet over DataChannel
  // (Media threads not started yet)
  break;
```

#### PHASE_2: Host Election Complete → Active

```c
case DISCOVERY_STATE_ACTIVE:
  // DUAL ROLE FOR HOST: Both server and participant
  if (session->is_host) {
    // Host does BOTH:
    // 1. Render thread: receives frames from participants, generates mixed ASCII, broadcasts
    // 2. Participant capture: sends own webcam to render thread, displays mixed frames

    session_host_start_render(session->host_ctx);
    session_participant_start_video_capture(session->participant_ctx);
    session_participant_start_audio_capture(session->participant_ctx);

    log_info("Host mode: started rendering + participant capture");
  } else {
    // Participant-only role: send frames to host, receive/display mixed frames
    session_participant_start_video_capture(session->participant_ctx);
    session_participant_start_audio_capture(session->participant_ctx);

    log_info("Participant mode: sending frames to host");
  }
  break;
```

#### PHASE_3: Host Role Migration (Optional, Within 60 Seconds)

When MIGRATE packet is received, the role transition happens **within the same connection**:

```c
case PACKET_TYPE_ACIP_MIGRATE:
  migration = (acip_migrate_t *)pkt_data;

  if (is_target_of_migration(migration->new_host_id)) {
    // Transition from participant → host
    log_info("Transitioning to host role");

    // Stop old role
    session_participant_stop_video_capture(session->participant_ctx);
    session_participant_stop_audio_capture(session->participant_ctx);

    // Assume host role
    session->is_host = true;
    session_host_start_render(session->host_ctx);  // Start rendering mixed frames

    // Resume participant capture (now sends to new host context)
    session_participant_start_video_capture(session->participant_ctx);
    session_participant_start_audio_capture(session->participant_ctx);

    log_info("New host role: rendering started");
  } else if (was_host_before_migration()) {
    // Transition from host → participant
    log_info("Transitioning to participant role");

    // Stop rendering thread
    session_host_stop_render(session->host_ctx);

    // Continue with participant capture (now sends to new host)
    // No need to restart - already running
    log_info("Now participant: sending frames to new host");
  }
  break;
```

### DataChannel Message Handling

The participant receive loop handles **three categories of messages**:

```c
// In session_participant receive_loop (over DataChannel):

while (connected) {
  packet_header_t *pkt = receive_from_datachannel();

  switch (pkt->type) {
    // CONTROL PACKETS (discovery negotiation)
    case PACKET_TYPE_ACIP_NAT_STATE:
      handle_nat_state(pkt);
      break;

    case PACKET_TYPE_ACIP_NETWORK_QUALITY:
      handle_network_quality_report(pkt);
      break;

    case PACKET_TYPE_ACIP_MIGRATE:
      handle_migrate_transition(pkt);
      break;

    // MEDIA PACKETS (from host or new host)
    case PACKET_TYPE_IMAGE_FRAME:
      frame_data = (frame_data_t *)&pkt[1];

      // Track measurement: accumulate frame size + RTT
      record_frame_measurement(frame_data->timestamp_ms);

      // Display the mixed frame
      display_frame_on_terminal(frame_data);
      break;

    case PACKET_TYPE_AUDIO:
      audio_data = (audio_data_t *)&pkt[1];

      // Play the mixed audio
      audio_playback_queue_samples(audio_data->opus_buffer, audio_data->opus_len);
      break;
  }
}
```

---

## Part 4.5: MIGRATE Packet Handling (Early Role Transition)

When the current host determines a better host exists (within first 60 seconds), it sends a MIGRATE packet to trigger an **instantaneous role swap** over the same DataChannel.

### MIGRATE Packet Structure

```c
// PACKET_TYPE_ACIP_MIGRATE = 142
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];       // Who is currently hosting (sender)
  uint8_t new_host_id[16];          // Who will become the new host
  uint32_t current_host_score;      // Why we're migrating (for logging)
  uint32_t new_host_score;
  uint8_t reserved[32];
} acip_migrate_t;
```

### Host-Side: Sending MIGRATE

```c
// In host_render_thread(), after collecting NETWORK_QUALITY reports:

// Score all participants
uint32_t scores[MAX_PARTICIPANTS];
uint8_t best_id = NULL;
uint32_t best_score = 0;

// Score ourselves
scores[our_id] = score_self(/* our bandwidth measurement */);
best_score = scores[our_id];
best_id = our_id;

// Score each participant
for (int i = 0; i < participant_count; i++) {
  uint32_t score = score_participant(&bandwidth_reports[i]);
  scores[i] = score;

  if (score > best_score) {
    best_score = score;
    best_id = participants[i].id;
  }
}

// Check if we should migrate (within first 60 seconds)
uint64_t call_duration = time_get_ms() - call_start_time_ms;
if (call_duration < 60000) {
  if (should_migrate(scores[our_id], best_score)) {
    // Send MIGRATE packet over DataChannel
    acip_migrate_t migrate = {
      .current_host_score = scores[our_id],
      .new_host_score = best_score,
    };
    memcpy(migrate.session_id, session_id, 16);
    memcpy(migrate.participant_id, our_id, 16);
    memcpy(migrate.new_host_id, best_id, 16);

    packet_send_migrate(datachannel, &migrate);

    // Current host (us) transitions to participant mode
    // New host will transition to host mode
  }
}
```

### Participant-Side: Receiving MIGRATE

When a participant receives MIGRATE, they check if they're the target of the migration:

```c
// In datachannel message handler:
case PACKET_TYPE_ACIP_MIGRATE: {
  acip_migrate_t *migrate = (acip_migrate_t *)pkt_data;

  log_info("MIGRATE received: %s → %s (scores: %u → %u)",
           format_id(migrate.participant_id),
           format_id(migrate.new_host_id),
           migrate.current_host_score,
           migrate.new_host_score);

  // Check if this is for us
  if (memcmp(migrate.new_host_id, our_id, 16) == 0) {
    // We are the new host!
    log_info("We are the new host, transitioning from participant → host");

    // Stop participant mode capture threads
    session_participant_stop_video_capture(participant_ctx);
    session_participant_stop_audio_capture(participant_ctx);

    // Transition to host mode
    // Host mode will:
    // 1. Stop receiving IMAGE_FRAME from old host
    // 2. Start receiving IMAGE_FRAME from other participants
    // 3. Start generating mixed ASCII frames
    // 4. Broadcast mixed frames to all participants
    session_host_start_render(host_ctx);

    // OLD host becomes participant
    // They will now send IMAGE_FRAME instead of receiving it
    participant_ctx->is_participant = true;
    session_participant_start_video_capture(participant_ctx);
    session_participant_start_audio_capture(participant_ctx);

  } else {
    // We're a regular participant (not the new host)
    // Just acknowledge and continue
    // Media flow will soon change sources but we don't need to do anything
    // (we'll stop receiving from old host, start from new host naturally)
  }
  break;
}
```

### Frame Flow During Migration

**Before MIGRATE**:
```
Old Host (Alice) sends:              All Participants receive:
├─ Mixed ASCII frames               ├─ Display mixed frame
├─ Mixed audio                      └─ Play audio
```

**MIGRATE packet sent**:
```
Alice → Everyone: "Bob is new host"
```

**After MIGRATE (within next 100ms)**:
```
New Host (Bob) sends:                All Participants receive:
├─ Mixed ASCII frames               ├─ Display mixed frame
├─ Mixed audio                      └─ Play audio

Old Host (Alice) sends:              Host (Bob) receives:
├─ Video frames (IMAGE_FRAME)       ├─ Incorporates Alice's video
└─ Audio frames (AUDIO)             └─ Mixes Alice's audio
```

**No connection reset, no new DataChannel, no ACDS involvement**.

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

1. **Single transport** - DataChannel carries all control and media (no TCP for media)
2. **Instant role migration** - Host role can swap within first 60 seconds without reconnection
3. **Background bandwidth measurement** - No synthetic tests, measured from real frames
4. **Deterministic host election** - Both sides compute same result from NAT_STATE independently
5. **Zero duplication** - Reuses `session_capture_ctx_t`, mixer, frame generation from existing code
6. **Consistent patterns** - Mirrors server/client threading model
7. **Scalable** - Works from 1v1 to N-way calls without redesign
8. **Efficient** - Atomic buffers, no contention in hot paths
9. **Maintainable** - Clear separation of concerns (capture → render → display)
10. **Testable** - Each component can be tested independently

### Key Architectural Improvements Over Previous Models

| Feature | Old Model | New Model |
|---------|-----------|-----------|
| Media transport | TCP + WebRTC | WebRTC DataChannel only |
| Host negotiation | ACDS relay | Peer-to-peer over DataChannel |
| Bandwidth test | Synthetic (blocks media start) | Real frames (background, parallel with media) |
| Role migration | Requires new connection | Same DataChannel, instantaneous |
| New participant join | No re-negotiation needed | Queries ACDS for current host only |
| Initial media latency | 1-2+ seconds | ~500ms |
| Migration detection | 5-30+ seconds | Continuous measurement + <100ms decision |
| ACDS involvement in failover | Critical (election) | Minimal (updates only) |

The architecture follows the principle: **Extract the essence, not the implementation.**
