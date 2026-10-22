#include <ascii-chat/otel.h>
#include <ascii-chat/stats/runtime.h>
/* Per-client video conversion and audio mixing. Render workers submit media
 * to the shared ACIP scheduler and are joined before connection destruction. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "render.h"
#include "connections.h"
#include "client.h"
#include "main.h"
#include "stream.h"
#include "protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/fps.h>

/* ============================================================================
 * Cross-Platform Utility Functions
 * ============================================================================
 */

/**
 * @brief Interruptible sleep function with platform-specific optimizations
 *
 * Provides a sleep mechanism that can be interrupted by the global shutdown
 * signal, enabling responsive thread termination. The implementation varies
 * by platform to optimize for responsiveness vs CPU usage.
 *
 * PLATFORM-SPECIFIC BEHAVIOR:
 * ============================
 *
 * WINDOWS IMPLEMENTATION:
 * - Uses Sleep() API with millisecond precision
 * - Simple approach due to Windows timer characteristics
 * - Sleep(1) may sleep up to 15.6ms due to timer resolution
 * - Checks shutdown flag before and after sleep
 *
 * POSIX IMPLEMENTATION (Linux/macOS):
 * - Uses condition variables for precise timing
 * - Can be interrupted immediately by shutdown signal
 * - Higher responsiveness for thread termination
 * - Uses static mutex/condition variable for coordination
 *
 * USAGE PATTERNS:
 * ===============
 * - Rate limiting in render threads (maintain target FPS)
 * - Backoff delays when queues are full
 * - Polling intervals for state changes
 * - Graceful busy-wait alternatives
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Low CPU overhead (proper sleep, not busy wait)
 * - Sub-second responsiveness to shutdown
 * - Maintains timing precision for media processing
 * - Scales well with many concurrent threads
 *
 * ERROR HANDLING:
 * - Early return if shutdown flag is set
 * - Platform-specific error handling for sleep APIs
 * - Logging for debugging (throttled to avoid spam)
 *
 * @param usec Sleep duration in microseconds
 *
 * @note Function may return early if shutdown is requested
 * @note Minimum sleep time is platform-dependent (typically 1ms)
 * @note Static variables are used for call counting (debugging)
 *
 * @warning Not suitable for precise timing (use for rate limiting only)
 * @see VIDEO_RENDER_FPS For video thread timing requirements
 */

// Removed interruptible_usleep - using regular platform_sleep_us instead
// Sleep interruption isn't needed for small delays and isn't truly possible anyway

/* ============================================================================
 * Per-Client Video Rendering Implementation
 * ============================================================================
 */

/**
 * @brief Main video rendering thread function for individual clients
 *
 * This is the core video processing thread that generates personalized ASCII
 * art frames for a specific client at 60fps. Each connected client gets their
 * own dedicated video thread, providing linear performance scaling and
 * personalized rendering based on terminal capabilities.
 *
 * THREAD EXECUTION FLOW:
 * ======================
 *
 * 1. INITIALIZATION:
 *    - Validate client parameters and socket
 *    - Initialize timing variables for rate limiting
 *    - Log thread startup for debugging
 *
 * 2. MAIN PROCESSING LOOP:
 *    - Check thread running state (with mutex protection)
 *    - Rate limit to 60fps using high-resolution timing
 *    - Take atomic snapshot of client state
 *    - Generate personalized ASCII frame for this client
 *    - Queue frame for delivery to client
 *    - Update timing for next iteration
 *
 * 3. CLEANUP AND EXIT:
 *    - Log thread termination
 *    - Return NULL to indicate clean exit
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 *
 * TIMING PRECISION:
 * - Target: 60fps (16.67ms intervals)
 * - Uses CLOCK_MONOTONIC for accurate timing
 * - Only sleeps if ahead of schedule (no CPU waste)
 * - Maintains consistent frame rate under varying load
 *
 * CLIENT-SPECIFIC PROCESSING:
 * - Generates frames customized for client's terminal
 * - Respects color depth, palette, and size preferences
 * - Adapts to client capability changes dynamically
 * - Handles client disconnection gracefully
 *
 * THREAD SAFETY MECHANISMS:
 * =========================
 *
 * STATE SYNCHRONIZATION:
 * - Uses client->client_state_mutex for all state access
 * - Implements snapshot pattern (copy state, release lock)
 * - Prevents race conditions with other threads
 * - Safe concurrent access to client data
 *
 * SHUTDOWN COORDINATION:
 * - Monitors global g_should_exit flag
 * - Checks per-client running flags with mutex protection
 * - Responds to shutdown within one frame interval (16.67ms)
 *
 * VIDEO PIPELINE INTEGRATION:
 * ============================
 *
 * FRAME GENERATION:
 * - Calls create_mixed_ascii_frame_for_client() from stream.c
 * - Provides client-specific dimensions and preferences
 * - Handles frame generation failures gracefully
 *
 * FRAME DELIVERY:
 * - Calls queue_ascii_frame_for_client() for packet queuing
 * - Uses client's dedicated video packet queue
 * - Send thread delivers frames asynchronously
 *
 * ERROR HANDLING STRATEGY:
 * ========================
 * - Invalid client parameters: Log error and exit immediately
 * - Frame generation failures: Continue with debug logging
 * - Queue overflow: Continue with debug logging (expected under load)
 * - Client disconnect: Clean exit without error spam
 * - Timing issues: Self-correcting (no accumulation)
 *
 * LOGGING AND MONITORING:
 * =======================
 * - Success logging: Throttled (every ~4 seconds)
 * - Failure logging: Throttled (every ~10 seconds)
 * - Performance metrics: Frame count and size tracking
 * - Debug information: Timing and queue status
 *
 * @param arg Pointer to client_info_t for the target client
 * @return NULL on thread completion (always clean exit)
 *
 * @note This function runs in its own dedicated thread
 * @note Thread lifetime matches client connection lifetime
 * @note Memory allocated for frames is automatically freed
 *
 * @warning Invalid client parameter causes immediate thread termination
 * @warning Thread must be joined properly to prevent resource leaks
 *
 * @see create_mixed_ascii_frame_for_client() For frame generation
 * @see queue_ascii_frame_for_client() For frame delivery
 * @see VIDEO_RENDER_FPS For timing constant definition
 */

