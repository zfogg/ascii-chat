/**
 * @file network/websocket/callback_timing.c
 * @brief WebSocket callback timing instrumentation implementation
 * @ingroup network
 *
 * Implements callback timing tracking for diagnosing WebSocket performance issues.
 */

#include <ascii-chat/network/websocket/callback_timing.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ascii-chat/atomic.h>

/**
 * @brief Global timing tracker - shared across all WebSocket connections
 */
websocket_callback_timing_t g_ws_callback_timing = {0};

void websocket_callback_timing_record(websocket_callback_stats_t *stats, uint64_t start_ns, uint64_t end_ns) {
  if (!stats) {
    return;
  }

  uint64_t duration_ns = (end_ns > start_ns) ? (end_ns - start_ns) : 0;
  uint64_t count = atomic_fetch_add_u64(&stats->count, 1);

  // Track cumulative duration
  atomic_fetch_add_u64(&stats->total_duration_ns, duration_ns);

  // Update last timestamp (atomically swap)
  uint64_t prev_last_ns = atomic_load_u64(&stats->last_ns);
  while (!atomic_cas_u64(&stats->last_ns, &prev_last_ns, end_ns)) {
    // Retry if CAS failed due to concurrent update
  }

  // Calculate interval from previous callback (if not the first one)
  if (count > 0 && prev_last_ns > 0) {
    uint64_t interval_ns = (end_ns > prev_last_ns) ? (end_ns - prev_last_ns) : 0;

    // Track min intervals (atomic compare-and-swap loop to avoid TOCTOU race)
    if (interval_ns > 0) {
      uint64_t current_min = atomic_load_u64(&stats->min_interval_ns);
      while (interval_ns < current_min) {
        if (atomic_cas_u64(&stats->min_interval_ns, &current_min, interval_ns)) {
          break;
        }
        // CAS failed - reload current value and retry if still applicable
        current_min = atomic_load_u64(&stats->min_interval_ns);
      }
    }

    // Track max intervals (atomic compare-and-swap loop to avoid TOCTOU race)
    uint64_t current_max = atomic_load_u64(&stats->max_interval_ns);
    while (interval_ns > current_max) {
      if (atomic_cas_u64(&stats->max_interval_ns, &current_max, interval_ns)) {
        break;
      }
      // CAS failed - reload current value and retry if still applicable
      current_max = atomic_load_u64(&stats->max_interval_ns);
    }
  }
}

void websocket_callback_timing_log_stats(void) {
  uint64_t protocol_init_count = atomic_load_u64(&g_ws_callback_timing.protocol_init.count);
  uint64_t protocol_destroy_count = atomic_load_u64(&g_ws_callback_timing.protocol_destroy.count);
  uint64_t writeable_count = atomic_load_u64(&g_ws_callback_timing.server_writeable.count);
  uint64_t receive_count = atomic_load_u64(&g_ws_callback_timing.receive.count);

  log_info("\n===== WEBSOCKET CALLBACK TIMING STATISTICS =====");
  log_info("Timestamp: %lu ns", websocket_callback_timing_start());

  // PROTOCOL_INIT stats
  log_info("LWS_CALLBACK_PROTOCOL_INIT:");
  log_info("  Total invocations: %lu", protocol_init_count);
  if (protocol_init_count > 0) {
    uint64_t total_duration = atomic_load_u64(&g_ws_callback_timing.protocol_init.total_duration_ns);
    uint64_t avg_duration = total_duration / protocol_init_count;
    log_info("  Avg duration: %lu ns", avg_duration);
  }

  // PROTOCOL_DESTROY stats
  log_info("LWS_CALLBACK_PROTOCOL_DESTROY:");
  log_info("  Total invocations: %lu", protocol_destroy_count);
  if (protocol_destroy_count > 0) {
    uint64_t total_duration = atomic_load_u64(&g_ws_callback_timing.protocol_destroy.total_duration_ns);
    uint64_t avg_duration = total_duration / protocol_destroy_count;
    log_info("  Avg duration: %lu ns", avg_duration);
  }

  // SERVER_WRITEABLE stats
  log_info("LWS_CALLBACK_SERVER_WRITEABLE:");
  log_info("  Total invocations: %lu", writeable_count);
  if (writeable_count > 0) {
    uint64_t total_duration = atomic_load_u64(&g_ws_callback_timing.server_writeable.total_duration_ns);
    uint64_t avg_duration = total_duration / writeable_count;
    uint64_t min_interval = atomic_load_u64(&g_ws_callback_timing.server_writeable.min_interval_ns);
    uint64_t max_interval = atomic_load_u64(&g_ws_callback_timing.server_writeable.max_interval_ns);
    log_info("  Avg duration: %lu ns", avg_duration);
    if (min_interval != UINT64_MAX) {
      log_info("  Min interval between callbacks: %lu ns (%.2f Hz)", min_interval, 1e9 / (double)min_interval);
    }
    if (max_interval > 0) {
      log_info("  Max interval between callbacks: %lu ns (%.2f Hz)", max_interval, 1e9 / (double)max_interval);
    }
  }

  // RECEIVE stats
  log_info("LWS_CALLBACK_RECEIVE:");
  log_info("  Total invocations: %lu", receive_count);
  if (receive_count > 0) {
    uint64_t total_duration = atomic_load_u64(&g_ws_callback_timing.receive.total_duration_ns);
    uint64_t avg_duration = total_duration / receive_count;
    uint64_t min_interval = atomic_load_u64(&g_ws_callback_timing.receive.min_interval_ns);
    uint64_t max_interval = atomic_load_u64(&g_ws_callback_timing.receive.max_interval_ns);
    log_info("  Avg duration: %lu ns", avg_duration);
    if (min_interval != UINT64_MAX) {
      log_info("  Min interval between callbacks: %lu ns (%.2f Hz)", min_interval, 1e9 / (double)min_interval);
    }
    if (max_interval > 0) {
      log_info("  Max interval between callbacks: %lu ns (%.2f Hz)", max_interval, 1e9 / (double)max_interval);
    }
  }

  log_info("===== END TIMING STATISTICS =====\n");
}

void websocket_callback_timing_reset(void) {
  memset(&g_ws_callback_timing, 0, sizeof(g_ws_callback_timing));
  atomic_store_u64(&g_ws_callback_timing.protocol_init.min_interval_ns, UINT64_MAX);
  atomic_store_u64(&g_ws_callback_timing.protocol_destroy.min_interval_ns, UINT64_MAX);
  atomic_store_u64(&g_ws_callback_timing.server_writeable.min_interval_ns, UINT64_MAX);
  atomic_store_u64(&g_ws_callback_timing.receive.min_interval_ns, UINT64_MAX);
}
