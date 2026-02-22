/**
 * @file network/rate_limit/memory.c
 * @brief 🧠 In-memory rate limiting backend using uthash
 *
 * Thread-safe implementation for ascii-chat server where persistence is not needed.
 */

#include <ascii-chat/network/rate_limit/memory.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/uthash/uthash.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Rate event record in memory
 */
typedef struct rate_event_s {
  char key[256];        ///< Hash key: "ip_address:event_type"
  uint64_t *timestamps; ///< Array of timestamps (circular buffer)
  size_t count;         ///< Number of events in buffer
  size_t capacity;      ///< Buffer capacity
  size_t head;          ///< Head index (oldest event)
  UT_hash_handle hh;    ///< uthash handle
} rate_event_t;

/**
 * @brief Memory backend data
 */
typedef struct {
  rate_event_t *events; ///< Hash table of rate events
  mutex_t lock;         ///< Mutex for thread safety
} memory_backend_t;

/**
 * @brief Create hash key from IP and event type
 */
static void make_key(const char *ip_address, rate_event_type_t event_type, char *key, size_t key_size) {
  safe_snprintf(key, key_size, "%s:%d", ip_address, event_type);
}

/**
 * @brief Add timestamp to event record
 */
static void add_timestamp(rate_event_t *event, uint64_t timestamp) {
  if (event->count < event->capacity) {
    // Buffer not full yet
    event->timestamps[event->count++] = timestamp;
  } else {
    // Buffer full, overwrite oldest
    event->timestamps[event->head] = timestamp;
    event->head = (event->head + 1) % event->capacity;
  }
}

/**
 * @brief Count events within time window
 */
static uint32_t count_events_in_window(rate_event_t *event, uint64_t window_start_ms) {
  uint32_t count = 0;

  for (size_t i = 0; i < event->count; i++) {
    if (event->timestamps[i] >= window_start_ms) {
      count++;
    }
  }

  return count;
}

/**
 * @brief Remove events older than cutoff time
 */
static void cleanup_old_events(rate_event_t *event, uint64_t cutoff_ns) {
  // Compact array by removing old timestamps
  size_t write_idx = 0;

  for (size_t read_idx = 0; read_idx < event->count; read_idx++) {
    if (event->timestamps[read_idx] >= cutoff_ns) {
      event->timestamps[write_idx++] = event->timestamps[read_idx];
    }
  }

  event->count = write_idx;
  event->head = 0; // Reset circular buffer head
}

static asciichat_error_t memory_check(void *backend_data, const char *ip_address, rate_event_type_t event_type,
                                      const rate_limit_config_t *config, bool *allowed) {
  memory_backend_t *backend = (memory_backend_t *)backend_data;

  // Use provided config or default
  const rate_limit_config_t *limit = config ? config : &DEFAULT_RATE_LIMITS[event_type];

  // Get current time in nanoseconds
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t window_start_ns = now_ns - ((uint64_t)limit->window_secs * NS_PER_SEC_INT);

  // Create hash key
  char key[256];
  make_key(ip_address, event_type, key, sizeof(key));

  mutex_lock(&backend->lock);

  // Find event record
  rate_event_t *event = NULL;
  HASH_FIND_STR(backend->events, key, event);

  uint32_t event_count = 0;

  if (event) {
    // Count events in window
    event_count = count_events_in_window(event, window_start_ns);
  }

  mutex_unlock(&backend->lock);

  // Check if limit exceeded
  *allowed = (event_count < limit->max_events);

  if (!*allowed) {
    log_warn("Rate limit exceeded for %s (event: %s, count: %u/%u)", ip_address,
             rate_limiter_event_type_string(event_type), event_count, limit->max_events);
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t memory_record(void *backend_data, const char *ip_address, rate_event_type_t event_type) {
  memory_backend_t *backend = (memory_backend_t *)backend_data;

  // Get current time in nanoseconds
  uint64_t now_ns = time_get_realtime_ns();

  // Create hash key
  char key[256];
  make_key(ip_address, event_type, key, sizeof(key));

  mutex_lock(&backend->lock);

  // Find or create event record
  rate_event_t *event = NULL;
  HASH_FIND_STR(backend->events, key, event);

  if (!event) {
    // Create new event record
    event = SAFE_MALLOC(sizeof(rate_event_t), rate_event_t *);
    if (!event) {
      mutex_unlock(&backend->lock);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate rate event");
    }

    memset(event, 0, sizeof(*event));
    SAFE_STRNCPY(event->key, key, sizeof(event->key));

    // Allocate timestamp buffer (default: 100 events)
    event->capacity = 100;
    event->timestamps = SAFE_MALLOC(sizeof(uint64_t) * event->capacity, uint64_t *);
    if (!event->timestamps) {
      SAFE_FREE(event);
      mutex_unlock(&backend->lock);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate timestamp buffer");
    }

    HASH_ADD_STR(backend->events, key, event);
  }

  // Add timestamp (in nanoseconds)
  add_timestamp(event, now_ns);

  mutex_unlock(&backend->lock);

  log_debug("Rate event recorded: %s - %s", ip_address, rate_limiter_event_type_string(event_type));
  return ASCIICHAT_OK;
}

static asciichat_error_t memory_cleanup(void *backend_data, uint32_t max_age_secs) {
  memory_backend_t *backend = (memory_backend_t *)backend_data;

  // Default to 1 hour cleanup window
  if (max_age_secs == 0) {
    max_age_secs = SEC_PER_HOUR;
  }

  // Calculate cutoff time in nanoseconds
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t cutoff_ns = now_ns - ((uint64_t)max_age_secs * NS_PER_SEC_INT);

  mutex_lock(&backend->lock);

  size_t total_removed = 0;
  rate_event_t *event = NULL, *tmp = NULL;

  HASH_ITER(hh, backend->events, event, tmp) {
    size_t before_count = event->count;

    // Remove old timestamps
    cleanup_old_events(event, cutoff_ns);

    total_removed += (before_count - event->count);

    // Remove event record if empty
    if (event->count == 0) {
      HASH_DEL(backend->events, event);
      SAFE_FREE(event->timestamps);
      SAFE_FREE(event);
    }
  }

  mutex_unlock(&backend->lock);

  if (total_removed > 0) {
    log_debug("Cleaned up %zu old rate events", total_removed);
  }

  return ASCIICHAT_OK;
}

static void memory_destroy(void *backend_data) {
  memory_backend_t *backend = (memory_backend_t *)backend_data;
  if (!backend) {
    return;
  }

  // Free all event records
  rate_event_t *event, *tmp;
  HASH_ITER(hh, backend->events, event, tmp) {
    HASH_DEL(backend->events, event);
    SAFE_FREE(event->timestamps);
    SAFE_FREE(event);
  }

  mutex_destroy(&backend->lock);
  SAFE_FREE(backend);
}

void *memory_backend_create(void) {
  memory_backend_t *backend = SAFE_MALLOC(sizeof(memory_backend_t), memory_backend_t *);
  if (!backend) {
    log_error("Failed to allocate memory backend");
    return NULL;
  }

  memset(backend, 0, sizeof(*backend));

  if (mutex_init(&backend->lock, "rate_limiter")  != 0) {
    log_error("Failed to initialize mutex");
    SAFE_FREE(backend);
    return NULL;
  }

  log_debug("Memory rate limiter backend initialized");
  return backend;
}

const rate_limiter_backend_ops_t memory_backend_ops = {
    .check = memory_check,
    .record = memory_record,
    .cleanup = memory_cleanup,
    .destroy = memory_destroy,
};