void *client_video_render_thread(void *arg) {
  acip_frame_workspace_t event_workspace = {0};
  client_info_t *client = (client_info_t *)arg;
  if (!client) {
    log_error("NULL client pointer in video render thread");
    return NULL;
  }

  // Take snapshot of client ID and socket at start to avoid race conditions
  char thread_client_id[MAX_CLIENT_ID_LEN];
  SAFE_STRNCPY(thread_client_id, client->client_id, sizeof(thread_client_id) - 1);
  bool is_webrtc = acip_transport_get_type(client->transport) == ACIP_TRANSPORT_WEBRTC;
  (void)is_webrtc; // May be unused in release builds

  log_debug("Video render thread: client_id=%s, webrtc=%d", thread_client_id, is_webrtc);

  log_debug("[VIDEO_RENDER_THREAD_START] ★★★ LOCK STATE at thread entry for client %s", thread_client_id);
  // debug_sync_print_state();  // Disabled: causes AddressSanitizer stack-use-after-return crash

  // Wait for client to send terminal capabilities before rendering
  // Without capabilities, convert_composite_to_ascii() will fail
  int timeout_ms = 5000;
  int waited_ms = 0;
  while (!client->has_terminal_caps && !atomic_load_bool(&g_should_exit) && !atomic_load_bool(&client->shutting_down)) {
    if (waited_ms == 0) {
      log_debug("Waiting for terminal capabilities from client %s...", thread_client_id);
    }
    APP_CALLBACK_VOID(platform_pump_events);
    platform_sleep_ms(10);
    waited_ms += 10;
    if (waited_ms >= timeout_ms) {
      log_debug("Still waiting for terminal capabilities from client %s", thread_client_id);
      waited_ms = 0;
    }
  }
  if (client->has_terminal_caps) {
    log_debug("Received terminal capabilities for client %s after %dms", thread_client_id, waited_ms);
  }

  // Get client's desired FPS from capabilities or use default
  int client_fps = VIDEO_RENDER_FPS; // Default to 60 FPS
  // Use snapshot pattern to avoid mutex in render thread
  bool has_caps = client->has_terminal_caps;
  int desired_fps = has_caps ? client->terminal_caps.desired_fps : 0;
  if (has_caps && desired_fps > 0) {
    client_fps = desired_fps;
    log_debug("Client %s requested FPS: %d (has_caps=%d, desired_fps=%d)", thread_client_id, client_fps, has_caps,
              desired_fps);
  } else {
    log_debug("Client %s using default FPS: %d (has_caps=%d, desired_fps=%d)", thread_client_id, client_fps, has_caps,
              desired_fps);
  }

  int base_frame_interval_ms = 1000 / client_fps;
  log_debug("Client %s render interval: %dms (%d FPS)", thread_client_id, base_frame_interval_ms, client_fps);

  // FPS tracking for video render thread
  fps_t video_fps_tracker = {0};
  fps_init(&video_fps_tracker, client_fps, "SERVER VIDEO");

  const uint64_t frame_interval_ns = NS_PER_SEC_INT / client_fps;
  uint64_t next_frame_ns = time_get_ns() + frame_interval_ns;
  bool last_has_sources = false;
  uint32_t frame_gen_count = 0;
  uint64_t frame_gen_start_time = 0;
  uint64_t render_rate_window_start_ns = time_get_ns();
  uint64_t rendered_frames_in_window = 0;
  uint64_t missed_deadlines_in_window = 0;

  log_debug("Video render loop STARTING for client %s", thread_client_id);

  bool should_continue = true;
  while (should_continue && !atomic_load_bool(&g_should_exit) && !atomic_load_bool(&client->shutting_down)) {
    ASCII_OTEL_SCOPE(SERVER_VIDEO);
    START_TIMER("render_iteration");
    log_dev_every(10 * NS_PER_MS_INT, "Video render loop iteration for client %s", thread_client_id);

    // Check for immediate shutdown
    if (atomic_load_bool(&g_should_exit)) {
      log_debug("Video render thread stopping for client %s (g_should_exit)", thread_client_id);
      break;
    }

    bool video_running = atomic_load_bool(&client->video_render_thread_running);
    bool active = atomic_load_bool(&client->active);
    bool shutting_down = atomic_load_bool(&client->shutting_down);

    should_continue = video_running && active && !shutting_down;

    if (!should_continue) {
      log_debug("Video render thread stopping for client %s (should_continue=false: video_running=%d, active=%d, "
                "shutting_down=%d)",
                thread_client_id, video_running, active, shutting_down);
      break;
    }

    // Processing consumes part of the frame interval. Sleep only until the
    // scheduled deadline, and skip missed deadlines without a catch-up burst.
    uint64_t iter_start_ns = time_get_ns();
    START_TIMER("render_frame_sleep");
    if (iter_start_ns < next_frame_ns) {
      platform_sleep_ns(next_frame_ns - iter_start_ns);
    }
    STOP_TIMER_AND_LOG(dev, 0, "render_frame_sleep", "frame deadline sleep completed");
    uint64_t after_sleep_ns = time_get_ns();
    uint64_t elapsed_intervals =
        after_sleep_ns >= next_frame_ns ? (after_sleep_ns - next_frame_ns) / frame_interval_ns + 1 : 1;
    if (elapsed_intervals > 1) {
      missed_deadlines_in_window += elapsed_intervals - 1;
    }
    next_frame_ns += elapsed_intervals * frame_interval_ns;

    // Capture timestamp for FPS tracking and frame timestamps
    uint64_t current_time_ns = time_get_ns();

    // Check thread state again before acquiring locks (client might have been destroyed during sleep).
    should_continue = atomic_load_bool(&client->video_render_thread_running) && atomic_load_bool(&client->active) &&
                      !atomic_load_bool(&client->shutting_down);
    if (!should_continue) {
      break;
    }

    // Optimization: No mutex needed - all fields are atomic or stable.
    // client_id: stable string pointer
    // width/height: stable uint16_t values (set once, never modified)
    // active: atomic_t - use atomic_load for thread safety
    const char *client_id_snapshot = client->client_id;       // Stable read
    unsigned short width_snapshot = client->width;            // Stable uint16_t
    unsigned short height_snapshot = client->height;          // Stable uint16_t
    bool active_snapshot = atomic_load_bool(&client->active); // Atomic read

    // Check if client is still active after getting snapshot
    if (!active_snapshot) {
      break;
    }

    // Phase 2 IMPLEMENTED: Generate frame specifically for THIS client using snapshot data
    size_t frame_size = 0;

    // Check if any clients are sending video
    bool has_video_sources = any_clients_sending_video();

    // DIAGNOSTIC: Track when video sources become available
    if (has_video_sources != last_has_sources) {
      log_debug("Client %s video sources: %s", thread_client_id, has_video_sources ? "AVAILABLE" : "UNAVAILABLE");
      last_has_sources = has_video_sources;
    }

    log_debug_every(5 * NS_PER_MS_INT,
                    "Video render iteration for client %s: has_video_sources=%d, width=%u, height=%u", thread_client_id,
                    has_video_sources, width_snapshot, height_snapshot);

    // Use default dimensions if client dimensions not received
    if (width_snapshot == 0 || height_snapshot == 0) {
      log_dev_every(5 * NS_PER_MS_INT, "Using default dimensions for client %s (client reported: width=%u, height=%u)",
                    thread_client_id, width_snapshot, height_snapshot);
      // Use 80x25 as standard terminal size fallback for snapshot/test clients
      width_snapshot = 80;
      height_snapshot = 25;
    }

    // Generate frames for all clients with valid dimensions, regardless of whether other clients are sending video.
    // This ensures WebSocket snapshot clients (receive-only mode) receive frames even when no other clients are
    // sending video.
    int sources_count = 0; // Track number of video sources in this frame

    // DIAGNOSTIC: Track every frame generation attempt
    frame_gen_count++;
    if (frame_gen_count == 1) {
      frame_gen_start_time = current_time_ns;
    }

    // Log every 120 attempts (should be ~2 seconds at 60 Hz)
    if (frame_gen_count % 120 == 0) {
      uint64_t elapsed_ns = current_time_ns - frame_gen_start_time;
      double gen_fps = (120.0 / (elapsed_ns / (double)NS_PER_SEC_INT));
      log_dev("render loop: client=%s fps=%.1f", thread_client_id, gen_fps);
    }

    log_dev_every(5 * NS_PER_MS_INT, "About to call create_mixed_ascii_frame_for_client for client %s with dims %ux%u",
                  thread_client_id, width_snapshot, height_snapshot);

    // debug_sync_print_state() is too expensive to call every frame (kills FPS)

    uint64_t frame_create_start_ns = time_get_ns();
    START_TIMER("render_create_frame");
    acip_render_settings_t frame_rendering = {0};
    char *ascii_frame = create_mixed_ascii_frame_for_client(client_id_snapshot, width_snapshot, height_snapshot, false,
                                                            &frame_size, NULL, &sources_count, &frame_rendering);
    STOP_TIMER_AND_LOG(dev, 0, "render_create_frame", "create_mixed_ascii_frame completed");
    uint64_t frame_create_end_ns = time_get_ns();
    if (sources_count > 0) {
      stats_duration_record(stats_runtime_scope(), STATS_DURATION_ASCII_CONVERT,
                            frame_create_end_ns - frame_create_start_ns);
      stats_counter_add(stats_runtime_scope(),
                        ascii_frame ? STATS_COUNTER_FRAMES_CONVERTED : STATS_COUNTER_FRAMES_DROPPED, 1);
    }

    if (frame_gen_count % 120 == 0) {
      char sleep_str[32], create_str[32];
      time_pretty((uint64_t)(after_sleep_ns - iter_start_ns), -1, sleep_str, sizeof(sleep_str));
      time_pretty((uint64_t)(frame_create_end_ns - frame_create_start_ns), -1, create_str, sizeof(create_str));
      log_dev("render timing: adaptive_sleep=%s frame_create=%s", sleep_str, create_str);
    }

    // Always send frames at configured FPS, even if no external video sources yet
    // This ensures single-client mode (like testing) still gets continuous frames
    // The frame data (animated background or test pattern) is still valid content

    if (ascii_frame && frame_size > 0) {
      server_send_video(client, &event_workspace, ascii_frame, frame_size, frame_rendering.width,
                        frame_rendering.height, sources_count, &frame_rendering);
      fps_frame_ns(&video_fps_tracker, current_time_ns, "frame rendered");
      rendered_frames_in_window++;
      uint64_t render_rate_now_ns = time_get_ns();
      uint64_t render_rate_elapsed_ns = render_rate_now_ns - render_rate_window_start_ns;
      if (render_rate_elapsed_ns >= NS_PER_SEC_INT) {
        double render_fps = (double)rendered_frames_in_window * NS_PER_SEC_INT / (double)render_rate_elapsed_ns;
        if (render_fps < (double)client_fps * 0.9 || missed_deadlines_in_window > 0) {
          log_warn("Server video render lagging client=%s fps=%.1f target=%d missed_deadlines=%llu", thread_client_id,
                   render_fps, client_fps, (unsigned long long)missed_deadlines_in_window);
        } else {
          log_debug("Server video render client=%s fps=%.1f target=%d missed_deadlines=0", thread_client_id, render_fps,
                    client_fps);
        }
        render_rate_window_start_ns = render_rate_now_ns;
        rendered_frames_in_window = 0;
        missed_deadlines_in_window = 0;
      }
      SAFE_FREE(ascii_frame);
    }

    uint64_t iter_end_ns = time_get_ns();
    if (frame_gen_count % 120 == 0) {
      char total_str[32];
      time_pretty((uint64_t)(iter_end_ns - iter_start_ns), -1, total_str, sizeof(total_str));
      log_warn_every(US_PER_SEC_INT, "  TIMING: TOTAL_ITERATION=%s", total_str);
    }
    STOP_TIMER_AND_LOG(dev, 0, "render_iteration", "render_iteration completed (total)");
  }

#ifdef DEBUG_THREADS
  log_debug("Video render thread stopped for client %s", thread_client_id);
#endif
  acip_frame_workspace_destroy(&event_workspace);

  // Clean up thread-local error context before exit

  return NULL;
}

