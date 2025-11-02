#include "rate_limiter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

static double timeval_diff_seconds(struct timeval *newer, struct timeval *older) {
  return (newer->tv_sec - older->tv_sec) + (newer->tv_usec - older->tv_usec) / 1000000.0;
}

static void update_stats(rate_limit_stats_t *stats, bool allowed, size_t size) {
  if (allowed) {
    stats->allowed_count++;
    stats->total_bytes += size;
  } else {
    stats->blocked_count++;
  }

  // Calculate average rate
  time_t now = time(NULL);
  if (stats->start_time == 0) {
    stats->start_time = now;
  }

  double elapsed = now - stats->start_time;
  if (elapsed > 0) {
    stats->avg_rate = stats->allowed_count / elapsed;
  }
}

/* ============================================================================
 * Token Bucket Implementation
 * ============================================================================
 */

static void token_bucket_init(token_bucket_t *tb, double rate, double burst) {
  tb->tokens = burst;
  tb->max_tokens = burst;
  tb->refill_rate = rate;
  tb->cost_per_byte = 0; // Default to per-request
  gettimeofday(&tb->last_refill, NULL);
}

static bool token_bucket_check(token_bucket_t *tb, size_t request_size) {
  struct timeval now;
  gettimeofday(&now, NULL);

  // Refill tokens based on elapsed time
  double elapsed = timeval_diff_seconds(&now, &tb->last_refill);
  tb->tokens += elapsed * tb->refill_rate;

  // Cap at max tokens
  if (tb->tokens > tb->max_tokens) {
    tb->tokens = tb->max_tokens;
  }

  tb->last_refill = now;

  // Calculate cost
  double cost = 1.0; // Default: 1 token per request
  if (tb->cost_per_byte > 0) {
    cost = request_size * tb->cost_per_byte;
  }

  // Check if we have enough tokens
  if (tb->tokens >= cost) {
    tb->tokens -= cost;
    return true;
  }

  return false;
}

/* ============================================================================
 * Sliding Window Implementation
 * ============================================================================
 */

static void sliding_window_init(sliding_window_t *sw, int window_secs, int max_reqs) {
  memset(sw->entries, 0, sizeof(sw->entries));
  sw->head = 0;
  sw->tail = 0;
  sw->count = 0;
  sw->window_seconds = window_secs;
  sw->max_requests = max_reqs;
  sw->max_bytes = 0; // No byte limit by default
}

static void sliding_window_cleanup_old(sliding_window_t *sw) {
  struct timeval now;
  gettimeofday(&now, NULL);

  // Remove entries older than window
  while (sw->count > 0) {
    window_entry_t *oldest = &sw->entries[sw->tail];
    double age = timeval_diff_seconds(&now, &oldest->timestamp);

    if (age > sw->window_seconds) {
      sw->tail = (sw->tail + 1) % MAX_WINDOW_ENTRIES;
      sw->count--;
    } else {
      break; // All remaining entries are within window
    }
  }
}

static bool sliding_window_check(sliding_window_t *sw, size_t request_size) {
  // Clean up old entries first
  sliding_window_cleanup_old(sw);

  // Check if we're at limit
  if (sw->count >= sw->max_requests) {
    return false;
  }

  // Check byte limit if set
  if (sw->max_bytes > 0) {
    size_t total_bytes = 0;
    for (int i = 0; i < sw->count; i++) {
      int idx = (sw->tail + i) % MAX_WINDOW_ENTRIES;
      total_bytes += sw->entries[idx].size;
    }

    if (total_bytes + request_size > sw->max_bytes) {
      return false;
    }
  }

  // Add new entry
  if (sw->count < MAX_WINDOW_ENTRIES) {
    struct timeval now;
    gettimeofday(&now, NULL);

    sw->entries[sw->head].timestamp = now;
    sw->entries[sw->head].size = request_size;
    sw->head = (sw->head + 1) % MAX_WINDOW_ENTRIES;
    sw->count++;

    return true;
  }

  return false; // Buffer full
}

/* ============================================================================
 * Fixed Window Implementation
 * ============================================================================
 */

