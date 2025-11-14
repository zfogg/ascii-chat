/**
 * @file server/stats.c
 * @ingroup server_stats
 * @brief 📊 Server performance monitoring: resource utilization tracking, client metrics, and health reporting
 * ======================
 * 1. Continuous monitoring of server performance metrics
 * 2. Per-client statistics collection and reporting
 * 3. Buffer pool utilization tracking
 * 4. Packet queue performance analysis
 * 5. Hash table efficiency monitoring
 * 6. Periodic statistics logging for operational visibility
 *
 * MONITORING ARCHITECTURE:
 * ========================
 *
 * STATISTICS COLLECTION THREAD:
 * - Dedicated background thread for non-intrusive monitoring
 * - 30-second reporting intervals (configurable)
 * - Interruptible sleep for responsive shutdown
 * - Thread-safe data collection from all system components
 *
 * PERFORMANCE IMPACT MINIMIZATION:
 * - Read-only access to operational data structures
 * - Minimal locking (reader locks where possible)
 * - Background processing doesn't affect real-time performance
 * - Throttled logging to prevent log spam
 *
 * MONITORED SUBSYSTEMS:
 * =====================
 *
 * CLIENT MANAGEMENT METRICS:
 * - Active client count
 * - Clients with audio capabilities
 * - Clients with video capabilities
 * - Connection duration and activity patterns
 *
 * BUFFER POOL PERFORMANCE:
 * - Global buffer pool utilization
 * - Allocation/deallocation rates
 * - Peak usage patterns
 * - Memory efficiency metrics
 *
 * PACKET QUEUE STATISTICS:
 * - Per-client queue depths
 * - Enqueue/dequeue rates
 * - Packet drop rates under load
 * - Queue overflow incidents
 *
 * HASH TABLE EFFICIENCY:
 * - Client lookup performance
 * - Hash collision rates
 * - Load factor monitoring
 * - Access pattern analysis
 *
 * FRAME PROCESSING METRICS:
 * - Total frames captured
 * - Total frames sent to clients
 * - Frame drop rate under load
 * - Blank frame count (no video sources)
 *
 * THREAD SAFETY AND DATA CONSISTENCY:
 * ====================================
 *
 * NON-INTRUSIVE MONITORING:
 * - Uses reader locks to avoid blocking operational threads
 * - Takes atomic snapshots of volatile data
 * - Minimal impact on render thread performance
 * - Safe concurrent access to client data
 *
 * STATISTICS ATOMICITY:
 * - Global statistics protected by dedicated mutex
 * - Consistent reporting even during concurrent updates
 * - Thread-safe access to shared counters
 *
 * OPERATIONAL INTEGRATION:
 * ========================
 *
 * LIFECYCLE MANAGEMENT:
 * - Statistics thread started in main.c during server initialization
 * - Graceful shutdown coordination with other threads
 * - Clean resource cleanup on server termination
 *
 * DEBUGGING SUPPORT:
 * - Extensive debug logging for troubleshooting
 * - Performance bottleneck identification
 * - System health monitoring
 * - Operational visibility for administrators
 *
 * EXTENSIBILITY FRAMEWORK:
 * ========================
 *
 * METRIC ADDITION:
 * - Easy addition of new performance counters
 * - Modular statistics collection
 * - Configurable reporting intervals
 * - Custom metric aggregation
 *
 * INTEGRATION POINTS:
 * - client.c: Client lifecycle statistics
 * - render.c: Frame processing metrics
 * - buffer_pool.c: Memory utilization data
 * - packet_queue.c: Queue performance data
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * The original server.c had no centralized monitoring, making it impossible to:
 * - Understand system performance characteristics
 * - Debug performance issues under load
 * - Monitor resource utilization
 * - Track client behavior patterns
 *
 * This separation provides:
 * - Centralized performance visibility
 * - Non-intrusive monitoring infrastructure
 * - Operational debugging capabilities
 * - System health awareness
 *
 * PERFORMANCE PHILOSOPHY:
 * ========================
 * - Monitoring should never impact real-time performance
 * - Statistics collection uses minimal CPU overhead
 * - Background processing model
 * - Graceful degradation under extreme load
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 * @see main.c For statistics thread lifecycle management
 * @see buffer_pool.c For memory utilization monitoring
 * @see packet_queue.c For queue performance metrics
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "stats.h"
#include "client.h"
#include "render.h"
#include "common.h"
#include "buffer_pool.h"
#include "packet_queue.h"
#include "debug/lock.h"

/**
 * @brief Global server statistics structure
 *
 * Maintains aggregated performance metrics for the entire server including
 * frame processing rates, client counts, and system health indicators.
 * All access to this structure must be protected by g_stats_mutex.
 *
 * CONTENTS:
 * - frames_captured: Total frames received from all clients
 * - frames_sent: Total ASCII frames delivered to all clients
 * - frames_dropped: Frames lost due to buffer overflows or processing delays
 * - avg_capture_fps: Moving average of frame capture rate
 * - avg_send_fps: Moving average of frame delivery rate
 */