/* ============================================================================
 * Per-Client Audio Rendering Implementation
 * ============================================================================
 */

/**
 * @brief Main audio rendering thread function for individual clients
 *
 * This is the audio processing thread that generates personalized audio mixes
 * for a specific client at 100fps (10ms intervals, 480 samples @ 48kHz). Each
 * client receives audio from all other clients while excluding their own audio
 * to prevent echo and feedback.
 *
 * THREAD EXECUTION FLOW:
 * ======================
 *
 * 1. INITIALIZATION:
 *    - Validate client parameters and socket
 *    - Allocate local mixing buffer
 *    - Log thread startup for debugging
 *
 * 2. MAIN PROCESSING LOOP:
 *    - Check thread running state (with mutex protection)
 *    - Take atomic snapshot of client state
 *    - Generate audio mix excluding this client's audio
 *    - Queue mixed audio for delivery to client
 *    - Sleep for precise timing (10ms intervals)
 *
 * 3. CLEANUP AND EXIT:
 *    - Log thread termination
 *    - Return NULL to indicate clean exit
 *
 * AUDIO MIXING STRATEGY:
 * ======================
 *
 * ECHO PREVENTION:
 * - Excludes client's own audio from their mix
 * - Prevents audio feedback loops
 * - Uses mixer_process_excluding_source() function
 * - Maintains audio quality for other participants
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 *
 * TIMING PRECISION:
 * - Target: 100fps (10ms intervals, 480 samples @ 48kHz)
 * - Accumulates to 960 samples (20ms) for Opus encoding
 * - Fixed timing (no dynamic rate adjustment)
 * - Low-latency audio delivery
 *
 * BUFFER MANAGEMENT:
 * - Uses fixed-size local buffer (AUDIO_FRAMES_PER_BUFFER)
 * - No dynamic allocation in processing loop
 * - Predictable memory usage
 * - Cache-friendly processing pattern
 *
 * THREAD SAFETY MECHANISMS:
 * =========================
 *
 * STATE SYNCHRONIZATION:
 * - Uses client->client_state_mutex for state access
 * - Implements snapshot pattern for client ID and queues
 * - Prevents race conditions during client disconnect
 * - Safe concurrent access with other threads
 *
 * MIXER COORDINATION:
 * - Global audio mixer is internally thread-safe
 * - Multiple audio threads can process concurrently
 * - Lock-free audio buffer operations where possible
 *
 * AUDIO PIPELINE INTEGRATION:
 * ============================
 *
 * AUDIO MIXING:
 * - Uses global g_audio_mixer for multi-client processing
 * - Processes AUDIO_FRAMES_PER_BUFFER samples per iteration
 * - Handles varying number of active audio sources
 *
 * AUDIO DELIVERY:
 * - Queues audio directly in client's audio packet queue
 * - Higher priority than video packets in send thread
 * - Real-time delivery requirements
 *
 * ERROR HANDLING STRATEGY:
 * ========================
 * - Invalid client parameters: Log error and exit immediately
 * - Missing audio mixer: Continue with polling (mixer may initialize later)
 * - Queue failures: Log debug info and continue (expected under load)
 * - Client disconnect: Clean exit without error spam
 *
 * PERFORMANCE OPTIMIZATIONS:
 * ==========================
 * - Local buffer allocation (no malloc in loop)
 * - Minimal processing per iteration
 * - Efficient mixer integration
 * - Low-overhead packet queuing
 *
 * @param arg Pointer to client_info_t for the target client
 * @return NULL on thread completion (always clean exit)
 *
 * @note This function runs in its own dedicated thread
 * @note Audio processing has higher priority than video
 * @note Thread lifetime matches client connection lifetime
 *
 * @warning Invalid client parameter causes immediate thread termination
 * @warning Thread must be joined properly to prevent resource leaks
 *
 * @see mixer_process_excluding_source() For audio mixing implementation
 * @see AUDIO_FRAMES_PER_BUFFER For buffer size constant
 */

