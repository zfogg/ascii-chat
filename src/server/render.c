/**
 * @file server/render.c
 * @ingroup server_render
 * @brief 🎨 Per-client rendering threads: 60fps video and 172fps audio processing with rate limiting
 * ======================
 * 1. Manage per-client video rendering threads (60fps per client)
 * 2. Manage per-client audio rendering threads (172fps per client)
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
 *    - Processes audio at 172fps (5.8ms intervals)
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
 * - Audio: 5.8ms intervals (172fps)
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
 * - Fixed 5.8ms intervals to match buffer size
 * - Matches PortAudio callback frequency
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
 * CRITICAL SYNCHRONIZATION POINTS:
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

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "render.h"
#include "client.h"
#include "stream.h"
#include "protocol.h"
#include "common.h"
#include "options.h"
#include "platform/abstraction.h"
#include "platform/init.h"
#include "packet_queue.h"
#include "util/time.h"
#include "audio/mixer.h"
#include "audio/audio.h"
#include "audio/opus_codec.h"
#include "util/format.h"

// Global client manager lock for thread-safe access
extern rwlock_t g_client_manager_rwlock;

/**
 * @brief Global shutdown flag from main.c - coordinate graceful thread termination
 *
 * All render threads monitor this flag to detect server shutdown and exit
 * their processing loops gracefully. This prevents resource leaks and ensures
 * clean shutdown behavior.
 */
extern atomic_bool g_server_should_exit;

/**
 * @brief Global audio mixer from main.c - provides multi-client audio mixing
 *
 * Audio render threads use this mixer to combine audio from multiple clients
 * while excluding the target client's own audio (prevents echo/feedback).
 */
extern mixer_t *g_audio_mixer;

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

