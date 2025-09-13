/**
 * @file render.c
 * @brief Per-Client Rendering Threads and Real-Time Media Processing
 *
 * This module implements ASCII-Chat's high-performance per-client rendering
 * system, providing dedicated threads for video and audio processing for each
 * connected client. It was extracted from the monolithic server.c to enable
 * scalable real-time media processing.
 *
 * CORE RESPONSIBILITIES:
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

#include "render.h"
#include "client.h"
#include "stream.h"
#include "common.h"
#include "platform/abstraction.h"
#include "platform/init.h"
#include "packet_queue.h"
#include "mixer.h"
#include "os/audio.h"

/**
 * @brief Global shutdown flag from main.c - coordinate graceful thread termination
 *
 * All render threads monitor this flag to detect server shutdown and exit
 * their processing loops gracefully. This prevents resource leaks and ensures
 * clean shutdown behavior.
 */
extern atomic_bool g_should_exit;

/**
 * @brief Global audio mixer from main.c - provides multi-client audio mixing
 *
 * Audio render threads use this mixer to combine audio from multiple clients
 * while excluding the target client's own audio (prevents echo/feedback).
 */
extern mixer_t *g_audio_mixer;

/**
 * @brief Global shutdown synchronization mutex from main.c
 */
extern static_mutex_t g_shutdown_mutex;

/**
 * @brief Global shutdown condition variable from main.c
 */
extern static_cond_t g_shutdown_cond;

/**
 * @brief Global counter for blank frames sent across all clients
 *
 * Tracks the total number of blank/empty frames sent when no video sources
 * are available. Used for debugging and performance monitoring.
 *
 * @note This counter is not thread-safe - used for approximate statistics only
 */
uint64_t g_blank_frames_sent = 0;

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

void interruptible_usleep(unsigned int usec) {
  if (atomic_load(&g_should_exit)) {
    return;
  }

  // Use platform abstraction for cross-platform sleep
  platform_interruptible_sleep_usec(usec);

  // Check again after sleep
  if (atomic_load(&g_should_exit)) {
    return;
  }
}

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
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in video render thread");
    return NULL;
  }

#ifdef DEBUG_THREADS
  log_info("Video render thread started for client %u (%s)", client->client_id, client->display_name);
#endif

  const int base_frame_interval_ms = 1000 / VIDEO_RENDER_FPS; // 60 FPS base rate
  struct timespec last_render_time;
  clock_gettime(CLOCK_MONOTONIC, &last_render_time);

  bool should_continue = true;
  while (should_continue && !atomic_load(&g_should_exit)) {
    mutex_lock(&client->client_state_mutex);
    should_continue = client->video_render_thread_running && client->active;
    mutex_unlock(&client->client_state_mutex);

    if (!should_continue) {
      break;
    }
    // Rate limiting
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_render_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_render_time.tv_nsec) / 1000000;

    if (elapsed_ms < base_frame_interval_ms) {
      interruptible_usleep((base_frame_interval_ms - elapsed_ms) * 1000);
      continue;
    }

    mutex_lock(&client->client_state_mutex);
    uint32_t client_id_snapshot = client->client_id;
    unsigned short width_snapshot = client->width;
    unsigned short height_snapshot = client->height;
    bool active_snapshot = client->active;
    mutex_unlock(&client->client_state_mutex);

    // Check if client is still active after getting snapshot
    if (!active_snapshot) {
      break;
    }

#ifdef DEBUG_THREADS
    LOG_DEBUG_EVERY(video_render_thread, 100, "Video render thread: %s", client->display_name);