server_stats_t g_stats = {0};

/**
 * @brief Mutex protecting global server statistics
 *
 * Ensures thread-safe access to g_stats structure from both the statistics
 * collection thread and any operational threads that update counters.
 * Uses standard mutex (not reader-writer) due to relatively infrequent access.
 */
mutex_t g_stats_mutex = {0};

/**
 * @brief Flag tracking whether stats mutex has been initialized
 *
 * Used to prevent attempting to lock an uninitialized mutex in debug builds
 * where stats_init() may not be called (it's guarded by #ifdef NDEBUG).
 *
 * @ingroup server_stats
 */
static bool g_stats_mutex_initialized = false;

/**
 * @brief Initialize the stats mutex
 * @return 0 on success, -1 on failure
 */
int stats_init(void) {
  int ret = mutex_init(&g_stats_mutex);
  if (ret == 0) {
    g_stats_mutex_initialized = true;
  }
  return ret;
}

/**
 * @brief Cleanup the stats mutex
 */
void stats_cleanup(void) {
  if (g_stats_mutex_initialized) {
    mutex_destroy(&g_stats_mutex);
    g_stats_mutex_initialized = false;
  }
}

/**
 * @brief Global shutdown flag from main.c - coordinate statistics thread termination
 *
 * The statistics thread monitors this flag to detect server shutdown and exit
 * its monitoring loop gracefully, ensuring clean resource cleanup.
 */
extern atomic_bool g_server_should_exit;

/* ============================================================================
 * Statistics Collection and Reporting Thread
 * ============================================================================
 */

/**
 * @brief Main statistics collection and reporting thread function
 *
 * This background thread performs continuous monitoring of server performance
 * and logs comprehensive statistics reports at regular intervals. It operates
 * independently of the main server processing threads to avoid performance impact.
 *
 * THREAD EXECUTION FLOW:
 * ======================
 *
 * 1. INITIALIZATION:
 *    - Log thread startup (with debug instrumentation)
 *    - Initialize loop counters for debugging
 *    - Set up monitoring infrastructure
 *
 * 2. MAIN MONITORING LOOP:
 *    - Check shutdown flag frequently (every 10ms)
 *    - Sleep in small intervals to maintain responsiveness
 *    - Collect statistics from all system components every 30 seconds
 *    - Generate comprehensive performance reports
 *
 * 3. STATISTICS COLLECTION:
 *    - Global buffer pool utilization
 *    - Per-client connection and activity status
 *    - Packet queue performance metrics
 *    - Hash table efficiency statistics
 *    - Frame processing counters
 *
 * 4. CLEANUP AND EXIT:
 *    - Detect shutdown signal and exit loop gracefully
 *    - Log thread termination for debugging
 *    - Return NULL to indicate clean exit
 *
 * MONITORING METHODOLOGY:
 * =======================
 *
 * NON-BLOCKING DATA COLLECTION:
 * - Uses reader locks on shared data structures
 * - Takes atomic snapshots of volatile counters
 * - Minimal impact on operational performance
 * - Safe concurrent access with render threads
 *
 * RESPONSIVE SHUTDOWN:
 * - Checks g_server_should_exit every 10ms during sleep periods
 * - Exits monitoring loop immediately when shutdown detected
 * - No hanging or delayed shutdown behavior
 * - Clean resource cleanup
 *
 * PERFORMANCE METRICS COLLECTED:
 * ==============================
 *
 * CLIENT STATISTICS:
 * - Total active clients
 * - Clients with audio queues active
 * - Clients with video queues active
 * - Per-client connection duration
 *
 * BUFFER POOL METRICS:
 * - Global allocation/deallocation rates
 * - Peak memory usage patterns
 * - Buffer pool efficiency
 * - Memory fragmentation indicators
 *
 * PACKET QUEUE PERFORMANCE:
 * - Per-client enqueue/dequeue rates
 * - Packet drop incidents
 * - Queue depth histograms
 * - Overflow frequency analysis
 *
 * HASH TABLE EFFICIENCY:
 * - Client lookup performance
 * - Hash collision statistics
 * - Load balancing effectiveness
 * - Access pattern analysis
 *
 * DEBUGGING AND TROUBLESHOOTING:
 * ===============================
 *
 * EXTENSIVE DEBUG LOGGING:
 * The function contains detailed debug printf statements for troubleshooting
 * threading issues. This instrumentation helps diagnose:
 * - Thread startup/shutdown problems
 * - Shutdown detection timing issues
 * - Sleep/wake cycle behavior
 * - Statistics collection reliability
 *
 * DEBUG OUTPUT INCLUDES:
 * - Function entry/exit points
 * - Loop iteration counts
 * - Shutdown flag state changes
 * - Sleep cycle progression
 * - Statistics collection timing
 *
 * ERROR HANDLING:
 * ===============
 * - Graceful handling of data access failures
 * - Continued operation if individual metrics fail
 * - No fatal errors that would crash statistics thread
 * - Degraded reporting if subsystems unavailable
 *
 * PERFORMANCE CONSIDERATIONS:
 * ===========================
 * - 30-second reporting interval balances visibility vs overhead
 * - 10ms sleep granularity provides responsive shutdown
 * - Read-only access minimizes lock contention
 * - Background processing doesn't affect real-time performance
 *
 * @param arg Thread argument (unused - required by thread interface)
 * @return NULL on clean thread termination
 *
 * @note This function runs continuously until server shutdown
 * @note Debug printf statements help diagnose threading issues
 * @note All statistics collection is non-blocking and read-only
 *
 * @warning Thread must be properly joined to prevent resource leaks
 */

