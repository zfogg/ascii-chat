/**
 * @file network/websocket/callback_profiler.c
 * @brief Implementation of LWS callback profiling
 *
 * Provides timing instrumentation for libwebsockets callbacks with
 * minimal overhead using atomic operations and per-callback statistics.
 */

#include <ascii-chat/network/websocket/callback_profiler.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/platform/mutex.h>
#include <libwebsockets.h>
#include <ascii-chat/uthash/uthash.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Per-callback profiling record
 */
typedef struct {
  int callback_reason;          ///< LWS callback reason (key)
  _Atomic(uint64_t) count;      ///< Atomic invocation counter
  _Atomic(uint64_t) total_time; ///< Atomic total time accumulator
  _Atomic(uint64_t) min_time;   ///< Atomic minimum time
  _Atomic(uint64_t) max_time;   ///< Atomic maximum time
  _Atomic(uint64_t) bytes;      ///< Atomic bytes processed counter
  _Atomic(uint64_t) last_ts;    ///< Atomic last invocation timestamp
  UT_hash_handle hh;            ///< Hash table handle
} callback_profile_t;

/**
 * @brief Per-timing session context
 */
typedef struct {
  uint64_t start_time_ns;    ///< Session start time
  int callback_reason;       ///< Which callback is being timed
  callback_profile_t *prof;  ///< Pointer to stats record
} timing_context_t;

// Global profiler state
static struct {
  mutex_t lock;              ///< Protects hashtable operations
  callback_profile_t *table; ///< Hash table of callback stats (callback_reason -> stats)
  bool initialized;          ///< Profiler active flag
} profiler_state = {0};

// ============================================================================
// LWS Callback Reason Names (for logging)
// ============================================================================

/**
 * @brief Get human-readable name for LWS callback reason
 */
static const char *lws_reason_name(int reason) {
  switch (reason) {
  case LWS_CALLBACK_PROTOCOL_INIT:
    return "PROTOCOL_INIT";
  case LWS_CALLBACK_PROTOCOL_DESTROY:
    return "PROTOCOL_DESTROY";
  case LWS_CALLBACK_ESTABLISHED:
    return "ESTABLISHED";
  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    return "CLIENT_ESTABLISHED";
  case LWS_CALLBACK_CLOSED:
    return "CLOSED";
  case LWS_CALLBACK_CLIENT_CLOSED:
    return "CLIENT_CLOSED";
  case LWS_CALLBACK_RECEIVE:
    return "RECEIVE";
  case LWS_CALLBACK_CLIENT_RECEIVE:
    return "CLIENT_RECEIVE";
  case LWS_CALLBACK_SERVER_WRITEABLE:
    return "SERVER_WRITEABLE";
  case LWS_CALLBACK_CLIENT_WRITEABLE:
    return "CLIENT_WRITEABLE";
  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    return "CLIENT_CONNECTION_ERROR";
  case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
    return "FILTER_NETWORK_CONNECTION";
  case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
    return "FILTER_HTTP_CONNECTION";
  case LWS_CALLBACK_ADD_HEADERS:
    return "ADD_HEADERS";
  default:
    return "UNKNOWN";
  }
}

// ============================================================================
// Public API
// ============================================================================

bool lws_profiler_init(void) {
  if (profiler_state.initialized) {
    return true; // Already initialized
  }

  if (!mutex_init(&profiler_state.lock)) {
    log_error("Failed to initialize profiler mutex");
    return false;
  }

  profiler_state.table = NULL;
  profiler_state.initialized = true;

  log_info("LWS callback profiler initialized");
  return true;
}

void lws_profiler_destroy(void) {
  if (!profiler_state.initialized) {
    return;
  }

  mutex_lock(&profiler_state.lock);

  // Free all hash table entries
  callback_profile_t *curr, *tmp;
  HASH_ITER(hh, profiler_state.table, curr, tmp) {
    HASH_DEL(profiler_state.table, curr);
    SAFE_FREE(curr);
  }

  mutex_unlock(&profiler_state.lock);
  mutex_destroy(&profiler_state.lock);

  profiler_state.initialized = false;
  log_info("LWS callback profiler destroyed");
}

uint64_t lws_profiler_start(int callback_reason, uint32_t client_id) {
  (void)client_id; // For future per-client tracking

  if (!profiler_state.initialized) {
    return 0; // Profiler not active
  }

  timing_context_t *ctx = SAFE_MALLOC(1, timing_context_t *);
  if (!ctx) {
    return 0;
  }

  ctx->start_time_ns = time_get_ns();
  ctx->callback_reason = callback_reason;

  // Get or create profile entry
  mutex_lock(&profiler_state.lock);

  callback_profile_t *prof;
  HASH_FIND_INT(profiler_state.table, &callback_reason, prof);

  if (!prof) {
    prof = SAFE_MALLOC(1, callback_profile_t *);
    if (!prof) {
      mutex_unlock(&profiler_state.lock);
      SAFE_FREE(ctx);
      return 0;
    }

    prof->callback_reason = callback_reason;
    atomic_store_explicit(&prof->count, 0, memory_order_relaxed);
    atomic_store_explicit(&prof->total_time, 0, memory_order_relaxed);
    atomic_store_explicit(&prof->min_time, UINT64_MAX, memory_order_relaxed);
    atomic_store_explicit(&prof->max_time, 0, memory_order_relaxed);
    atomic_store_explicit(&prof->bytes, 0, memory_order_relaxed);
    atomic_store_explicit(&prof->last_ts, 0, memory_order_relaxed);

    HASH_ADD_INT(profiler_state.table, callback_reason, prof);
  }

  ctx->prof = prof;
  mutex_unlock(&profiler_state.lock);

  return (uint64_t)(uintptr_t)ctx;
}