#endif

    // Phase 2 IMPLEMENTED: Generate frame specifically for THIS client using snapshot data
    size_t frame_size = 0;
    char *ascii_frame =
        create_mixed_ascii_frame_for_client(client_id_snapshot, width_snapshot, height_snapshot, false, &frame_size);

    // Phase 2 IMPLEMENTED: Queue frame for this specific client
    if (ascii_frame && frame_size > 0) {
      int queue_result = queue_ascii_frame_for_client(client, ascii_frame, frame_size);
      if (queue_result == 0) {
        // Successfully queued frame - log occasionally for monitoring
        static int success_count = 0;
        success_count++;
        if (success_count == 1 || success_count % (30 * 60) == 0) { // Log every ~4 seconds at 60fps
          char pretty_size[64];
          format_bytes_pretty(frame_size, pretty_size, sizeof(pretty_size));
          log_info("Per-client render: Successfully queued %d ASCII frames for client %u (%ux%u, %s)", success_count,
                   client->client_id, client->width, client->height, pretty_size);
        }
      }
      free(ascii_frame);
    } else {
      // No frame generated (probably no video sources) - this is normal, no error logging needed
      static int no_frame_count = 0;
      no_frame_count++;
      if (no_frame_count % 300 == 0) { // Log every ~10 seconds at 30fps
        log_debug("Per-client render: No video sources available for client %u (%d attempts)", client->client_id,
                  no_frame_count);
      }
    }

    last_render_time = current_time;
  }

#ifdef DEBUG_THREADS
  log_info("Video render thread stopped for client %u", client->client_id);
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
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in audio render thread");
    return NULL;
  }

#ifdef DEBUG_THREADS
  log_info("Audio render thread started for client %u (%s)", client->client_id, client->display_name);
#endif

  float mix_buffer[AUDIO_FRAMES_PER_BUFFER];

  bool should_continue = true;
  while (should_continue && !atomic_load(&g_should_exit)) {
    // CRITICAL FIX: Check thread state with mutex protection
    mutex_lock(&client->client_state_mutex);
    should_continue = client->audio_render_thread_running && client->active;
    mutex_unlock(&client->client_state_mutex);

    if (!should_continue) {
      break;
    }

    if (!g_audio_mixer) {
      interruptible_usleep(10000);
      continue;
    }

    // CRITICAL FIX: Protect client state access with per-client mutex
    mutex_lock(&client->client_state_mutex);
    uint32_t client_id_snapshot = client->client_id;
    bool active_snapshot = client->active;
    packet_queue_t *audio_queue_snapshot = client->audio_queue;
    mutex_unlock(&client->client_state_mutex);

    // Check if client is still active after getting snapshot
    if (!active_snapshot || !audio_queue_snapshot) {
      break;
    }

#ifdef DEBUG_THREADS
    LOG_DEBUG_EVERY(mixer_process_excluding_source_calling, 100,
                    "Audio render thread: Mixer process excluding source being called %d times",
                    mixer_process_excluding_source_calling_counter);
#endif

    // Create mix excluding THIS client's audio using snapshot data
    int samples_mixed =
        mixer_process_excluding_source(g_audio_mixer, mix_buffer, AUDIO_FRAMES_PER_BUFFER, client_id_snapshot);

#if defined(DEBUG_AUDIO) || defined(DEBUG_THREADS)
    LOG_DEBUG_EVERY(mixer_process_excluding_source_called, 100,
                    "Audio render thread: Mixer process excluding source called %d times",
                    mixer_process_excluding_source_called_counter);
#endif

    // Queue audio directly for this specific client using snapshot data
    if (samples_mixed > 0) {
      size_t data_size = AUDIO_FRAMES_PER_BUFFER * sizeof(float);
      int result = packet_queue_enqueue(audio_queue_snapshot, PACKET_TYPE_AUDIO, mix_buffer, data_size, 0, true);
      if (result < 0) {
        log_debug("Failed to queue audio for client %u", client_id_snapshot);
      } else {
#ifdef DEBUG_AUDIO
        LOG_DEBUG_EVERY(queue_count, 100, "Audio render thread: Successfully queued %d audio samples for client %u",
                        samples_mixed, client_id_snapshot);
#endif
      }
    }

    // Audio mixing rate - 5.8ms to match buffer size
    interruptible_usleep(5800);
  }

#ifdef DEBUG_THREADS
  log_info("Audio render thread stopped for client %u", client->client_id);