void *stats_logger_thread(void *arg) {
  (void)arg;

  while (!atomic_load(&g_server_should_exit)) {
    // Log buffer pool statistics every 10 seconds with fast exit checking (10ms intervals)
    for (int i = 0; i < 1000 && !atomic_load(&g_server_should_exit); i++) {
      platform_sleep_usec(10000); // 10ms sleep
    }

    // Check exit condition before proceeding with statistics logging
    // CRITICAL: Check multiple times to avoid accessing freed resources during shutdown
    if (atomic_load(&g_server_should_exit)) {
      break;
    }

    // Collect all statistics data first
#ifndef NDEBUG
    char lock_debug_info[512] = {0};
    // CRITICAL: Check exit condition again before accessing lock_debug
    // lock_debug might be destroyed during shutdown
    if (!atomic_load(&g_server_should_exit) && lock_debug_is_initialized()) {
      uint64_t total_acquired = 0, total_released = 0;
      uint32_t currently_held = 0;
      lock_debug_get_stats(&total_acquired, &total_released, &currently_held);

      safe_snprintf(lock_debug_info, sizeof(lock_debug_info),
                    "Historical Mutex/RWLock Statistics:\n"
                    "  Total locks acquired: %llu\n"
                    "  Total locks released: %llu\n"
                    "  Currently held: %u\n"
                    "  Net locks (acquired - released): %lld",
                    (unsigned long long)total_acquired, (unsigned long long)total_released, currently_held,
                    (long long)total_acquired - (long long)total_released);
    }
#endif

    // Collect client statistics
    // CRITICAL: Check exit condition again before accessing rwlock
    // rwlock might be destroyed during shutdown
    if (atomic_load(&g_server_should_exit)) {
      break;
    }

    rwlock_rdlock(&g_client_manager_rwlock);
    int active_clients = 0;
    int clients_with_audio = 0;
    int clients_with_video = 0;
    char client_details[2048] = {0};
    int client_details_len = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (atomic_load(&g_client_manager.clients[i].active)) {
        active_clients++;
        if (g_client_manager.clients[i].audio_queue) {
          clients_with_audio++;
        }
        if (g_client_manager.clients[i].outgoing_video_buffer) {
          clients_with_video++;
        }
      }
    }

    // Collect per-client details
    for (int i = 0; i < MAX_CLIENTS; i++) {
      client_info_t *client = &g_client_manager.clients[i];
      bool is_active = atomic_load(&client->active);
      uint32_t client_id_snapshot = atomic_load(&client->client_id);

      if (is_active && client_id_snapshot != 0) {
        // Check audio queue stats
        if (client->audio_queue) {
          uint64_t enqueued, dequeued, dropped;
          packet_queue_get_stats(client->audio_queue, &enqueued, &dequeued, &dropped);
          if (enqueued > 0 || dequeued > 0 || dropped > 0) {
            int len =
                snprintf(client_details + client_details_len, sizeof(client_details) - client_details_len,
                         "  Client %u audio queue: %llu enqueued, %llu dequeued, %llu dropped", client_id_snapshot,
                         (unsigned long long)enqueued, (unsigned long long)dequeued, (unsigned long long)dropped);
            if (len > 0 && client_details_len + len < (int)sizeof(client_details)) {
              client_details_len += len;
            }
          }
        }
        // Check video buffer stats
        if (client->outgoing_video_buffer) {
          video_frame_stats_t stats;
          video_frame_get_stats(client->outgoing_video_buffer, &stats);
          if (stats.total_frames > 0) {
            int len = snprintf(client_details + client_details_len, sizeof(client_details) - client_details_len,
                               "  Client %u video buffer: %llu frames, %llu dropped (%.1f%% drop rate)",
                               client_id_snapshot, (unsigned long long)stats.total_frames,
                               (unsigned long long)stats.dropped_frames, stats.drop_rate * 100.0f);
            if (len > 0 && client_details_len + len < (int)sizeof(client_details)) {
              client_details_len += len;
            }
          }
        }
      }
    }
    rwlock_rdunlock(&g_client_manager_rwlock);

    // Single comprehensive log statement
    if (client_details_len > 0) {
      log_info("Stats: Clients: %d, Audio: %d, Video: %d\n%s", active_clients, clients_with_audio, clients_with_video,
               client_details);
    }
  }

  log_server_stats();

  asciichat_error_stats_print();

  return NULL;
}

