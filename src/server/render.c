/**
 * @file server/render.c
 * @ingroup server_render
 * @brief 🎨 Per-client rendering threads: 60fps video and 100fps audio processing with rate limiting
 * ======================
 * 1. Manage per-client video rendering threads (60fps per client)
 * 2. Manage per-client audio rendering threads (100fps per client, 480 samples @ 48kHz)
 * 3. Coordinate frame generation timing and rate limiting
 * 4. Ensure thread-safe access to client state and media buffers
 * 5. Provide graceful thread lifecycle management
 * 6. Handle platform-specific timing and synchronization
 *
 * THREADING ARCHITECTURE:
 * =======================
 *
 * PER-CLIENT THREAD MODEL:
 * Each connected client spawns exactly 2 dedicated threads:
 *
 * 1. VIDEO RENDER THREAD:
 *    - Generates personalized ASCII frames at 60fps
 *    - Calls stream.c functions for video mixing and conversion
 *    - Queues frames in client's video packet queue
 *    - Adapts to client's terminal capabilities
 *
 * 2. AUDIO RENDER THREAD:
 *    - Mixes audio streams excluding client's own audio
 *    - Processes audio at 100fps (10ms intervals, 480 samples @ 48kHz)
 *    - Queues audio packets in client's audio packet queue
 *    - Provides low-latency audio delivery
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 *
 * LINEAR SCALING:
 * - Each client gets dedicated CPU resources
 * - No shared bottlenecks between clients
 * - Performance scales linearly up to 9+ clients
 * - Real-time guarantees maintained per client
 *
 * TIMING PRECISION:
 * - Video: 16.67ms intervals (60fps)
 * - Audio: 10ms intervals (100fps, 480 samples @ 48kHz)
 * - Platform-specific high-resolution timers
 * - Interruptible sleep for responsive shutdown
 *
 * RATE LIMITING STRATEGY:
 * =======================
 *
 * VIDEO RATE LIMITING:
 * - Uses CLOCK_MONOTONIC for precise timing
 * - Calculates elapsed time since last frame
 * - Sleeps only if ahead of schedule
 * - Prevents CPU spinning under light load
 *
 * AUDIO RATE LIMITING:
 * - Fixed 10ms intervals to match 48kHz sample rate (480 samples/iteration)
 * - Accumulates to 960 samples (20ms) for Opus encoding
 * - Ensures smooth audio delivery
 * - Balances latency vs CPU usage
 *
 * THREAD SAFETY AND SYNCHRONIZATION:
 * ===================================
 *
 * PER-CLIENT MUTEX PROTECTION:
 * All client state access uses the snapshot pattern:
 * 1. Acquire client->client_state_mutex
 * 2. Copy needed state to local variables
 * 3. Release mutex immediately
 * 4. Process using local copies
 *
 * Synchronization Points:
 * - Thread running flags (protected by client mutex)
 * - Client dimensions and capabilities (atomic snapshots)
 * - Packet queue access (internally thread-safe)
 * - Buffer access (uses buffer-specific locks)
 *
 * GRACEFUL SHUTDOWN HANDLING:
 * ============================
 *
 * SHUTDOWN SEQUENCE:
 * 1. Set thread running flags to false
 * 2. Threads detect flag change and exit loops
 * 3. Join threads to ensure complete cleanup
 * 4. Destroy per-client mutexes
 * 5. Clear thread handles
 *
 * INTERRUPTIBLE OPERATIONS:
 * - Sleep operations can be interrupted by shutdown signal
 * - Threads check shutdown flag frequently
 * - No blocking operations that can't be interrupted
 *
 * PLATFORM ABSTRACTION INTEGRATION:
 * ==================================
 *
 * CROSS-PLATFORM TIMING:
 * - Windows: Uses Sleep() with millisecond precision
 * - POSIX: Uses condition variables for responsive interruption
 * - High-resolution timing with clock_gettime()
 *
 * THREAD MANAGEMENT:
 * - Uses platform abstraction layer for thread creation/joining
 * - Handles platform-specific thread initialization
 * - Safe thread cleanup across all platforms
 *
 * ERROR HANDLING PHILOSOPHY:
 * ===========================
 * - Thread creation failures trigger complete cleanup
 * - Invalid client states cause thread exit (not crash)
 * - Memory allocation failures are logged and handled gracefully
 * - Network errors don't affect thread stability
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - stream.c: Called by video threads for frame generation
 * - mixer.c: Called by audio threads for audio mixing
 * - client.c: Manages thread creation/destruction lifecycle
 * - packet_queue.c: Used for queuing generated media
 * - main.c: Provides global shutdown signaling
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * The original server.c contained rendering logic mixed with connection
 * management, making it impossible to:
 * - Understand performance characteristics
 * - Optimize rendering pipelines
 * - Add new rendering features
 * - Debug threading issues
 *
 * This separation provides:
 * - Clear rendering pipeline architecture
 * - Isolated performance optimization
 * - Easier threading model comprehension
 * - Better real-time guarantees
 *
 * MEMORY AND RESOURCE MANAGEMENT:
 * ===============================
 * - All allocations are bounded and predictable
 * - Thread stacks are platform-managed
 * - Frame generation uses buffer pools
 * - No memory leaks on thread exit
 * - Automatic resource cleanup on client disconnect
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 * @see stream.c For video frame generation implementation
 * @see mixer.c For audio mixing implementation
 * @see client.c For thread lifecycle management
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "render.h"
#include "client.h"
#include "main.h"
#include "stream.h"
#include "protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/network/packet_queue.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/opus_codec.h>
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
 * - Monitors global g_server_should_exit flag
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
  client_info_t *client = (client_info_t *)arg;
  if (!client) {
    log_error("NULL client pointer in video render thread");
    return NULL;
  }

  // Take snapshot of client ID and socket at start to avoid race conditions
  char thread_client_id[MAX_CLIENT_ID_LEN];
  SAFE_STRNCPY(thread_client_id, client->client_id, sizeof(thread_client_id) - 1);
  socket_t thread_socket = client->socket;
  bool is_webrtc = (thread_socket == INVALID_SOCKET_VALUE);
  (void)is_webrtc; // May be unused in release builds

  log_debug("Video render thread: client_id=%s, webrtc=%d", thread_client_id, is_webrtc);

  log_info("[VIDEO_RENDER_THREAD_START] ★★★ LOCK STATE at thread entry for client %u", thread_client_id);
  debug_sync_print_state();

  // Get client's desired FPS from capabilities or use default
  int client_fps = VIDEO_RENDER_FPS; // Default to 60 FPS
  // Use snapshot pattern to avoid mutex in render thread
  bool has_caps = client->has_terminal_caps;
  int desired_fps = has_caps ? client->terminal_caps.desired_fps : 0;
  if (has_caps && desired_fps > 0) {
    client_fps = desired_fps;
    log_debug("Client %u requested FPS: %d (has_caps=%d, desired_fps=%d)", thread_client_id, client_fps, has_caps,
              desired_fps);
  } else {
    log_debug("Client %u using default FPS: %d (has_caps=%d, desired_fps=%d)", thread_client_id, client_fps, has_caps,
              desired_fps);
  }

  int base_frame_interval_ms = 1000 / client_fps;
  log_debug("Client %u render interval: %dms (%d FPS)", thread_client_id, base_frame_interval_ms, client_fps);

  // FPS tracking for video render thread
  fps_t video_fps_tracker = {0};
  fps_init(&video_fps_tracker, client_fps, "SERVER VIDEO");

  // Adaptive sleep for frame rate limiting
  adaptive_sleep_state_t sleep_state = {0};
  adaptive_sleep_config_t config = {
      .baseline_sleep_ns = (uint64_t)(NS_PER_SEC_INT / client_fps), // Dynamic FPS (typically 16.67ms for 60 FPS)
      .min_speed_multiplier = 1.0,                                  // Constant rate (no slowdown)
      .max_speed_multiplier = 1.0,                                  // Constant rate (no speedup)
      .speedup_rate = 0.0,                                          // No adaptive behavior (constant FPS)
      .slowdown_rate = 0.0                                          // No adaptive behavior (constant FPS)
  };
  adaptive_sleep_init(&sleep_state, &config);

  log_info("Video render loop STARTING for client %u", thread_client_id);

  bool should_continue = true;
  while (should_continue && !atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down)) {
    log_dev_every(10 * NS_PER_MS_INT, "Video render loop iteration for client %u", thread_client_id);

    // Check for immediate shutdown
    if (atomic_load(&g_server_should_exit)) {
      log_debug("Video render thread stopping for client %u (g_server_should_exit)", thread_client_id);
      break;
    }

    bool video_running = atomic_load(&client->video_render_thread_running);
    bool active = atomic_load(&client->active);
    bool shutting_down = atomic_load(&client->shutting_down);

    should_continue = video_running && active && !shutting_down;

    if (!should_continue) {
      log_debug("Video render thread stopping for client %u (should_continue=false: video_running=%d, active=%d, "
                "shutting_down=%d)",
                thread_client_id, video_running, active, shutting_down);
      break;
    }

    // Frame rate limiting using adaptive sleep system
    // Use queue_depth=0 and target_depth=0 for constant-rate renderer (no backlog management)
    adaptive_sleep_do(&sleep_state, 0, 0);

    // Capture timestamp for FPS tracking and frame timestamps
    uint64_t current_time_ns = time_get_ns();

    // Check thread state again before acquiring locks (client might have been destroyed during sleep).
    should_continue = atomic_load(&client->video_render_thread_running) && atomic_load(&client->active) &&
                      !atomic_load(&client->shutting_down);
    if (!should_continue) {
      break;
    }

    // Optimization: No mutex needed - all fields are atomic or stable.
    // client_id: atomic_uint - use atomic_load for thread safety
    // width/height: atomic_ushort - use atomic_load
    // active: atomic_bool - use atomic_load
    const char *client_id_snapshot = client->client_id;            // Atomic read
    unsigned short width_snapshot = atomic_load(&client->width);   // Atomic read
    unsigned short height_snapshot = atomic_load(&client->height); // Atomic read
    bool active_snapshot = atomic_load(&client->active);           // Atomic read

    // Check if client is still active after getting snapshot
    if (!active_snapshot) {
      break;
    }

    // Phase 2 IMPLEMENTED: Generate frame specifically for THIS client using snapshot data
    size_t frame_size = 0;

    // Check if any clients are sending video
    bool has_video_sources = any_clients_sending_video();

    // DIAGNOSTIC: Track when video sources become available
    static bool last_has_sources = false;
    if (has_video_sources != last_has_sources) {
      log_warn("DIAGNOSTIC: Client %u video sources: %s", thread_client_id,
               has_video_sources ? "AVAILABLE" : "UNAVAILABLE");
      last_has_sources = has_video_sources;
    }

    log_debug_every(5 * NS_PER_MS_INT,
                    "Video render iteration for client %u: has_video_sources=%d, width=%u, height=%u", thread_client_id,
                    has_video_sources, width_snapshot, height_snapshot);

    // Use default dimensions if client dimensions not received
    if (width_snapshot == 0 || height_snapshot == 0) {
      log_dev_every(5 * NS_PER_MS_INT, "Using default dimensions for client %u (client reported: width=%u, height=%u)",
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
    // Use per-client static variables to track frequency independently
    static uint32_t frame_gen_count = 0;
    static uint64_t frame_gen_start_time = 0;

    frame_gen_count++;
    if (frame_gen_count == 1) {
      frame_gen_start_time = current_time_ns;
    }

    // Log every 120 attempts (should be ~2 seconds at 60 Hz)
    if (frame_gen_count % 120 == 0) {
      uint64_t elapsed_ns = current_time_ns - frame_gen_start_time;
      double gen_fps = (120.0 / (elapsed_ns / (double)NS_PER_SEC_INT));
      log_warn("DIAGNOSTIC: Client %u LOOP running at %.1f FPS (120 iterations in %.2fs)", thread_client_id, gen_fps,
               elapsed_ns / (double)NS_PER_SEC_INT);
    }

    log_dev_every(5 * NS_PER_MS_INT, "About to call create_mixed_ascii_frame_for_client for client %u with dims %ux%u",
                  thread_client_id, width_snapshot, height_snapshot);

    log_info("[VIDEO_RENDER_LOOP] ★★★ LOCK STATE before create_mixed_ascii_frame_for_client for client %u",
             thread_client_id);
    debug_sync_print_state();

    char *ascii_frame = create_mixed_ascii_frame_for_client(client_id_snapshot, width_snapshot, height_snapshot, false,
                                                            &frame_size, NULL, &sources_count);

    // RACE CONDITION FIX: If no video sources are available yet, skip frame generation
    // This prevents sending empty/NULL frames early and allows more time for video frames to arrive
    if (sources_count == 0) {
      log_dev_every(100 * NS_PER_MS_INT,
                    "Video render waiting for sources on client %u (no video sources yet, skipping frame)",
                    thread_client_id);
      SAFE_FREE(ascii_frame);
      continue; // Skip to next iteration (frame rate limited by adaptive_sleep_do above)
    }

    // DEBUG: Log frame generation details
    static uint32_t last_frame_hash = -1; // Initialize to -1 so first frame is always new
    uint32_t current_frame_hash = 0;
    bool frame_is_new = false;
    if (ascii_frame && frame_size > 0) {
      for (size_t i = 0; i < frame_size && i < 1000; i++) {
        current_frame_hash = (uint32_t)((uint64_t)current_frame_hash * 31 + ((unsigned char *)ascii_frame)[i]);
      }
      if (current_frame_hash != last_frame_hash) {
        log_info("RENDER_FRAME CHANGE: Client %u frame #%zu sources=%d hash=0x%08x (prev=0x%08x)", thread_client_id,
                 frame_size, sources_count, current_frame_hash, last_frame_hash);
        last_frame_hash = current_frame_hash;
        frame_is_new = true;
      } else {
        log_dev_every(25000, "RENDER_FRAME DUPLICATE: Client %u frame #%zu sources=%d hash=0x%08x (no change)",
                      thread_client_id, frame_size, sources_count, current_frame_hash);
        frame_is_new = false;
      }
    }

    log_dev_every(5 * NS_PER_MS_INT,
                  "create_mixed_ascii_frame_for_client returned: ascii_frame=%p, frame_size=%zu, sources_count=%d",
                  (void *)ascii_frame, frame_size, sources_count);

    // Phase 2 IMPLEMENTED: Write frame to double buffer (never drops!)
    if (ascii_frame && frame_size > 0) {
      log_debug_every(5 * NS_PER_MS_INT, "Buffering frame for client %u (size=%zu)", thread_client_id, frame_size);
      // GRID LAYOUT CHANGE DETECTION: Store source count with frame
      // Send thread will compare this with last sent count to detect grid changes
      atomic_store(&client->last_rendered_grid_sources, sources_count);

      // Use double-buffer system which has its own internal swap_mutex
      // No external locking needed - the double-buffer is thread-safe by design
      video_frame_buffer_t *vfb_snapshot = client->outgoing_video_buffer;

      if (vfb_snapshot) {
        video_frame_t *write_frame = video_frame_begin_write(vfb_snapshot);
        if (write_frame) {
          // Copy ASCII frame data to the back buffer (NOT holding rwlock - just double-buffer's internal lock)
          if (write_frame->data && frame_size <= vfb_snapshot->allocated_buffer_size) {
            memcpy(write_frame->data, ascii_frame, frame_size);
            write_frame->size = frame_size;
            write_frame->capture_timestamp_ns = current_time_ns;

            // Only commit the frame if it's actually NEW (different from last committed frame)
            // This prevents sending duplicate frames and improves client-side FPS tracking
            if (frame_is_new) {
              uint64_t commit_start_ns = time_get_ns();
              // Commit the frame (swaps buffers atomically using vfb->swap_mutex, NOT rwlock)
              video_frame_commit(vfb_snapshot);
              uint64_t commit_end_ns = time_get_ns();
              char commit_duration_str[32];
              time_pretty((uint64_t)(commit_end_ns - commit_start_ns), -1, commit_duration_str,
                          sizeof(commit_duration_str));

              static uint32_t commits_count = 0;
              static uint64_t commits_start_time = 0;
              commits_count++;
              if (commits_count == 1) {
                commits_start_time = commit_end_ns;
              }
              if (commits_count % 10 == 0) {
                uint64_t elapsed_ns = commit_end_ns - commits_start_time;
                double commit_fps = (10.0 / (elapsed_ns / (double)NS_PER_SEC_INT));
                log_warn("DIAGNOSTIC: Client %u UNIQUE frames being sent at %.1f FPS (10 commits counted)",
                         thread_client_id, commit_fps);
              }

              log_info("[FRAME_COMMIT_TIMING] Client %u frame commit took %s (hash=0x%08x)", thread_client_id,
                       commit_duration_str, current_frame_hash);

              // Phase 3 IMPLEMENTED: Transmit the rendered frame to client via WebSocket
              // This completes the pipeline: Render → Buffer → Transmit → Client receive → Display
              if (client->transport) {
                asciichat_error_t send_result = acip_send_ascii_frame(
                    client->transport, ascii_frame, frame_size, width_snapshot, height_snapshot, client_id_snapshot);
                if (send_result != ASCIICHAT_OK) {
                  log_warn("[FRAME_SEND_ERROR] Client %u frame transmission failed: error=%d", thread_client_id,
                           send_result);
                } else {
                  log_dev_every(5 * NS_PER_MS_INT, "[FRAME_SEND_OK] Client %u transmitted frame size=%zu (%.1f KB)",
                                thread_client_id, frame_size, frame_size / 1024.0);
                }
              } else {
                log_warn("[FRAME_SEND_ERROR] Client %u has no transport (frame not sent)", thread_client_id);
              }
            } else {
              // Discard duplicate frame by not committing (back buffer is safe to reuse)
              log_dev_every(25000, "Skipping commit for duplicate frame for client %u (hash=0x%08x)", thread_client_id,
                            current_frame_hash);
            }

            // Log occasionally for monitoring
            char pretty_size[64];
            format_bytes_pretty(frame_size, pretty_size, sizeof(pretty_size));

            // Compute hash of ASCII frame to detect duplicates
            uint32_t ascii_hash = 0;
            for (size_t i = 0; i < frame_size && i < 1000; i++) {
              ascii_hash = (uint32_t)((((uint64_t)ascii_hash << 5) - ascii_hash) + (unsigned char)ascii_frame[i]);
            }
            log_dev_every(5 * NS_PER_MS_INT, "Client %u: Rendered ASCII frame size=%s hash=0x%08x sources=%d",
                          thread_client_id, pretty_size, ascii_hash, sources_count);

          } else {
            log_warn("Frame too large for buffer: %zu > %zu", frame_size, vfb_snapshot->allocated_buffer_size);
          }

          // FPS tracking - frame successfully generated (handles lag detection and periodic reporting)
          fps_frame_ns(&video_fps_tracker, current_time_ns, "frame rendered");
        }
      }

      SAFE_FREE(ascii_frame);
    } else {
      // No frame generated (probably no video sources) - this is normal, no error logging needed
      log_dev_every(10 * NS_PER_MS_INT, "Per-client render: No video sources available for client %u",
                    client_id_snapshot);
    }
  }

#ifdef DEBUG_THREADS
  log_debug("Video render thread stopped for client %u", thread_client_id);
#endif

  // Clean up thread-local error context before exit
  asciichat_errno_destroy();

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
  bool is_webrtc = (client->socket == INVALID_SOCKET_VALUE);
  (void)is_webrtc; // May be unused in release builds

  // LOCK OPTIMIZATION: Only need client_state_mutex, not global rwlock
  // We already have a stable client pointer
  mutex_lock(&client->client_state_mutex);
  SAFE_STRNCPY(thread_display_name, client->display_name, sizeof(thread_display_name));
  mutex_unlock(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_debug("Audio render thread started for client %u (%s), webrtc=%d", thread_client_id, thread_display_name,
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
    log_error("Failed to create Opus encoder for audio render thread (client %u)", thread_client_id);
    return NULL;
  }

  // FPS tracking for audio render thread
  fps_t audio_fps_tracker = {0};
  fps_init(&audio_fps_tracker, AUDIO_RENDER_FPS, "SERVER AUDIO");

  // Adaptive sleep for audio rate limiting at 100 FPS (10ms intervals, 480 samples @ 48kHz)
  adaptive_sleep_state_t audio_sleep_state = {0};
  adaptive_sleep_config_t audio_config = {
      .baseline_sleep_ns = 10 * NS_PER_MS_INT, // 10ms = 100 FPS (480 samples @ 48kHz)
      .min_speed_multiplier = 1.0,             // Constant rate (no slowdown)
      .max_speed_multiplier = 1.0,             // Constant rate (no speedup)
      .speedup_rate = 0.0,                     // No adaptive behavior (constant rate)
      .slowdown_rate = 0.0                     // No adaptive behavior (constant rate)
  };
  adaptive_sleep_init(&audio_sleep_state, &audio_config);

  // Per-thread counters (NOT static - each thread instance gets its own)
  int backpressure_check_counter = 0;
  int server_audio_frame_count = 0;

  bool should_continue = true;
  while (should_continue && !atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down)) {
    log_debug_every(LOG_RATE_SLOW, "Audio render loop iteration for client %u", thread_client_id);

    // Check for immediate shutdown
    if (atomic_load(&g_server_should_exit)) {
      log_debug("Audio render thread stopping for client %u (g_server_should_exit)", thread_client_id);
      break;
    }

    // Check thread state before acquiring any locks to prevent use-after-destroy.
    // If we acquire locks after client is being destroyed, we'll crash with SIGSEGV
    should_continue = (((int)atomic_load(&client->audio_render_thread_running) != 0) &&
                       ((int)atomic_load(&client->active) != 0) && !atomic_load(&client->shutting_down));

    if (!should_continue) {
      log_debug("Audio render thread stopping for client %u (should_continue=false)", thread_client_id);
      break;
    }

    if (!g_audio_mixer) {
      log_dev_every(10 * NS_PER_MS_INT, "Audio render waiting for mixer (client %u)", thread_client_id);
      // Check shutdown flag while waiting
      if (atomic_load(&g_server_should_exit))
        break;
      platform_sleep_ns(10 * NS_PER_MS_INT);
      continue;
    }

    // Optimization: No mutex needed - all fields are atomic or stable.
    // client_id: atomic_uint - use atomic_load for thread safety
    // active: atomic_bool - use atomic_load
    // audio_queue: Assigned once at init and never changes
    const char *client_id_snapshot = client->client_id;         // Atomic read
    bool active_snapshot = atomic_load(&client->active);        // Atomic read
    packet_queue_t *audio_queue_snapshot = client->audio_queue; // Stable after init

    // Check if client is still active after getting snapshot
    if (!active_snapshot || !audio_queue_snapshot) {
      break;
    }

    // Create mix excluding THIS client's audio using snapshot data
    START_TIMER("mix_%s", client_id_snapshot);

    // ADAPTIVE READING: Read more samples when we're behind to catch up
    // Normal: 480 samples per 10ms iteration
    // When behind: read up to 960 samples to catch up faster
    // Check source buffer levels to decide
    int samples_to_read = 480; // Default: 10ms worth

    // Log latency at each stage in the server pipeline
    // NOTE: Skipping mixer source latency logging due to potential use-after-free
    // when source IDs are freed during concurrent client cleanup
    if (g_audio_mixer) {
      // Log outgoing queue latency
      size_t queue_depth = packet_queue_size(audio_queue_snapshot);
      float queue_latency_ms = (float)queue_depth * 20.0f; // ~20ms per Opus packet
      log_dev_every(5 * NS_PER_MS_INT, "LATENCY: Server send queue for client %s: %.1fms (%zu packets)",
                    client_id_snapshot, queue_latency_ms, queue_depth);
    }

    int samples_mixed = 0;
    if (GET_OPTION(no_audio_mixer)) {
      // Disable mixer.h processing: simple mixing without ducking/compression/etc
      // Just add audio from all sources except this client, no processing
      SAFE_MEMSET(mix_buffer, samples_to_read * sizeof(float), 0, samples_to_read * sizeof(float));

      if (g_audio_mixer) {
        int max_samples_in_frame = 0;
        // Simple mixing: just add all sources except current client
        for (int i = 0; i < g_audio_mixer->max_sources; i++) {
          if (g_audio_mixer->source_ids[i] && strcmp(g_audio_mixer->source_ids[i], client_id_snapshot) != 0 &&
              g_audio_mixer->source_buffers[i]) {
            // Read from this source and add to mix buffer
            float temp_buffer[960]; // Max adaptive read size
            int samples_read =
                (int)audio_ring_buffer_read(g_audio_mixer->source_buffers[i], temp_buffer, samples_to_read);

            // Track the maximum samples we got from any source
            if (samples_read > max_samples_in_frame) {
              max_samples_in_frame = samples_read;
            }

            // Add to mix buffer
            for (int j = 0; j < samples_read; j++) {
              mix_buffer[j] += temp_buffer[j];
            }
          }
        }
        samples_mixed = max_samples_in_frame; // Only count samples we actually read
      }

      log_debug_every(LOG_RATE_DEFAULT,
                      "Audio mixer DISABLED (--no-audio-mixer): simple mixing, samples=%d for client %u", samples_mixed,
                      client_id_snapshot);
    } else {
      // Use adaptive sample count in normal mixer mode
      uint32_t client_id_hash = fnv1a_hash_string(client_id_snapshot);
      samples_mixed = mixer_process_excluding_source(g_audio_mixer, mix_buffer, samples_to_read, client_id_hash);
    }

    STOP_TIMER_AND_LOG_EVERY(dev, NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "mix_%s", "Mixer for client %s: took",
                             client_id_snapshot);

    // Debug logging every 100 iterations (disabled - can slow down audio rendering)
    // log_debug_every(LOG_RATE_SLOW, "Audio render for client %u: samples_mixed=%d", client_id_snapshot,
    // samples_mixed);

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
      // OPTIMIZATION: Don't check queue depth every iteration - it's expensive (requires lock)
      // Only check periodically every 100 iterations (~0.6s at 172 fps)
      // NOTE: backpressure_check_counter is now per-thread (not static), so each client thread has its own counter
      bool apply_backpressure = false;

      if (++backpressure_check_counter >= 100) {
        backpressure_check_counter = 0;
        size_t queue_depth = packet_queue_size(audio_queue_snapshot);
        // Opus frames are produced at ~50 FPS (20ms each), so 50 packets = 1 second
        // Keep latency bounded to ~1s max in the send queue
        apply_backpressure = (queue_depth > 50); // > 50 packets = ~1s buffered at 50 FPS

        if (apply_backpressure) {
          log_warn_every(4500 * US_PER_MS_INT,
                         "Audio backpressure for client %s: queue depth %zu packets (%.1fs buffered)",
                         client_id_snapshot, queue_depth, (float)queue_depth / 50.0f);
        }
      }

      if (apply_backpressure) {
        // Skip this packet to let the queue drain
        // Reset accumulation buffer so fresh samples can be captured on next iteration.
        // Without this reset, we'd loop forever with stale audio and no space for new samples
        opus_frame_accumulated = 0;
        platform_sleep_ns(5800 * NS_PER_US_INT);
        continue;
      }

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
                        "Server audio frame #%d for client %u: samples_mixed=%d, Peak=%.6f, RMS=%.6f, opus_size=%d, "
                        "first4=[%.4f,%.4f,%.4f,%.4f]",
                        server_audio_frame_count, client_id_snapshot, samples_mixed, peak, rms, opus_size,
                        opus_frame_buffer[0], opus_frame_buffer[1], opus_frame_buffer[2], opus_frame_buffer[3]);
        }
      }

      // Always reset accumulation buffer after attempting to encode - we've consumed these samples
      // If we don't reset, new audio samples would be dropped while stale data sits in the buffer
      opus_frame_accumulated = 0;

      if (opus_size <= 0) {
        log_error("Failed to encode audio to Opus for client %s: opus_size=%d", client_id_snapshot, opus_size);
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

        // Calculate total packet size: 12-byte header + 2-byte frame size + opus data
        const size_t header_size = 12; // 3 uint32s
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

          // Write frame size (2 bytes, network byte order)
          uint16_t fs_net = htons(frame_size);
          memcpy(ptr, &fs_net, 2);
          ptr += 2;

          // Copy Opus data
          memcpy(ptr, opus_buffer, (size_t)opus_size);

          // Queue the properly formatted packet
          START_TIMER("audio_queue_%s", client_id_snapshot);

          int result = packet_queue_enqueue(audio_queue_snapshot, PACKET_TYPE_AUDIO_OPUS_BATCH, packet_buffer,
                                            total_packet_size, 0, true);

          STOP_TIMER_AND_LOG_EVERY(dev, NS_PER_SEC_INT, 1 * NS_PER_MS_INT, "audio_queue_%s",
                                   "Audio queue for client %s: took", client_id_snapshot);

          if (result < 0) {
            log_debug("Failed to queue Opus audio for client %s", client_id_snapshot);
          } else {
            // FPS tracking - audio packet successfully queued (handles lag detection and periodic reporting)
            fps_frame_ns(&audio_fps_tracker, time_get_ns(), "audio packet queued");
          }

          // Free the allocated packet buffer after queuing (packet_queue_enqueue copies data)
          SAFE_FREE(packet_buffer);
        }
      }
      // NOTE: opus_frame_accumulated is already reset at line 928 after encode attempt
    }

    // Audio mixing rate limiting using adaptive sleep system
    // Target: 10ms intervals (100 FPS) for 480 samples @ 48kHz
    // Use queue_depth=0 and target_depth=0 for constant-rate audio processing
    adaptive_sleep_do(&audio_sleep_state, 0, 0);
  }

#ifdef DEBUG_THREADS
  log_debug("Audio render thread stopped for client %u", thread_client_id);
#endif

  // Clean up Opus encoder
  if (opus_encoder) {
    opus_codec_destroy(opus_encoder);
  }

  // Clean up thread-local error context before exit
  asciichat_errno_destroy();

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
  log_info("★★★ create_client_render_threads() CALLED for client_id=%u", client ? client->client_id : 0);

  if (!server_ctx || !client) {
    log_error("Cannot create render threads: NULL %s", !server_ctx ? "server_ctx" : "client");
    return -1;
  }

#ifdef DEBUG_THREADS
  log_debug("Creating render threads for client %u", client->client_id);
#endif

  // NOTE: Mutexes are already initialized in add_client() before any threads start
  // This prevents race conditions where receive thread tries to use uninitialized mutexes

  // Initialize render thread control flags
  // IMPORTANT: Set to true BEFORE creating thread to avoid race condition
  // where thread starts and immediately exits because flag is false
  atomic_store(&client->video_render_thread_running, true);
  atomic_store(&client->audio_render_thread_running, true);

  // Create video rendering thread (stop_id=2, stop after receive thread)
  char thread_name[64];
  safe_snprintf(thread_name, sizeof(thread_name), "video_render_%s", client->client_id);
  asciichat_error_t video_result = tcp_server_spawn_thread(server_ctx->tcp_server, client->socket,
                                                           client_video_render_thread, client, 2, thread_name);
  if (video_result != ASCIICHAT_OK) {
    // Reset flag since thread creation failed
    atomic_store(&client->video_render_thread_running, false);
    // Mutexes will be destroyed by remove_client() which called us
    return -1;
  }

  // Create audio rendering thread (stop_id=2, same priority as video)
  safe_snprintf(thread_name, sizeof(thread_name), "audio_render_%s", client->client_id);
  asciichat_error_t audio_result = tcp_server_spawn_thread(server_ctx->tcp_server, client->socket,
                                                           client_audio_render_thread, client, 2, thread_name);
  if (audio_result != ASCIICHAT_OK) {
    // Clean up video thread (atomic operation, no mutex needed)
    atomic_store(&client->video_render_thread_running, false);
    // Reset audio flag since thread creation failed
    atomic_store(&client->audio_render_thread_running, false);
    // tcp_server_stop_client_threads() will be called by remove_client()
    // to clean up the video thread we just created
    // Mutexes will be destroyed by remove_client() which called us
    return -1;
  }

#ifdef DEBUG_THREADS
  log_debug("Created render threads for client %u", client->client_id);
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
 * - asciichat_thread_join() waits for complete thread exit
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
 * @see asciichat_thread_join() For platform-specific joining
 */