void lws_profiler_stop(uint64_t handle, size_t bytes_processed) {
  if (!handle || !profiler_state.initialized) {
    return;
  }

  timing_context_t *ctx = (timing_context_t *)(uintptr_t)handle;
  uint64_t end_time = time_get_ns();
  uint64_t elapsed = time_elapsed_ns(ctx->start_time_ns, end_time);

  if (!ctx->prof) {
    SAFE_FREE(ctx);
    return;
  }

  callback_profile_t *prof = ctx->prof;

  // Update atomically (no lock needed)
  atomic_fetch_add_explicit(&prof->count, 1, memory_order_relaxed);
  atomic_fetch_add_explicit(&prof->total_time, elapsed, memory_order_relaxed);
  atomic_fetch_add_explicit(&prof->bytes, bytes_processed, memory_order_relaxed);
  atomic_store_explicit(&prof->last_ts, end_time, memory_order_relaxed);

  // Update min/max (read-compare-swap loop)
  uint64_t current_min = atomic_load_explicit(&prof->min_time, memory_order_relaxed);
  while (elapsed < current_min &&
         !atomic_compare_exchange_weak_explicit(&prof->min_time, &current_min, elapsed,
                                                memory_order_relaxed, memory_order_relaxed)) {
    // Retry with updated current_min
  }

  uint64_t current_max = atomic_load_explicit(&prof->max_time, memory_order_relaxed);
  while (elapsed > current_max &&
         !atomic_compare_exchange_weak_explicit(&prof->max_time, &current_max, elapsed,
                                                memory_order_relaxed, memory_order_relaxed)) {
    // Retry with updated current_max
  }

  SAFE_FREE(ctx);
}

bool lws_profiler_get_stats(int callback_reason, lws_callback_stats_t *stats) {
  if (!profiler_state.initialized || !stats) {
    return false;
  }

  mutex_lock(&profiler_state.lock);

  callback_profile_t *prof;
  HASH_FIND_INT(profiler_state.table, &callback_reason, prof);

  if (!prof) {
    mutex_unlock(&profiler_state.lock);
    return false;
  }

  // Snapshot atomically
  stats->invocation_count = atomic_load_explicit(&prof->count, memory_order_acquire);
  stats->total_time_ns = atomic_load_explicit(&prof->total_time, memory_order_acquire);
  stats->min_time_ns = atomic_load_explicit(&prof->min_time, memory_order_acquire);
  stats->max_time_ns = atomic_load_explicit(&prof->max_time, memory_order_acquire);
  stats->bytes_processed = atomic_load_explicit(&prof->bytes, memory_order_acquire);
  stats->last_invocation_ns = atomic_load_explicit(&prof->last_ts, memory_order_acquire);

  // Handle uninitialized min_time
  if (stats->min_time_ns == UINT64_MAX) {
    stats->min_time_ns = 0;
  }

  mutex_unlock(&profiler_state.lock);
  return true;
}

void lws_profiler_reset(void) {
  if (!profiler_state.initialized) {
    return;
  }

  mutex_lock(&profiler_state.lock);

  callback_profile_t *curr, *tmp;
  HASH_ITER(hh, profiler_state.table, curr, tmp) {
    atomic_store_explicit(&curr->count, 0, memory_order_relaxed);
    atomic_store_explicit(&curr->total_time, 0, memory_order_relaxed);
    atomic_store_explicit(&curr->min_time, UINT64_MAX, memory_order_relaxed);
    atomic_store_explicit(&curr->max_time, 0, memory_order_relaxed);
    atomic_store_explicit(&curr->bytes, 0, memory_order_relaxed);
    atomic_store_explicit(&curr->last_ts, 0, memory_order_relaxed);
  }

  mutex_unlock(&profiler_state.lock);
  log_info("LWS profiler statistics reset");
}