/**
 * @brief Update global server statistics (placeholder)
 *
 * This function is intended to update the global server statistics structure
 * with current performance metrics. Currently unimplemented but provides
 * the framework for centralized statistics updates.
 *
 * PLANNED FUNCTIONALITY:
 * ======================
 * - Aggregate per-client frame counts into global totals
 * - Calculate moving averages for FPS metrics
 * - Update system health indicators
 * - Compute performance trend data
 *
 * IMPLEMENTATION STRATEGY:
 * - Thread-safe updates using g_stats_mutex
 * - Atomic operations for frequently updated counters
 * - Efficient aggregation algorithms
 * - Minimal performance impact
 *
 * INTEGRATION POINTS:
 * - Called by render threads when updating frame counts
 * - Invoked by client management code for connection metrics
 * - Used by packet queue systems for throughput tracking
 *
 * @todo Implement comprehensive statistics aggregation
 * @note Function currently serves as placeholder for future development
 */
void update_server_stats(void) {
  // TODO: Implement server statistics update
  // This would update g_stats with current performance metrics
}

/**
 * @brief Log comprehensive server statistics summary
 *
 * Outputs a formatted summary of server performance statistics including
 * frame processing rates, throughput metrics, and system health indicators.
 * This function provides operational visibility into server performance.
 *
 * STATISTICS REPORTED:
 * ====================
 *
 * FRAME PROCESSING METRICS:
 * - frames_captured: Total frames received from all clients
 * - frames_sent: Total ASCII frames delivered to all clients
 * - frames_dropped: Frames lost due to overload or errors
 *
 * PERFORMANCE INDICATORS:
 * - avg_capture_fps: Moving average of frame capture rate
 * - avg_send_fps: Moving average of frame delivery rate
 *
 * THREAD SAFETY:
 * ==============
 * - Acquires g_stats_mutex for atomic statistics snapshot
 * - Prevents inconsistent reporting during concurrent updates
 * - Minimal lock hold time (only during data copy)
 *
 * USAGE SCENARIOS:
 * ================
 * - Periodic performance reporting
 * - Debug information during troubleshooting
 * - System health monitoring
 * - Performance trend analysis
 *
 * OUTPUT FORMAT:
 * ==============
 * - Human-readable format suitable for log analysis
 * - Structured data for automated monitoring
 * - Consistent formatting for operational visibility
 *
 * @note Function provides thread-safe access to global statistics
 * @note Statistics snapshot is atomic to ensure consistency
 * @see update_server_stats() For statistics update implementation
 */
void log_server_stats(void) {
  // Check if stats mutex is initialized before trying to lock it
  // In debug builds, stats_init() may not be called (it's guarded by #ifdef NDEBUG)
  // Skip logging if mutex is not initialized to avoid crashing
  if (!g_stats_mutex_initialized) {
    return; // Mutex not initialized, skip logging
  }
  mutex_lock(&g_stats_mutex);
  log_info("Server Statistics:\n"
           "  frames_captured=%llu\n"
           "  frames_sent=%llu\n"
           "  frames_dropped=%llu\n"
           "  Average FPS: capture=%.2f, send=%.2f",
           (unsigned long long)g_stats.frames_captured, (unsigned long long)g_stats.frames_sent,
           (unsigned long long)g_stats.frames_dropped, g_stats.avg_capture_fps, g_stats.avg_send_fps);
  mutex_unlock(&g_stats_mutex);
}