// Removed interruptible_usleep - using regular platform_sleep_usec instead
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
  // CRITICAL: Use atomic_load for client_id to prevent data races
  uint32_t thread_client_id = atomic_load(&client->client_id);
  socket_t thread_socket = client->socket;

  log_debug("Video render thread: client_id=%u", thread_client_id);

  if (thread_socket == INVALID_SOCKET_VALUE) {
    log_error("Invalid socket in video render thread for client %u", thread_client_id);
    return NULL;
  }

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
  struct timespec last_render_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &last_render_time);

  // FPS tracking for video render thread
  uint64_t video_frame_count = 0;
  struct timespec last_video_fps_report_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &last_video_fps_report_time);
  struct timespec last_video_frame_time = last_render_time;
  int expected_video_fps = client_fps;

  bool should_continue = true;
  while (should_continue && !atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down)) {

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

    // Rate limiting with better shutdown responsiveness
    struct timespec current_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

    // Use microseconds for precision - avoid integer division precision loss
    int64_t elapsed_us = ((int64_t)(current_time.tv_sec - last_render_time.tv_sec) * 1000000LL) +
                         ((int64_t)(current_time.tv_nsec - last_render_time.tv_nsec) / 1000);
    int64_t base_frame_interval_us = (int64_t)base_frame_interval_ms * 1000;

    if (elapsed_us < base_frame_interval_us) {
      long sleep_us = (long)(base_frame_interval_us - elapsed_us);
      // Sleep in small chunks for reasonable shutdown response (balance performance vs responsiveness)
      const long max_sleep_chunk = 5000; // 5ms chunks for good shutdown response without destroying performance
      while (sleep_us > 0 && !atomic_load(&g_server_should_exit)) {
        long chunk = sleep_us > max_sleep_chunk ? max_sleep_chunk : sleep_us;
        platform_sleep_usec(chunk);
        sleep_us -= chunk;

        // Check all shutdown conditions after each tiny sleep
        if (atomic_load(&g_server_should_exit))
          break;

        bool still_running = atomic_load(&client->video_render_thread_running) && atomic_load(&client->active);
        if (!still_running)
          break;
      }
      // Fall through to render frame after sleeping
    }

    // PROFILING: Mark start of frame generation
    struct timespec profile_lock_start, profile_lock_end, profile_video_check_start, profile_video_check_end;
    struct timespec profile_write_start, profile_write_end;

    (void)clock_gettime(CLOCK_MONOTONIC, &profile_lock_start);

    // CRITICAL: Check thread state again BEFORE acquiring locks (client might have been destroyed during sleep)
    should_continue = atomic_load(&client->video_render_thread_running) && atomic_load(&client->active) &&
                      !atomic_load(&client->shutting_down);
    if (!should_continue) {
      break;
    }

    // CRITICAL OPTIMIZATION: No mutex needed - all fields are atomic or stable!
    // client_id: atomic_uint - use atomic_load for thread safety
    // width/height: atomic_ushort - use atomic_load
    // active: atomic_bool - use atomic_load
    uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read
    unsigned short width_snapshot = atomic_load(&client->width);   // Atomic read
    unsigned short height_snapshot = atomic_load(&client->height); // Atomic read
    bool active_snapshot = atomic_load(&client->active);           // Atomic read

    (void)clock_gettime(CLOCK_MONOTONIC, &profile_lock_end);

    // Check if client is still active after getting snapshot
    if (!active_snapshot) {
      break;
    }

    // Phase 2 IMPLEMENTED: Generate frame specifically for THIS client using snapshot data
    size_t frame_size = 0;

    (void)clock_gettime(CLOCK_MONOTONIC, &profile_video_check_start);

    // Check if any clients are sending video
    bool has_video_sources = any_clients_sending_video();

    (void)clock_gettime(CLOCK_MONOTONIC, &profile_video_check_end);

    if (!has_video_sources) {
      // No video sources - skip frame generation but DON'T update last_render_time
      // This ensures the next iteration still maintains proper frame timing
      // DON'T continue here - let the loop update last_render_time at the bottom
      // Fall through to update last_render_time at bottom of loop
      goto skip_frame_generation;
    }

    int sources_count = 0; // Track number of video sources in this frame

    // TIME THE ASCII GENERATION
    struct timespec gen_start, gen_end;
    (void)clock_gettime(CLOCK_MONOTONIC, &gen_start);

    char *ascii_frame = create_mixed_ascii_frame_for_client(client_id_snapshot, width_snapshot, height_snapshot, false,
                                                            &frame_size, NULL, &sources_count);

    (void)clock_gettime(CLOCK_MONOTONIC, &gen_end);
    uint64_t gen_time_us = ((uint64_t)gen_end.tv_sec * 1000000 + (uint64_t)gen_end.tv_nsec / 1000) -
                           ((uint64_t)gen_start.tv_sec * 1000000 + (uint64_t)gen_start.tv_nsec / 1000);

    (void)clock_gettime(CLOCK_MONOTONIC, &profile_write_start);

    // Phase 2 IMPLEMENTED: Write frame to double buffer (never drops!)
    if (ascii_frame && frame_size > 0) {
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
            write_frame->capture_timestamp_us =
                (uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000;

            // Commit the frame (swaps buffers atomically using vfb->swap_mutex, NOT rwlock)
            video_frame_commit(vfb_snapshot);

            // Log occasionally for monitoring
            char pretty_size[64];
            format_bytes_pretty(frame_size, pretty_size, sizeof(pretty_size));

          } else {
            log_warn("Frame too large for buffer: %zu > %zu", frame_size, vfb_snapshot->allocated_buffer_size);
          }

          // FPS tracking - frame successfully generated
          video_frame_count++;

          // Calculate time since last frame
          uint64_t frame_interval_us =
              ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
              ((uint64_t)last_video_frame_time.tv_sec * 1000000 + (uint64_t)last_video_frame_time.tv_nsec / 1000);
          last_video_frame_time = current_time;

          // Expected frame interval in microseconds
          uint64_t expected_interval_us = 1000000 / expected_video_fps;
          uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

          // Log error if frame took too long to generate
          static int lag_counter = 0;
          if (video_frame_count > 1 && frame_interval_us > lag_threshold_us) {
            lag_counter++;
            if (lag_counter % 10 == 0) {
              log_warn_every(
                  1000000,
                  "SERVER VIDEO LAG: Client %u frame rendered %.1fms late (expected %.1fms, got %.1fms, actual "
                  "fps: %.1f)",
                  thread_client_id, (float)(frame_interval_us - expected_interval_us) / 1000.0f,
                  (float)expected_interval_us / 1000.0f, (float)frame_interval_us / 1000.0f,
                  1000000.0f / frame_interval_us);
            }
          }

          // Report FPS every 5 seconds
          uint64_t elapsed_us = ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
                                ((uint64_t)last_video_fps_report_time.tv_sec * 1000000 +
                                 (uint64_t)last_video_fps_report_time.tv_nsec / 1000);

          if (elapsed_us >= 5000000) { // 5 seconds
            float elapsed_seconds = (float)elapsed_us / 1000000.0f;
            float actual_fps = (float)video_frame_count / elapsed_seconds;

            char duration_str[32];
            format_duration_s((double)elapsed_seconds, duration_str, sizeof(duration_str));
            log_debug("SERVER VIDEO FPS: Client %u: %.1f fps (%llu frames in %s)", thread_client_id, actual_fps,
                      video_frame_count, duration_str);
            // Reset counters for next interval
            video_frame_count = 0;
            last_video_fps_report_time = current_time;
          }
        }
      }

      (void)clock_gettime(CLOCK_MONOTONIC, &profile_write_end);

      SAFE_FREE(ascii_frame);

      // PROFILING: Calculate and log timing breakdown on EVERY frame during debugging
      uint64_t lock_time_us =
          ((uint64_t)profile_lock_end.tv_sec * 1000000 + (uint64_t)profile_lock_end.tv_nsec / 1000) -
          ((uint64_t)profile_lock_start.tv_sec * 1000000 + (uint64_t)profile_lock_start.tv_nsec / 1000);
      uint64_t video_check_time_us =
          ((uint64_t)profile_video_check_end.tv_sec * 1000000 + (uint64_t)profile_video_check_end.tv_nsec / 1000) -
          ((uint64_t)profile_video_check_start.tv_sec * 1000000 + (uint64_t)profile_video_check_start.tv_nsec / 1000);
      uint64_t write_time_us =
          ((uint64_t)profile_write_end.tv_sec * 1000000 + (uint64_t)profile_write_end.tv_nsec / 1000) -
          ((uint64_t)profile_write_start.tv_sec * 1000000 + (uint64_t)profile_write_start.tv_nsec / 1000);
      (void)lock_time_us;
      (void)video_check_time_us;
      (void)gen_time_us;
      (void)write_time_us;
    } else {
      // No frame generated (probably no video sources) - this is normal, no error logging needed
      log_debug_every(3000000, "Per-client render: No video sources available for client %u", client_id_snapshot);
    }

  skip_frame_generation:
    // Update last_render_time to maintain consistent frame timing
    // CRITICAL: Use the current_time captured at the START of this iteration for rate limiting.
    // This ensures we maintain the target frame rate based on when we STARTED processing,
    // not when we FINISHED. This prevents timing drift from frame generation overhead.
    last_render_time = current_time;
  }

