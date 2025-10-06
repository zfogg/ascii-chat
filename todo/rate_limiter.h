#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

/* ============================================================================
 * Rate Limiter Library
 *
 * Provides multiple rate limiting strategies:
 * 1. Token Bucket - Allows bursts, controls average rate
 * 2. Sliding Window - Tracks requests over time window
 * 3. Fixed Window - Simple per-second/minute counters
 * ============================================================================
 */

// Forward declarations
typedef struct rate_limiter rate_limiter_t;
typedef struct rate_limit_config rate_limit_config_t;

// Rate limiter types
typedef enum { RATE_LIMIT_TOKEN_BUCKET, RATE_LIMIT_SLIDING_WINDOW, RATE_LIMIT_FIXED_WINDOW } rate_limit_type_t;

// Configuration for rate limits
struct rate_limit_config {
  rate_limit_type_t type;

  // Common settings
  double max_requests_per_second; // Average rate limit

  // Token bucket specific
  double burst_size;    // Max tokens (burst capacity)
  double cost_per_byte; // Tokens per byte (0 = per request)

  // Sliding window specific
  int window_seconds;          // Time window size
  int max_requests_per_window; // Max requests in window

  // Fixed window specific
  int requests_per_second; // Max requests per second
  int requests_per_minute; // Max requests per minute
};

// Statistics tracking
typedef struct {
  uint64_t allowed_count;
  uint64_t blocked_count;
  uint64_t total_bytes;
  double avg_rate;
  time_t start_time;
} rate_limit_stats_t;

/* ============================================================================
 * Token Bucket Implementation
 * ============================================================================
 */

typedef struct {
  double tokens;              // Current token count
  double max_tokens;          // Bucket capacity
  double refill_rate;         // Tokens per second
  struct timeval last_refill; // Last refill time
  double cost_per_byte;       // Token cost per byte (0 = per request)
} token_bucket_t;

/* ============================================================================
 * Sliding Window Implementation
 * ============================================================================
 */

#define MAX_WINDOW_ENTRIES 10000

typedef struct {
  struct timeval timestamp;
  size_t size;
} window_entry_t;

typedef struct {
  window_entry_t entries[MAX_WINDOW_ENTRIES];
  int head;           // Circular buffer head
  int tail;           // Circular buffer tail
  int count;          // Current entries
  int window_seconds; // Window size
  int max_requests;   // Max requests in window
  size_t max_bytes;   // Max bytes in window
} sliding_window_t;

/* ============================================================================
 * Fixed Window Implementation
 * ============================================================================
 */

typedef struct {
  // Per-second tracking
  time_t current_second;
  int requests_this_second;
  size_t bytes_this_second;
  int max_per_second;

  // Per-minute tracking
  time_t current_minute;
  int requests_this_minute;
  size_t bytes_this_minute;
  int max_per_minute;
} fixed_window_t;

/* ============================================================================
 * Main Rate Limiter Structure
 * ============================================================================
 */

struct rate_limiter {
  rate_limit_type_t type;

  // Implementation specific data
  union {
    token_bucket_t token_bucket;
    sliding_window_t sliding_window;
    fixed_window_t fixed_window;
  } impl;

  // Statistics
  rate_limit_stats_t stats;

  // Optional identifier (for logging)
  char name[64];
};

/* ============================================================================
 * Public API
 * ============================================================================
 */

// Create a rate limiter with given configuration
rate_limiter_t *rate_limiter_create(const char *name, const rate_limit_config_t *config);

// Destroy a rate limiter
void rate_limiter_destroy(rate_limiter_t *limiter);

// Check if request is allowed (returns true if allowed, false if rate limited)
bool rate_limiter_check(rate_limiter_t *limiter, size_t request_size);

// Try to consume (combines check + consume if allowed)
bool rate_limiter_try_consume(rate_limiter_t *limiter, size_t request_size);

// Get current statistics
void rate_limiter_get_stats(rate_limiter_t *limiter, rate_limit_stats_t *stats);

// Reset statistics
void rate_limiter_reset_stats(rate_limiter_t *limiter);

// Get human-readable status string
const char *rate_limiter_status(rate_limiter_t *limiter);

/* ============================================================================
 * Convenience Functions
 * ============================================================================
 */

// Create pre-configured limiters
rate_limiter_t *rate_limiter_create_token_bucket(const char *name, double requests_per_sec, double burst_size);

rate_limiter_t *rate_limiter_create_sliding_window(const char *name, int window_seconds, int max_requests);

rate_limiter_t *rate_limiter_create_fixed_window(const char *name, int per_second, int per_minute);

// Per-packet-type rate limiting
typedef struct {
  rate_limiter_t *video_limiter;
  rate_limiter_t *audio_limiter;
  rate_limiter_t *control_limiter;
  rate_limiter_t *bandwidth_limiter;
} multi_rate_limiter_t;

multi_rate_limiter_t *multi_rate_limiter_create(void);
void multi_rate_limiter_destroy(multi_rate_limiter_t *multi);
bool multi_rate_limiter_check_video(multi_rate_limiter_t *multi, size_t size);
bool multi_rate_limiter_check_audio(multi_rate_limiter_t *multi, size_t size);
bool multi_rate_limiter_check_control(multi_rate_limiter_t *multi, size_t size);

#endif // RATE_LIMITER_H