void lws_profiler_dump_report(void) {
  if (!profiler_state.initialized) {
    log_warn("LWS profiler not initialized");
    return;
  }

  mutex_lock(&profiler_state.lock);

  if (profiler_state.table == NULL) {
    mutex_unlock(&profiler_state.lock);
    log_info("LWS profiler: No callback invocations recorded yet");
    return;
  }

  log_info("╔════════════════════════════════════════════════════════════════════════════╗");
  log_info("║                 LIBWEBSOCKETS CALLBACK PROFILING REPORT                   ║");
  log_info("╠════════════════════════════════════════════════════════════════════════════╣");

  callback_profile_t *curr, *tmp;
  HASH_ITER(hh, profiler_state.table, curr, tmp) {
    lws_callback_stats_t stats;
    stats.invocation_count = atomic_load_explicit(&curr->count, memory_order_relaxed);
    stats.total_time_ns = atomic_load_explicit(&curr->total_time, memory_order_relaxed);
    stats.min_time_ns = atomic_load_explicit(&curr->min_time, memory_order_relaxed);
    stats.max_time_ns = atomic_load_explicit(&curr->max_time, memory_order_relaxed);
    stats.bytes_processed = atomic_load_explicit(&curr->bytes, memory_order_relaxed);

    if (stats.min_time_ns == UINT64_MAX) {
      stats.min_time_ns = 0;
    }

    if (stats.invocation_count == 0) {
      continue; // Skip never-invoked callbacks
    }

    // Calculate average
    uint64_t avg_time_ns = stats.total_time_ns / stats.invocation_count;
    double throughput_kbps = 0;
    if (stats.bytes_processed > 0 && stats.total_time_ns > 0) {
      throughput_kbps = (double)stats.bytes_processed * 1000.0 / (double)stats.total_time_ns * 8.0; // bits/ms
    }

    char min_str[32], max_str[32], avg_str[32];
    format_duration_ns(stats.min_time_ns, min_str, sizeof(min_str));
    format_duration_ns(stats.max_time_ns, max_str, sizeof(max_str));
    format_duration_ns(avg_time_ns, avg_str, sizeof(avg_str));

    log_info("║");
    log_info("║  Callback: LWS_%s", lws_reason_name(curr->callback_reason));
    log_info("║    Invocations: %llu", (unsigned long long)stats.invocation_count);
    log_info("║    Timing: min=%s, max=%s, avg=%s", min_str, max_str, avg_str);
    log_info("║    Total data: %llu bytes", (unsigned long long)stats.bytes_processed);
    if (throughput_kbps > 0) {
      log_info("║    Throughput: %.2f kbps", throughput_kbps);
    }
  }

  log_info("╠════════════════════════════════════════════════════════════════════════════╣");
  log_info("║  Use 'lws_profiler_export_json()' for detailed metrics export              ║");
  log_info("╚════════════════════════════════════════════════════════════════════════════╝");

  mutex_unlock(&profiler_state.lock);
}

char *lws_profiler_export_json(void) {
  if (!profiler_state.initialized) {
    return NULL;
  }

  // Build JSON string
  // Start with 1KB buffer, grow as needed
  size_t capacity = 4096;
  char *json = SAFE_MALLOC(capacity, char *);
  if (!json) {
    return NULL;
  }

  size_t offset = 0;
  offset += snprintf(json + offset, capacity - offset, "{\"callbacks\":[");

  mutex_lock(&profiler_state.lock);

  callback_profile_t *curr, *tmp;
  bool first = true;

  HASH_ITER(hh, profiler_state.table, curr, tmp) {
    lws_callback_stats_t stats;
    stats.invocation_count = atomic_load_explicit(&curr->count, memory_order_relaxed);
    stats.total_time_ns = atomic_load_explicit(&curr->total_time, memory_order_relaxed);
    stats.min_time_ns = atomic_load_explicit(&curr->min_time, memory_order_relaxed);
    stats.max_time_ns = atomic_load_explicit(&curr->max_time, memory_order_relaxed);
    stats.bytes_processed = atomic_load_explicit(&curr->bytes, memory_order_relaxed);

    if (stats.min_time_ns == UINT64_MAX) {
      stats.min_time_ns = 0;
    }

    if (stats.invocation_count == 0) {
      continue;
    }

    uint64_t avg_time_ns = stats.total_time_ns / stats.invocation_count;

    if (!first) {
      offset += snprintf(json + offset, capacity - offset, ",");
    }
    first = false;

    offset += snprintf(json + offset, capacity - offset,
                       "{\"reason\":\"%s\",\"invocations\":%llu,\"total_ns\":%llu,"
                       "\"min_ns\":%llu,\"max_ns\":%llu,\"avg_ns\":%llu,\"bytes\":%llu}",
                       lws_reason_name(curr->callback_reason), (unsigned long long)stats.invocation_count,
                       (unsigned long long)stats.total_time_ns, (unsigned long long)stats.min_time_ns,
                       (unsigned long long)stats.max_time_ns, (unsigned long long)avg_time_ns,
                       (unsigned long long)stats.bytes_processed);

    // Check buffer overflow
    if (offset >= capacity - 256) {
      capacity *= 2;
      char *new_json = SAFE_MALLOC(capacity, char *);
      if (!new_json) {
        mutex_unlock(&profiler_state.lock);
        SAFE_FREE(json);
        return NULL;
      }
      memcpy(new_json, json, offset);
      SAFE_FREE(json);
      json = new_json;
    }
  }

  mutex_unlock(&profiler_state.lock);

  offset += snprintf(json + offset, capacity - offset, "]}");

  return json;
}

bool lws_profiler_is_initialized(void) {
  return profiler_state.initialized;
}