#ifdef DEBUG_THREADS
  log_debug("Video render thread stopped for client %u", thread_client_id);
#endif

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
 * for a specific client at 172fps (5.8ms intervals). Each client receives
 * audio from all other clients while excluding their own audio to prevent
 * echo and feedback.
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
 *    - Sleep for precise timing (5.8ms intervals)
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
 * - Target: 172fps (5.8ms intervals)
 * - Matches PortAudio callback frequency
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

  if (!client || client->socket == INVALID_SOCKET_VALUE) {
    log_error("Invalid client info in audio render thread");
    return NULL;
  }

  // Take snapshot of client ID and display name at start to avoid race conditions
  // CRITICAL: Use atomic_load for client_id to prevent data races
  uint32_t thread_client_id = atomic_load(&client->client_id);
  char thread_display_name[64];

  // LOCK OPTIMIZATION: Only need client_state_mutex, not global rwlock
  // We already have a stable client pointer
  mutex_lock(&client->client_state_mutex);
  SAFE_STRNCPY(thread_display_name, client->display_name, sizeof(thread_display_name));
  mutex_unlock(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_debug("Audio render thread started for client %u (%s)", thread_client_id, thread_display_name);
#endif

  // Mix buffer: 480 samples = 10ms @ 48kHz (matches loop iteration timing)
  // Sized to ensure 960 samples accumulate in ~2 iterations for 20ms packets
  float mix_buffer[480];

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
  uint64_t audio_packet_count = 0;
  struct timespec last_audio_fps_report_time;
  struct timespec last_audio_packet_time;
  struct timespec last_packet_send_time; // For time-based packet transmission (every 20ms)
  (void)clock_gettime(CLOCK_MONOTONIC, &last_audio_fps_report_time);
  (void)clock_gettime(CLOCK_MONOTONIC, &last_audio_packet_time);
  (void)clock_gettime(CLOCK_MONOTONIC, &last_packet_send_time);
  int expected_audio_fps = AUDIO_RENDER_FPS; // Based on AUDIO_FRAMES_PER_BUFFER / AUDIO_SAMPLE_RATE

  bool should_continue = true;
  while (should_continue && !atomic_load(&g_server_should_exit) && !atomic_load(&client->shutting_down)) {
    // Capture loop start time for precise timing
    struct timespec loop_start_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &loop_start_time);

    log_debug_every(10000000, "Audio render loop iteration for client %u", thread_client_id);

    // Check for immediate shutdown
    if (atomic_load(&g_server_should_exit)) {
      log_debug("Audio render thread stopping for client %u (g_server_should_exit)", thread_client_id);
      break;
    }

    // CRITICAL: Check thread state BEFORE acquiring any locks to prevent use-after-destroy
    // If we acquire locks after client is being destroyed, we'll crash with SIGSEGV
    should_continue = (((int)atomic_load(&client->audio_render_thread_running) != 0) &&
                       ((int)atomic_load(&client->active) != 0) && !atomic_load(&client->shutting_down));

    if (!should_continue) {
      log_debug("Audio render thread stopping for client %u (should_continue=false)", thread_client_id);
      break;
    }

    if (!g_audio_mixer) {
      log_info_every(1000000, "Audio render waiting for mixer (client %u)", thread_client_id);
      // Check shutdown flag while waiting
      if (atomic_load(&g_server_should_exit))
        break;
      platform_sleep_usec(10000);
      continue;
    }

    // CRITICAL OPTIMIZATION: No mutex needed - all fields are atomic or stable!
    // client_id: atomic_uint - use atomic_load for thread safety
    // active: atomic_bool - use atomic_load
    // audio_queue: Assigned once at init and never changes
    uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read
    bool active_snapshot = atomic_load(&client->active);           // Atomic read
    packet_queue_t *audio_queue_snapshot = client->audio_queue;    // Stable after init

    // Check if client is still active after getting snapshot
    if (!active_snapshot || !audio_queue_snapshot) {
      break;
    }

    // Create mix excluding THIS client's audio using snapshot data
    struct timespec mix_start_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &mix_start_time);

    int samples_mixed = 0;
    if (opt_no_audio_mixer) {
      // Disable mixer.h processing: simple mixing without ducking/compression/etc
      // Just add audio from all sources except this client, no processing
      //
      // CRITICAL TIMING FIX: Read 480 samples per 10ms loop iteration
      // At 48kHz sample rate, 10ms = 480 samples
      // This ensures 960 samples accumulate in ~2 iterations = ~20ms packets
      // (Previously read only 256 samples per iteration, causing 40ms packets and stuttering)
      const int samples_per_iteration = 480; // 48kHz * 10ms

      SAFE_MEMSET(mix_buffer, samples_per_iteration * sizeof(float), 0, samples_per_iteration * sizeof(float));

      if (g_audio_mixer) {
        int max_samples_in_frame = 0;
        // Simple mixing: just add all sources except current client
        for (int i = 0; i < g_audio_mixer->max_sources; i++) {
          if (g_audio_mixer->source_ids[i] != 0 && g_audio_mixer->source_ids[i] != client_id_snapshot &&
              g_audio_mixer->source_buffers[i]) {
            // Read from this source and add to mix buffer
            float temp_buffer[480];
            int samples_read =
                (int)audio_ring_buffer_read(g_audio_mixer->source_buffers[i], temp_buffer, samples_per_iteration);

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

      log_debug_every(5000000, "Audio mixer DISABLED (--no-audio-mixer): simple mixing, samples=%d for client %u",
                      samples_mixed, client_id_snapshot);
    } else {
      // Also use 480 samples per iteration in normal mixer mode for consistent timing
      samples_mixed = mixer_process_excluding_source(g_audio_mixer, mix_buffer, 480, client_id_snapshot);
    }

    struct timespec mix_end_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &mix_end_time);
    uint64_t mix_time_us = ((uint64_t)mix_end_time.tv_sec * 1000000 + (uint64_t)mix_end_time.tv_nsec / 1000) -
                           ((uint64_t)mix_start_time.tv_sec * 1000000 + (uint64_t)mix_start_time.tv_nsec / 1000);

    if (mix_time_us > 2000) { // Log if mixing takes > 2ms
      log_warn_every(5000000, "Slow mixer for client %u: took %lluus (%.2fms)", client_id_snapshot, mix_time_us,
                     (float)mix_time_us / 1000.0f);
    }

    // Debug logging every 100 iterations (disabled - can slow down audio rendering)
    // log_debug_every(10000000, "Audio render for client %u: samples_mixed=%d", client_id_snapshot, samples_mixed);

    // Accumulate all samples (including 0 or partial) until we have a full Opus frame
    // This maintains continuous stream without silence padding
    struct timespec accum_start = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &accum_start);

    int space_available = OPUS_FRAME_SAMPLES - opus_frame_accumulated;
    int samples_to_copy = (samples_mixed <= space_available) ? samples_mixed : space_available;

    // Only copy if we have samples, otherwise just wait for next frame
    if (samples_to_copy > 0) {
      SAFE_MEMCPY(opus_frame_buffer + opus_frame_accumulated,
                  (OPUS_FRAME_SAMPLES - opus_frame_accumulated) * sizeof(float), mix_buffer,
                  samples_to_copy * sizeof(float));
      opus_frame_accumulated += samples_to_copy;
    }

    struct timespec accum_end = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &accum_end);
    uint64_t accum_time_us = ((uint64_t)accum_end.tv_sec * 1000000 + (uint64_t)accum_end.tv_nsec / 1000) -
                             ((uint64_t)accum_start.tv_sec * 1000000 + (uint64_t)accum_start.tv_nsec / 1000);

    if (accum_time_us > 500) {
      log_warn_every(5000000, "Slow accumulate for client %u: took %lluus", client_id_snapshot, accum_time_us);
    }

    // Only encode and send when we have accumulated a full Opus frame
    if (opus_frame_accumulated >= OPUS_FRAME_SAMPLES) {
      // OPTIMIZATION: Don't check queue depth every iteration - it's expensive (requires lock)
      // Only check periodically every 100 iterations (~0.6s at 172 fps)
      static int backpressure_check_counter = 0;
      bool apply_backpressure = false;

      if (++backpressure_check_counter >= 100) {
        backpressure_check_counter = 0;
        size_t queue_depth = packet_queue_size(audio_queue_snapshot);
        apply_backpressure = (queue_depth > 1000); // > 1000 packets = 5.8s buffered

        if (apply_backpressure) {
          log_warn("Audio backpressure for client %u: queue depth %zu packets (%.1fs buffered)", client_id_snapshot,
                   queue_depth, (float)queue_depth / 172.0f);
        }
      }

      if (apply_backpressure) {
        // Skip this packet to let the queue drain
        // CRITICAL: Reset accumulation buffer so fresh samples can be captured on next iteration
        // Without this reset, we'd loop forever with stale audio and no space for new samples
        opus_frame_accumulated = 0;
        platform_sleep_usec(5800);
        continue;
      }

      // Encode accumulated Opus frame (960 samples = 20ms @ 48kHz)
      uint8_t opus_buffer[1024]; // Max Opus frame size

      struct timespec opus_start_time;
      (void)clock_gettime(CLOCK_MONOTONIC, &opus_start_time);

      int opus_size =
          opus_codec_encode(opus_encoder, opus_frame_buffer, OPUS_FRAME_SAMPLES, opus_buffer, sizeof(opus_buffer));

      struct timespec opus_end_time;
      (void)clock_gettime(CLOCK_MONOTONIC, &opus_end_time);
      uint64_t opus_time_us = ((uint64_t)opus_end_time.tv_sec * 1000000 + (uint64_t)opus_end_time.tv_nsec / 1000) -
                              ((uint64_t)opus_start_time.tv_sec * 1000000 + (uint64_t)opus_start_time.tv_nsec / 1000);

      if (opus_time_us > 2000) { // Log if encoding takes > 2ms
        log_warn_every(5000000, "Slow Opus encode for client %u: took %lluus (%.2fms), size=%d", client_id_snapshot,
                       opus_time_us, (float)opus_time_us / 1000.0f, opus_size);
      }

      // Always reset accumulation buffer after attempting to encode - we've consumed these samples
      // If we don't reset, new audio samples would be dropped while stale data sits in the buffer
      opus_frame_accumulated = 0;

      if (opus_size <= 0) {
        log_error("Failed to encode audio to Opus for client %u: opus_size=%d", client_id_snapshot, opus_size);
      } else {
        // Queue Opus-encoded audio for this specific client
        struct timespec queue_start = {0};
        (void)clock_gettime(CLOCK_MONOTONIC, &queue_start);

        int result =
            packet_queue_enqueue(audio_queue_snapshot, PACKET_TYPE_AUDIO_OPUS, opus_buffer, (size_t)opus_size, 0, true);

        struct timespec queue_end = {0};
        (void)clock_gettime(CLOCK_MONOTONIC, &queue_end);
        uint64_t queue_time_us = ((uint64_t)queue_end.tv_sec * 1000000 + (uint64_t)queue_end.tv_nsec / 1000) -
                                 ((uint64_t)queue_start.tv_sec * 1000000 + (uint64_t)queue_start.tv_nsec / 1000);

        if (queue_time_us > 500) {
          log_warn_every(5000000, "Slow queue for client %u: took %lluus", client_id_snapshot, queue_time_us);
        }

        if (result < 0) {
          log_debug("Failed to queue Opus audio for client %u", client_id_snapshot);
        } else {
          // FPS tracking - audio packet successfully queued
          audio_packet_count++;

          struct timespec current_time;
          (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

          // Calculate time since last packet
          uint64_t packet_interval_us =
              ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
              ((uint64_t)last_audio_packet_time.tv_sec * 1000000 + (uint64_t)last_audio_packet_time.tv_nsec / 1000);
          last_audio_packet_time = current_time;

          // Expected packet interval in microseconds (5800us for 172fps)
          uint64_t expected_interval_us = 1000000 / expected_audio_fps;
          uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

          // Log warning if packet took too long to process
          if (audio_packet_count > 1 && packet_interval_us > lag_threshold_us) {
            log_warn_every(
                1000000,
                "SERVER AUDIO LAG: Client %u packet processed %.1fms late (expected %.1fms, got %.1fms, actual "
                "fps: %.1f)",
                thread_client_id, (float)(packet_interval_us - expected_interval_us) / 1000.0f,
                (float)expected_interval_us / 1000.0f, (float)packet_interval_us / 1000.0f,
                1000000.0f / packet_interval_us);
          }

          // Report FPS every 5 seconds
          uint64_t elapsed_us = ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
                                ((uint64_t)last_audio_fps_report_time.tv_sec * 1000000 +
                                 (uint64_t)last_audio_fps_report_time.tv_nsec / 1000);

          if (elapsed_us >= 5000000) { // 5 seconds
            float elapsed_seconds = (float)elapsed_us / 1000000.0f;
            float actual_fps = (float)audio_packet_count / elapsed_seconds;

            char duration_str[32];
            format_duration_s((double)elapsed_seconds, duration_str, sizeof(duration_str));
            log_debug("SERVER AUDIO FPS: Client %u: %.1f fps (%llu packets in %s)", thread_client_id, actual_fps,
                      audio_packet_count, duration_str);

            // Reset counters for next interval
            audio_packet_count = 0;
            last_audio_fps_report_time = current_time;
          }
        }
      }
      // NOTE: opus_frame_accumulated is already reset at line 928 after encode attempt
    }

    // Audio mixing rate - 5.8ms to match buffer size
    // Calculate elapsed time and sleep for remainder to maintain constant FPS
    struct timespec loop_end_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &loop_end_time);

    uint64_t loop_elapsed_us = ((uint64_t)loop_end_time.tv_sec * 1000000 + (uint64_t)loop_end_time.tv_nsec / 1000) -
                               ((uint64_t)loop_start_time.tv_sec * 1000000 + (uint64_t)loop_start_time.tv_nsec / 1000);

    // Target loop time: 10ms (two iterations per Opus frame)
    // Balances between fast ring buffer consumption and accumulation time
    // Too fast (5.3ms) = frequent empty reads; too slow (20ms) = buffer accumulation issues
    const uint64_t target_loop_us = 10000; // 10ms = reasonable compromise
    long remaining_sleep_us;

    if (loop_elapsed_us >= target_loop_us) {
      // Processing took longer than target - skip sleep but warn
      log_warn_every(1000000, "Audio processing took %lluus (%.1fms) - exceeds target %lluus (%.1fms) for client %u",
                     loop_elapsed_us, (float)loop_elapsed_us / 1000.0f, target_loop_us, (float)target_loop_us / 1000.0f,
                     thread_client_id);
      remaining_sleep_us = 0;
    } else {
      // Sleep for remaining time to maintain constant FPS
      remaining_sleep_us = (long)(target_loop_us - loop_elapsed_us);
    }

    // Sleep in small chunks for better shutdown responsiveness
    const long sleep_chunk = 1000; // 1ms chunks for reasonable shutdown response
    while (remaining_sleep_us > 0 && !atomic_load(&g_server_should_exit)) {
      long chunk = remaining_sleep_us > sleep_chunk ? sleep_chunk : remaining_sleep_us;
      platform_sleep_usec(chunk);
      remaining_sleep_us -= chunk;

      // Check all shutdown conditions
      if (atomic_load(&g_server_should_exit))
        break;

      bool still_running = atomic_load(&client->audio_render_thread_running) && atomic_load(&client->active);
      if (!still_running)
        break;
    }
  }