static void fixed_window_init(fixed_window_t *fw, int per_sec, int per_min) {
  fw->current_second = 0;
  fw->requests_this_second = 0;
  fw->bytes_this_second = 0;
  fw->max_per_second = per_sec;

  fw->current_minute = 0;
  fw->requests_this_minute = 0;
  fw->bytes_this_minute = 0;
  fw->max_per_minute = per_min;
}

static bool fixed_window_check(fixed_window_t *fw, size_t request_size) {
  time_t now = time(NULL);

  // Reset counters if new second
  if (now != fw->current_second) {
    fw->current_second = now;
    fw->requests_this_second = 0;
    fw->bytes_this_second = 0;
  }

  // Reset minute counter if new minute
  if (now / 60 != fw->current_minute) {
    fw->current_minute = now / 60;
    fw->requests_this_minute = 0;
    fw->bytes_this_minute = 0;
  }

  // Check per-second limit
  if (fw->max_per_second > 0 && fw->requests_this_second >= fw->max_per_second) {
    return false;
  }

  // Check per-minute limit
  if (fw->max_per_minute > 0 && fw->requests_this_minute >= fw->max_per_minute) {
    return false;
  }

  // Update counters
  fw->requests_this_second++;
  fw->requests_this_minute++;
  fw->bytes_this_second += request_size;
  fw->bytes_this_minute += request_size;

  return true;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================
 */

rate_limiter_t *rate_limiter_create(const char *name, const rate_limit_config_t *config) {
  rate_limiter_t *limiter = calloc(1, sizeof(rate_limiter_t));
  if (!limiter)
    return NULL;

  // Set name
  if (name) {
    strncpy(limiter->name, name, sizeof(limiter->name) - 1);
  }

  limiter->type = config->type;

  // Initialize based on type
  switch (config->type) {
  case RATE_LIMIT_TOKEN_BUCKET:
    token_bucket_init(&limiter->impl.token_bucket, config->max_requests_per_second, config->burst_size);
    limiter->impl.token_bucket.cost_per_byte = config->cost_per_byte;
    break;

  case RATE_LIMIT_SLIDING_WINDOW:
    sliding_window_init(&limiter->impl.sliding_window, config->window_seconds, config->max_requests_per_window);
    break;

  case RATE_LIMIT_FIXED_WINDOW:
    fixed_window_init(&limiter->impl.fixed_window, config->requests_per_second, config->requests_per_minute);
    break;
  }

  // Initialize stats
  memset(&limiter->stats, 0, sizeof(limiter->stats));
  limiter->stats.start_time = time(NULL);

  return limiter;
}

void rate_limiter_destroy(rate_limiter_t *limiter) {
  if (limiter) {
    free(limiter);
  }
}

bool rate_limiter_check(rate_limiter_t *limiter, size_t request_size) {
  if (!limiter)
    return true; // No limiter = allow all

  bool allowed = false;

  switch (limiter->type) {
  case RATE_LIMIT_TOKEN_BUCKET:
    allowed = token_bucket_check(&limiter->impl.token_bucket, request_size);
    break;

  case RATE_LIMIT_SLIDING_WINDOW:
    allowed = sliding_window_check(&limiter->impl.sliding_window, request_size);
    break;

  case RATE_LIMIT_FIXED_WINDOW:
    allowed = fixed_window_check(&limiter->impl.fixed_window, request_size);
    break;
  }

  update_stats(&limiter->stats, allowed, request_size);
  return allowed;
}

bool rate_limiter_try_consume(rate_limiter_t *limiter, size_t request_size) {
  return rate_limiter_check(limiter, request_size);
}

void rate_limiter_get_stats(rate_limiter_t *limiter, rate_limit_stats_t *stats) {
  if (limiter && stats) {
    *stats = limiter->stats;
  }
}

void rate_limiter_reset_stats(rate_limiter_t *limiter) {
  if (limiter) {
    memset(&limiter->stats, 0, sizeof(limiter->stats));
    limiter->stats.start_time = time(NULL);
  }
}

const char *rate_limiter_status(rate_limiter_t *limiter) {
  static char status[256];

  if (!limiter) {
    return "No limiter configured";
  }

  switch (limiter->type) {
  case RATE_LIMIT_TOKEN_BUCKET:
    snprintf(status, sizeof(status), "TokenBucket[%s]: %.1f/%.1f tokens, %.1f/sec refill", limiter->name,
             limiter->impl.token_bucket.tokens, limiter->impl.token_bucket.max_tokens,
             limiter->impl.token_bucket.refill_rate);
    break;

  case RATE_LIMIT_SLIDING_WINDOW:
    snprintf(status, sizeof(status), "SlidingWindow[%s]: %d/%d requests in %ds window", limiter->name,
             limiter->impl.sliding_window.count, limiter->impl.sliding_window.max_requests,
             limiter->impl.sliding_window.window_seconds);
    break;

  case RATE_LIMIT_FIXED_WINDOW:
    snprintf(status, sizeof(status), "FixedWindow[%s]: %d/sec, %d/min", limiter->name,
             limiter->impl.fixed_window.requests_this_second, limiter->impl.fixed_window.requests_this_minute);
    break;
  }

  return status;
}

/* ============================================================================
 * Convenience Functions
 * ============================================================================
 */

rate_limiter_t *rate_limiter_create_token_bucket(const char *name, double requests_per_sec, double burst_size) {
  rate_limit_config_t config = {.type = RATE_LIMIT_TOKEN_BUCKET,
                                .max_requests_per_second = requests_per_sec,
                                .burst_size = burst_size,
                                .cost_per_byte = 0};

  return rate_limiter_create(name, &config);
}

rate_limiter_t *rate_limiter_create_sliding_window(const char *name, int window_seconds, int max_requests) {
  rate_limit_config_t config = {
      .type = RATE_LIMIT_SLIDING_WINDOW, .window_seconds = window_seconds, .max_requests_per_window = max_requests};

  return rate_limiter_create(name, &config);
}

rate_limiter_t *rate_limiter_create_fixed_window(const char *name, int per_second, int per_minute) {
  rate_limit_config_t config = {
      .type = RATE_LIMIT_FIXED_WINDOW, .requests_per_second = per_second, .requests_per_minute = per_minute};

  return rate_limiter_create(name, &config);
}

/* ============================================================================
 * Multi-Rate Limiter (Per packet type)
 * ============================================================================
 */

multi_rate_limiter_t *multi_rate_limiter_create(void) {
  multi_rate_limiter_t *multi = calloc(1, sizeof(multi_rate_limiter_t));
  if (!multi)
    return NULL;

  // Video: 60 FPS max, burst of 120 frames
  multi->video_limiter = rate_limiter_create_token_bucket("video", 60.0, 120.0);

  // Audio: 100 packets/sec, burst of 200
  multi->audio_limiter = rate_limiter_create_token_bucket("audio", 100.0, 200.0);

  // Control: 10/sec, burst of 20
  multi->control_limiter = rate_limiter_create_token_bucket("control", 10.0, 20.0);

  // Bandwidth: 10MB/sec with 1KB tokens
  rate_limit_config_t bw_config = {
      .type = RATE_LIMIT_TOKEN_BUCKET,
      .max_requests_per_second = 10000.0, // 10K tokens/sec
      .burst_size = 20000.0,              // 20K tokens burst
      .cost_per_byte = 1.0 / 1024.0       // 1 token per KB
  };
  multi->bandwidth_limiter = rate_limiter_create("bandwidth", &bw_config);

  return multi;
}

void multi_rate_limiter_destroy(multi_rate_limiter_t *multi) {
  if (multi) {
    rate_limiter_destroy(multi->video_limiter);
    rate_limiter_destroy(multi->audio_limiter);
    rate_limiter_destroy(multi->control_limiter);
    rate_limiter_destroy(multi->bandwidth_limiter);
    free(multi);
  }
}

bool multi_rate_limiter_check_video(multi_rate_limiter_t *multi, size_t size) {
  if (!multi)
    return true;

  // Check both video rate and bandwidth
  return rate_limiter_check(multi->video_limiter, size) && rate_limiter_check(multi->bandwidth_limiter, size);
}

bool multi_rate_limiter_check_audio(multi_rate_limiter_t *multi, size_t size) {
  if (!multi)
    return true;

  return rate_limiter_check(multi->audio_limiter, size) && rate_limiter_check(multi->bandwidth_limiter, size);
}

bool multi_rate_limiter_check_control(multi_rate_limiter_t *multi, size_t size) {
  if (!multi)
    return true;

  return rate_limiter_check(multi->control_limiter, size) && rate_limiter_check(multi->bandwidth_limiter, size);
}