void *client_audio_render_thread(void *arg) {
  client_info_t *client = (client_info_t *)arg;

  if (!client) {
    log_error("Invalid client info in audio render thread");
    return NULL;
  }

  // Take snapshot of client ID and display name at start to avoid race conditions
  char thread_client_id[MAX_CLIENT_ID_LEN];
  SAFE_STRNCPY(thread_client_id, client->client_id, sizeof(thread_client_id) - 1);
  char thread_display_name[64];
  bool is_webrtc = acip_transport_get_type(client->transport) == ACIP_TRANSPORT_WEBRTC;
  (void)is_webrtc; // May be unused in release builds

  // LOCK OPTIMIZATION: Only need client_state_mutex, not global rwlock
  // We already have a stable client pointer
  mutex_lock(&client->client_state_mutex);
  SAFE_STRNCPY(thread_display_name, client->display_name, sizeof(thread_display_name));
  mutex_unlock(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_debug("Audio render thread started for client %s (%s), webrtc=%d", thread_client_id, thread_display_name,
            is_webrtc);
#endif

  // Mix buffer: up to 960 samples for adaptive reading
  // Normal: 480 samples = 10ms @ 48kHz
  // Catchup: 960 samples = 20ms when buffers are filling up
  float mix_buffer[960];

// Opus frame accumulation buffer (960 samples = 20ms @ 48kHz)
// Opus requires minimum 480 samples, 960 is optimal for 20ms frames
#define OPUS_FRAME_SAMPLES 960
  float opus_frame_buffer[OPUS_FRAME_SAMPLES];
  int opus_frame_accumulated = 0;

  // Create Opus encoder for this client's audio stream (48kHz, mono, 128kbps, AUDIO mode for music quality)
  opus_codec_t *opus_encoder = opus_codec_create_encoder(OPUS_APPLICATION_AUDIO, 48000, 128000);
  if (!opus_encoder) {
    log_error("Failed to create Opus encoder for audio render thread (client %s)", thread_client_id);
    return NULL;
  }

  // FPS tracking for audio render thread
  fps_t audio_fps_tracker = {0};
  fps_init(&audio_fps_tracker, AUDIO_PACKET_FPS, "SERVER AUDIO");

  // Per-thread counters (NOT static - each thread instance gets its own)
  int server_audio_frame_count = 0;

  bool should_continue = true;
  uint64_t audio_deadline = time_get_ns();
  while (should_continue && !atomic_load_bool(&g_should_exit) && !atomic_load_bool(&client->shutting_down)) {
    ASCII_OTEL_SCOPE(SERVER_AUDIO);
    log_debug_every(LOG_RATE_SLOW, "Audio render loop iteration for client %s", thread_client_id);

    // Check for immediate shutdown
    if (atomic_load_bool(&g_should_exit)) {
      log_debug("Audio render thread stopping for client %s (g_should_exit)", thread_client_id);
      break;
    }

    // Check thread state before acquiring any locks to prevent use-after-destroy.
    // If we acquire locks after client is being destroyed, we'll crash with SIGSEGV
    should_continue = (((int)atomic_load_bool(&client->audio_render_thread_running) != 0) &&
                       ((int)atomic_load_bool(&client->active) != 0) && !atomic_load_bool(&client->shutting_down));

    if (!should_continue) {
      log_debug("Audio render thread stopping for client %s (should_continue=false)", thread_client_id);
      break;
    }

    if (!g_audio_mixer) {
      log_dev_every(10 * NS_PER_MS_INT, "Audio render waiting for mixer (client %s)", thread_client_id);
      // Check shutdown flag while waiting
      if (atomic_load_bool(&g_should_exit))
        break;
      platform_sleep_ns(10 * NS_PER_MS_INT);
      continue;
    }

    const char *client_id_snapshot = client->client_id;
    if (!atomic_load_bool(&client->active))
      break;

    // Create mix excluding THIS client's audio using snapshot data
    START_TIMER("mix_%s", client_id_snapshot);

    // ADAPTIVE READING: Read more samples when we're behind to catch up
    // Normal: 480 samples per 10ms iteration
    // When behind: read up to 960 samples to catch up faster
    // Check source buffer levels to decide
    int samples_to_read = 480; // Default: 10ms worth

    int samples_mixed = 0;
    uint32_t client_id_hash = fnv1a_hash_string(client_id_snapshot);
    uint64_t stats_mix_start = time_get_ns();
    samples_mixed = mixer_process_excluding_source(g_audio_mixer, mix_buffer, samples_to_read, client_id_hash);
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_AUDIO_MIX, time_get_ns() - stats_mix_start);

    STOP_TIMER_AND_LOG_EVERY(dev, NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "mix_%s", "Mixer for client %s: took",
                             client_id_snapshot);

    log_debug_every(LOG_RATE_SLOW, "Audio render for client %s: samples_mixed=%d", client_id_snapshot, samples_mixed);

    // Accumulate all samples (including 0 or partial) until we have a full Opus frame
    // This maintains continuous stream without silence padding
    START_TIMER("accum_%s", client_id_snapshot);

    int space_available = OPUS_FRAME_SAMPLES - opus_frame_accumulated;
    int samples_to_copy = (samples_mixed <= space_available) ? samples_mixed : space_available;

    // Only copy if we have samples, otherwise just wait for next frame
    if (samples_to_copy > 0) {
      SAFE_MEMCPY(opus_frame_buffer + opus_frame_accumulated,
                  (OPUS_FRAME_SAMPLES - opus_frame_accumulated) * sizeof(float), mix_buffer,
                  samples_to_copy * sizeof(float));
      opus_frame_accumulated += samples_to_copy;
    }

    STOP_TIMER_AND_LOG_EVERY(dev, NS_PER_SEC_INT, 2 * NS_PER_MS_INT, "accum_%s", "Accumulate for client %s: took",
                             client_id_snapshot);

    // Only encode and send when we have accumulated a full Opus frame
    if (opus_frame_accumulated >= OPUS_FRAME_SAMPLES) {
      // Encode accumulated Opus frame (960 samples = 20ms @ 48kHz)
      uint8_t opus_buffer[1024]; // Max Opus frame size

      START_TIMER("opus_encode_%s", client_id_snapshot);

      int opus_size =
          opus_codec_encode(opus_encoder, opus_frame_buffer, OPUS_FRAME_SAMPLES, opus_buffer, sizeof(opus_buffer));

      STOP_TIMER_AND_LOG_EVERY(dev, NS_PER_SEC_INT, 10 * NS_PER_MS_INT, "opus_encode_%s",
                               "Opus encode for client %s: took", client_id_snapshot);

      // DEBUG: Log mix buffer and encoding results to see audio levels being sent
      {
        float peak = 0.0f, rms = 0.0f;
        for (int i = 0; i < OPUS_FRAME_SAMPLES; i++) {
          float abs_val = fabsf(opus_frame_buffer[i]);
          if (abs_val > peak)
            peak = abs_val;
          rms += opus_frame_buffer[i] * opus_frame_buffer[i];
        }
        rms = sqrtf(rms / OPUS_FRAME_SAMPLES);
        // NOTE: server_audio_frame_count is now per-thread (not static), so each client thread has its own counter
        server_audio_frame_count++;
        if (server_audio_frame_count <= 5 || server_audio_frame_count % 20 == 0) {
          // Log first 4 samples to verify they look like valid audio (not NaN/Inf/garbage)
          log_dev_every(4500 * US_PER_MS_INT,
                        "Server audio frame #%d for client %s: samples_mixed=%d, Peak=%.6f, RMS=%.6f, opus_size=%d, "
                        "first4=[%.4f,%.4f,%.4f,%.4f]",
                        server_audio_frame_count, client_id_snapshot, samples_mixed, peak, rms, opus_size,
                        opus_frame_buffer[0], opus_frame_buffer[1], opus_frame_buffer[2], opus_frame_buffer[3]);
        }
      }

      // Always reset accumulation buffer after attempting to encode - we've consumed these samples
      // If we don't reset, new audio samples would be dropped while stale data sits in the buffer
      opus_frame_accumulated = 0;

      if (opus_size <= 0) {
        log_error_every(US_PER_SEC_INT, "Failed to encode audio to Opus for client %s: opus_size=%d",
                        client_id_snapshot, opus_size);
      } else {
        // Wrap Opus data in proper batch packet format for client parsing:
        // Header: sample_rate (4B) + frame_duration (4B) + frame_count (4B)
        // Frame sizes: frame_count * 2B (one uint16 per frame with Opus data size)
        // Opus data: raw encoded audio

        // Fixed values for Opus encoding (standard 48kHz, 960 samples per frame = 20ms)
        const uint32_t sample_rate = 48000;
        const uint32_t frame_duration = 20; // milliseconds (960 samples / 48kHz = 20ms)
        const uint32_t frame_count = 1;     // one Opus frame per packet
        const uint16_t frame_size = (uint16_t)opus_size;

        // Calculate total packet size: 16-byte header + frame_sizes + opus data
        const size_t header_size = 16; // 4 uint32s (sample_rate, frame_duration, frame_count, reserved)
        const size_t frame_sizes_size = (size_t)frame_count * sizeof(uint16_t);
        const size_t total_packet_size = header_size + frame_sizes_size + (size_t)opus_size;

        // Allocate buffer for the complete packet
        uint8_t *packet_buffer = SAFE_MALLOC(total_packet_size, uint8_t *);
        if (!packet_buffer) {
          log_error("Failed to allocate Opus batch packet buffer for client %s (size=%zu)", client_id_snapshot,
                    total_packet_size);
        } else {
          // Build header in network byte order
          uint8_t *ptr = packet_buffer;

          // Write sample_rate (4 bytes, network byte order)
          uint32_t sr_net = htonl(sample_rate);
          memcpy(ptr, &sr_net, 4);
          ptr += 4;

          // Write frame_duration (4 bytes, network byte order)
          uint32_t fd_net = htonl(frame_duration);
          memcpy(ptr, &fd_net, 4);
          ptr += 4;

          // Write frame_count (4 bytes, network byte order)
          uint32_t fc_net = htonl(frame_count);
          memcpy(ptr, &fc_net, 4);
          ptr += 4;

          // Write reserved field (4 bytes, all zeros)
          uint32_t reserved = 0;
          memcpy(ptr, &reserved, 4);
          ptr += 4;

          // Write frame sizes (2 bytes per frame, network byte order)
          for (uint32_t i = 0; i < frame_count; i++) {
            uint16_t fs_net = htons(frame_size);
            memcpy(ptr, &fs_net, 2);
            ptr += 2;
          }

          // Copy Opus data
          memcpy(ptr, opus_buffer, (size_t)opus_size);

          // Queue the properly formatted packet
          START_TIMER("audio_queue_%s", client_id_snapshot);

          asciichat_error_t result = server_send_audio(client, packet_buffer, total_packet_size);

          STOP_TIMER_AND_LOG_EVERY(dev, NS_PER_SEC_INT, 1 * NS_PER_MS_INT, "audio_queue_%s",
                                   "Audio queue for client %s: took", client_id_snapshot);

          if (result != ASCIICHAT_OK) {
            log_debug_every(US_PER_SEC_INT, "Failed to queue Opus audio for client %s", client_id_snapshot);
          } else {
            // FPS tracking - audio packet successfully queued (handles lag detection and periodic reporting)
            fps_frame_ns(&audio_fps_tracker, time_get_ns(), "audio packet queued");
          }

          // Free the allocated packet buffer after queuing (packet_queue_push_copy copies data)
          SAFE_FREE(packet_buffer);
        }
      }
      // NOTE: opus_frame_accumulated is already reset at line 928 after encode attempt
    }

    // Audio mixing rate limiting using adaptive sleep system
    // Target: 10ms intervals (100 FPS) for 480 samples @ 48kHz
    // Use queue_depth=0 and target_depth=0 for constant-rate audio processing
    audio_deadline += 10 * NS_PER_MS_INT;
    uint64_t now = time_get_ns();
    if (audio_deadline > now)
      platform_sleep_ns(audio_deadline - now);
    else if (now - audio_deadline > 100 * NS_PER_MS_INT)
      audio_deadline = now;
  }

#ifdef DEBUG_THREADS
  log_debug("Audio render thread stopped for client %s", thread_client_id);
#endif

  // Clean up Opus encoder
  if (opus_encoder) {
    opus_codec_destroy(opus_encoder);
  }

  // Clean up thread-local error context before exit

  return NULL;
}