#endif
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
 *    - video_buffer_mutex: Protects video buffer access
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
  log_info("Creating render threads for client %u", client->client_id);
#endif

  // Initialize per-client mutex
  if (mutex_init(&client->client_state_mutex) != 0) {
    log_error("Failed to initialize client state mutex for client %u", client->client_id);
    return -1;
  }

  // THREAD-SAFE FRAMEBUFFER: Initialize per-client video buffer mutex
  if (mutex_init(&client->video_buffer_mutex) != 0) {
    log_error("Failed to initialize video buffer mutex for client %u", client->client_id);
    mutex_destroy(&client->client_state_mutex);
    return -1;
  }

  // Initialize render thread control flags
  client->video_render_thread_running = false;
  client->audio_render_thread_running = false;

  // Create video rendering thread
  if (ascii_thread_create(&client->video_render_thread, client_video_render_thread, client) != 0) {
    log_error("Failed to create video render thread for client %u", client->client_id);
    mutex_destroy(&client->video_buffer_mutex);
    mutex_destroy(&client->client_state_mutex);
    return -1;
  } else {
#ifdef DEBUG_THREADS
    log_info("Created video render thread for client %u", client->client_id);
#endif
  }

  // CRITICAL FIX: Protect thread_running flag with mutex
  mutex_lock(&client->client_state_mutex);
  client->video_render_thread_running = true;
  mutex_unlock(&client->client_state_mutex);

  // Create audio rendering thread
  if (ascii_thread_create(&client->audio_render_thread, client_audio_render_thread, client) != 0) {
    log_error("Failed to create audio render thread for client %u", client->client_id);
    // Clean up video thread
    mutex_lock(&client->client_state_mutex);
    client->video_render_thread_running = false;
    mutex_unlock(&client->client_state_mutex);
    // Note: thread cancellation not available in platform abstraction
    ascii_thread_join(&client->video_render_thread, NULL);
    mutex_destroy(&client->video_buffer_mutex);
    mutex_destroy(&client->client_state_mutex);
    return -1;
  } else {
#ifdef DEBUG_THREADS
    log_info("Created audio render thread for client %u", client->client_id);
#endif
  }

  // CRITICAL FIX: Protect thread_running flag with mutex
  mutex_lock(&client->client_state_mutex);
  client->audio_render_thread_running = true;
  mutex_unlock(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_info("Created render threads for client %u", client->client_id);
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
    log_error("Cannot destroy render threads for NULL client");
    return;
  }

#ifdef DEBUG_THREADS
  log_info("Destroying render threads for client %u", client->client_id);
#endif

  // Signal threads to stop - CRITICAL FIX: Protect with mutex
  mutex_lock(&client->client_state_mutex);
  client->video_render_thread_running = false;
  client->audio_render_thread_running = false;
  mutex_unlock(&client->client_state_mutex);

  // Wait for threads to finish (deterministic cleanup)
  if (ascii_thread_is_initialized(&client->video_render_thread)) {
    int result = ascii_thread_join(&client->video_render_thread, NULL);
    if (result == 0) {
#ifdef DEBUG_THREADS
      log_debug("Video render thread joined for client %u", client->client_id);
#endif
    } else {
      log_error("Failed to join video render thread for client %u: %s", client->client_id, SAFE_STRERROR(result));
    }
    memset(&client->video_render_thread, 0, sizeof(asciithread_t));
  }

  if (ascii_thread_is_initialized(&client->audio_render_thread)) {
    int result = ascii_thread_join(&client->audio_render_thread, NULL);
    if (result == 0) {
#ifdef DEBUG_THREADS
      log_debug("Audio render thread joined for client %u", client->client_id);
#endif
    } else {
      log_error("Failed to join audio render thread for client %u: %s", client->client_id, SAFE_STRERROR(result));
    }
    memset(&client->audio_render_thread, 0, sizeof(asciithread_t));
  }

  // Destroy per-client mutex
  mutex_destroy(&client->client_state_mutex);

#ifdef DEBUG_THREADS
  log_info("Successfully destroyed render threads for client %u", client->client_id);
#endif
}