#ifdef DEBUG_THREADS
  log_debug("Audio render thread stopped for client %u", thread_client_id);
#endif

  // Clean up Opus encoder
  if (opus_encoder) {
    opus_codec_destroy(opus_encoder);
  }

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

int create_client_render_threads(client_info_t *client) {
  if (!client) {
    log_error("Cannot create render threads for NULL client");
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

  // Create video rendering thread
  log_debug("Creating video render thread for client %u", client->client_id);
  if (ascii_thread_create(&client->video_render_thread, client_video_render_thread, client) != 0) {
    log_error("Failed to create video render thread for client %u", client->client_id);
    // Reset flag since thread creation failed
    atomic_store(&client->video_render_thread_running, false);
    // Mutexes will be destroyed by remove_client() which called us
    return -1;
  }
  log_debug("Successfully created video render thread for client %u", client->client_id);

  // Create audio rendering thread
  if (ascii_thread_create(&client->audio_render_thread, client_audio_render_thread, client) != 0) {
    log_error("Failed to create audio render thread for client %u", client->client_id);
    // Clean up video thread (atomic operation, no mutex needed)
    atomic_store(&client->video_render_thread_running, false);
    // Reset audio flag since thread creation failed
    atomic_store(&client->audio_render_thread_running, false);
    // Note: thread cancellation not available in platform abstraction
    ascii_thread_join(&client->video_render_thread, NULL);
    // Mutexes will be destroyed by remove_client() which called us
    return -1;
  }
  log_debug("Created audio render thread for client %u", client->client_id);

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
 * - ascii_thread_join() waits for complete thread exit
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
 * @see ascii_thread_join() For platform-specific joining
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

  if (ascii_thread_is_initialized(&client->video_render_thread)) {
    log_debug("Joining video render thread for client %u", client->client_id);
    int result;
    if (is_shutting_down) {
      // During shutdown, give thread a brief chance to exit cleanly with timeout
      log_debug("Shutdown mode: joining video render thread for client %u with 100ms timeout", client->client_id);
      result = ascii_thread_join_timeout(&client->video_render_thread, NULL, 100);
      if (result == -2) {
        log_warn("Video render thread for client %u timed out during shutdown (continuing)", client->client_id);
        // Don't call CloseHandle/cleanup on timeout - thread might still be running
        // Use platform-safe thread initialization
        ascii_thread_init(&client->video_render_thread);
        // Continue with audio thread join and cleanup instead of returning early
      }
    } else {
      log_debug("Calling ascii_thread_join for video thread of client %u", client->client_id);
      result = ascii_thread_join(&client->video_render_thread, NULL);
      log_debug("ascii_thread_join returned %d for video thread of client %u", result, client->client_id);
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
    ascii_thread_init(&client->video_render_thread);
  }

  if (ascii_thread_is_initialized(&client->audio_render_thread)) {
    int result;
    if (is_shutting_down) {
      log_debug("Shutdown mode: joining audio render thread for client %u with 100ms timeout", client->client_id);
      result = ascii_thread_join_timeout(&client->audio_render_thread, NULL, 100);
      if (result == -2) {
        log_warn("Audio render thread for client %u timed out during shutdown (continuing)", client->client_id);
        // Use platform-safe thread initialization
        ascii_thread_init(&client->audio_render_thread);
        // Continue with cleanup even if thread timed out
      }
    } else {
      result = ascii_thread_join(&client->audio_render_thread, NULL);
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
    ascii_thread_init(&client->audio_render_thread);
  }

  // DO NOT destroy the mutex here - client.c will handle it
  // mutex_destroy(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_debug("Successfully destroyed render threads for client %u", client->client_id);
#endif
}