/* ============================================================================
 * Thread Lifecycle Management Functions
 * ============================================================================
 */

/**
 * @brief Create and initialize per-client rendering threads
 *
 * Sets up the complete per-client threading infrastructure including both
 * video and audio rendering threads plus all necessary synchronization
 * primitives. This function is called once per client during connection
 * establishment.
 *
 * INITIALIZATION SEQUENCE:
 * ========================
 *
 * 1. MUTEX INITIALIZATION:
 *    - client_state_mutex: Protects client state variables
 *
 * 2. THREAD CREATION:
 *    - Create video rendering thread (60fps)
 *    - Create audio rendering thread (172fps)
 *    - Set running flags with proper synchronization
 *
 * 3. ERROR HANDLING:
 *    - Complete cleanup if any step fails
 *    - Prevent partially initialized client state
 *    - Ensure no resource leaks on failure
 *
 * THREAD SYNCHRONIZATION SETUP:
 * ==============================
 *
 * PER-CLIENT MUTEXES:
 * Each client gets dedicated mutexes for fine-grained locking:
 * - Prevents contention between clients
 * - Allows concurrent processing of multiple clients
 * - Enables lock-free operations within client context
 *
 * THREAD RUNNING FLAGS:
 * - Protected by client_state_mutex
 * - Allow graceful thread termination
 * - Checked frequently by render threads
 *
 * ERROR RECOVERY:
 * ===============
 *
 * PARTIAL FAILURE HANDLING:
 * If thread creation fails partway through:
 * - Stop and join any already-created threads
 * - Destroy any initialized mutexes
 * - Return error code to caller
 * - Leave client in clean state for retry or cleanup
 *
 * RESOURCE LEAK PREVENTION:
 * - All allocations have corresponding cleanup
 * - Thread handles are properly managed
 * - Mutex destruction is deterministic
 *
 * INTEGRATION POINTS:
 * ===================
 * - Called by add_client() in client.c
 * - Threads integrate with stream.c and mixer.c
 * - Cleanup handled by stop_client_render_threads()
 *
 * @param client Target client for thread creation
 * @return 0 on success, -1 on failure
 *
 * @note Function performs complete initialization or complete cleanup
 * @note Mutexes must be destroyed by stop_client_render_threads()
 * @note Thread handles must be joined before client destruction
 *
 * @warning Partial initialization is not supported - complete success or failure
 * @see stop_client_render_threads() For cleanup implementation
 */