void stop_client_render_threads(client_info_t *client) {
  if (!client) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client is NULL");
    return;
  }

  log_debug("Stopping render threads for client %u", client->client_id);

  // Signal threads to stop (atomic operations, no mutex needed)
  atomic_store(&client->video_render_thread_running, false);
  atomic_store(&client->audio_render_thread_running, false);

  // Wait for threads to finish (deterministic cleanup)
  // During shutdown, don't wait forever for threads to join
  bool is_shutting_down = atomic_load(&g_server_should_exit);

  if (asciichat_thread_is_initialized(&client->video_render_thread)) {
    log_debug("Joining video render thread for client %u", client->client_id);
    int result;
    if (is_shutting_down) {
      // During shutdown, don't timeout - wait for thread to exit
      // Timeouts mask the real problem: threads that are still running
      log_debug("Shutdown mode: joining video render thread for client %u (no timeout)", client->client_id);
      result = asciichat_thread_join(&client->video_render_thread, NULL);
      if (result != 0) {
        log_warn("Video render thread for client %u failed to join during shutdown: %s", client->client_id,
                 SAFE_STRERROR(result));
      }
    } else {
      log_debug("Calling asciichat_thread_join for video thread of client %u", client->client_id);
      result = asciichat_thread_join(&client->video_render_thread, NULL);
      log_debug("asciichat_thread_join returned %d for video thread of client %u", result, client->client_id);
    }

    if (result == 0) {
#ifdef DEBUG_THREADS
      log_debug("Video render thread joined for client %u", client->client_id);
#endif
    } else if (result != -2) { // Don't log timeout errors again
      if (is_shutting_down) {
        log_warn("Failed to join video render thread for client %u during shutdown (continuing): %s", client->client_id,
                 SAFE_STRERROR(result));
      } else {
        log_error("Failed to join video render thread for client %u: %s", client->client_id, SAFE_STRERROR(result));
      }
    }
    // Clear thread handle safely using platform abstraction
    asciichat_thread_init(&client->video_render_thread);
  }

  if (asciichat_thread_is_initialized(&client->audio_render_thread)) {
    int result;
    if (is_shutting_down) {
      // During shutdown, don't timeout - wait for thread to exit
      // Timeouts mask the real problem: threads that are still running
      log_debug("Shutdown mode: joining audio render thread for client %u (no timeout)", client->client_id);
      result = asciichat_thread_join(&client->audio_render_thread, NULL);
      if (result != 0) {
        log_warn("Audio render thread for client %u failed to join during shutdown: %s", client->client_id,
                 SAFE_STRERROR(result));
      }
    } else {
      result = asciichat_thread_join(&client->audio_render_thread, NULL);
    }

    if (result == 0) {
#ifdef DEBUG_THREADS
      log_debug("Audio render thread joined for client %u", client->client_id);
#endif
    } else if (result != -2) { // Don't log timeout errors again
      if (is_shutting_down) {
        log_warn("Failed to join audio render thread for client %u during shutdown (continuing): %s", client->client_id,
                 SAFE_STRERROR(result));
      } else {
        log_error("Failed to join audio render thread for client %u: %s", client->client_id, SAFE_STRERROR(result));
      }
    }
    // Clear thread handle safely using platform abstraction
    asciichat_thread_init(&client->audio_render_thread);
  }

  // DO NOT destroy the mutex here - client.c will handle it
  // mutex_destroy(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_debug("Successfully destroyed render threads for client %u", client->client_id);
#endif
}