int create_client_render_threads(server_context_t *server_ctx, client_info_t *client) {
  log_debug("client_id=%s", client ? client->client_id : "NULL");

  if (!server_ctx || !client) {
    log_error("Cannot create render threads: NULL %s", !server_ctx ? "server_ctx" : "client");
    return -1;
  }

#ifdef DEBUG_THREADS
  log_debug("Creating render threads for client %s", client->client_id);
#endif

  // NOTE: Mutexes are already initialized in add_client() before any threads start
  // This prevents race conditions where receive thread tries to use uninitialized mutexes

  // Initialize render thread control flags
  // IMPORTANT: Set to true BEFORE creating thread to avoid race condition
  // where thread starts and immediately exits because flag is false
  atomic_store_bool(&client->video_render_thread_running, true);
  atomic_store_bool(&client->audio_render_thread_running, true);

  // Create video rendering thread (stop_id=2, stop after receive thread)
  char thread_name[64];
  safe_snprintf(thread_name, sizeof(thread_name), "video_render_%s", client->client_id);
  asciichat_error_t video_result;

  video_result = acthread_create(&client->video_render_thread, thread_name, client_video_render_thread, client);

  if (video_result != ASCIICHAT_OK) {
    // Reset flag since thread creation failed
    atomic_store_bool(&client->video_render_thread_running, false);
    // Mutexes will be destroyed by remove_client() which called us
    return -1;
  }

  // Create audio rendering thread (stop_id=2, same priority as video)
  safe_snprintf(thread_name, sizeof(thread_name), "audio_render_%s", client->client_id);
  asciichat_error_t audio_result;

  audio_result = acthread_create(&client->audio_render_thread, thread_name, client_audio_render_thread, client);
  if (audio_result != ASCIICHAT_OK) {
    // Clean up video thread (atomic operation, no mutex needed)
    atomic_store_bool(&client->video_render_thread_running, false);
    // Reset audio flag since thread creation failed
    atomic_store_bool(&client->audio_render_thread_running, false);
    // stop_client_render_threads() will be called by remove_client()
    // to clean up the video thread we just created
    // Mutexes will be destroyed by remove_client() which called us
    return -1;
  }

#ifdef DEBUG_THREADS
  log_debug("Created render threads for client %s", client->client_id);
#endif

  return 0;
}

/**
 * @brief Stop and cleanup per-client rendering threads
 *
 * Performs graceful shutdown of both video and audio rendering threads for
 * a specific client, including proper thread joining and resource cleanup.
 * This function ensures deterministic cleanup without resource leaks.
 *
 * SHUTDOWN SEQUENCE:
 * ==================
 *
 * 1. SIGNAL SHUTDOWN:
 *    - Set thread running flags to false
 *    - Threads detect flag change and exit processing loops
 *    - Use mutex protection for atomic flag updates
 *
 * 2. THREAD JOINING:
 *    - Wait for video render thread to complete
 *    - Wait for audio render thread to complete
 *    - Handle join failures appropriately
 *
 * 3. RESOURCE CLEANUP:
 *    - Destroy per-client mutexes
 *    - Clear thread handles
 *    - Reset client thread state
 *
 * GRACEFUL TERMINATION:
 * =====================
 *
 * THREAD COORDINATION:
 * - Threads monitor running flags in their main loops
 * - Flags are checked frequently (every iteration)
 * - Threads exit cleanly within one processing cycle
 * - No forced thread termination (unsafe)
 *
 * DETERMINISTIC CLEANUP:
 * - thread_join() waits for complete thread exit
 * - All thread resources are properly released
 * - No zombie threads or resource leaks
 *
 * ERROR HANDLING:
 * ===============
 *
 * THREAD JOIN FAILURES:
 * - Logged as errors but don't prevent cleanup
 * - Continue with resource cleanup regardless
 * - Platform-specific error reporting
 *
 * NULL CLIENT HANDLING:
 * - Safe to call with NULL client pointer
 * - Error logged and function returns safely
 * - No undefined behavior or crashes
 *
 * RESOURCE MANAGEMENT:
 * ====================
 *
 * MUTEX CLEANUP:
 * - Destroys all per-client mutexes
 * - Prevents resource leaks on client disconnect
 * - Platform-independent destruction
 *
 * THREAD HANDLE MANAGEMENT:
 * - Clears thread handles after joining
 * - Prevents accidental reuse of stale handles
 * - Memory zeroing for safety
 *
 * INTEGRATION REQUIREMENTS:
 * =========================
 * - Called by remove_client() in client.c
 * - Must be called before freeing client structure
 * - Coordinates with other cleanup functions
 *
 * @param client Target client for thread cleanup
 *
 * @note Function is safe to call multiple times (idempotent)
 * @note All resources are cleaned up regardless of errors
 * @note Thread joining may take up to one processing cycle
 *
 * @warning Must be called before client structure deallocation
 * @see create_client_render_threads() For thread creation
 * @see thread_join() For platform-specific joining
 */
asciichat_error_t stop_client_render_threads(client_info_t *client) {
  if (!client)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing render client");
  atomic_store_bool(&client->video_render_thread_running, false);
  atomic_store_bool(&client->audio_render_thread_running, false);
  if (thread_is_initialized(&client->video_render_thread)) {
    asciichat_error_t result = thread_join(&client->video_render_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    thread_init(&client->video_render_thread);
  }
  if (thread_is_initialized(&client->audio_render_thread)) {
    asciichat_error_t result = thread_join(&client->audio_render_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    thread_init(&client->audio_render_thread);
  }
  return ASCIICHAT_OK;
}
