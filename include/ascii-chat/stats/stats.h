#pragma once

/** Typed, process-local measurements. No UI, mode globals, or worker threads. */
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stats_scope stats_scope_t;
typedef struct stats_sampler stats_sampler_t;

typedef enum {
  STATS_COUNTER_FRAMES_CAPTURED,
  STATS_COUNTER_FRAMES_CONVERTED,
  STATS_COUNTER_FRAMES_ENCODED,
  STATS_COUNTER_FRAMES_DECODED,
  STATS_COUNTER_FRAMES_PRESENTED,
  STATS_COUNTER_FRAMES_SKIPPED,
  STATS_COUNTER_FRAMES_DROPPED,
  STATS_COUNTER_PACKETS_SENT,
  STATS_COUNTER_PACKETS_RECEIVED,
  STATS_COUNTER_BYTES_SENT,
  STATS_COUNTER_BYTES_RECEIVED,
  STATS_COUNTER_SEND_ERRORS,
  STATS_COUNTER_RECEIVE_ERRORS,
  STATS_COUNTER_AUDIO_UNDERRUNS,
  STATS_COUNTER_AUDIO_OVERRUNS,
  STATS_COUNTER_SESSION_CREATES,
  STATS_COUNTER_SESSION_JOINS,
  STATS_COUNTER_SESSION_LOOKUPS,
  STATS_COUNTER_REQUEST_FAILURES,
  STATS_COUNTER_RATE_LIMIT_REJECTIONS,
  STATS_COUNTER_SESSION_EXPIRATIONS,
  STATS_COUNTER_HOST_MIGRATIONS,
  STATS_COUNTER_COUNT
} stats_counter_id_t;

typedef enum {
  STATS_GAUGE_CONNECTIONS_ACTIVE,
  STATS_GAUGE_SESSIONS_ACTIVE,
  STATS_GAUGE_PARTICIPANTS_ACTIVE,
  STATS_GAUGE_VIDEO_QUEUE_DEPTH,
  STATS_GAUGE_AUDIO_BUFFERED_SAMPLES,
  STATS_GAUGE_COUNT
} stats_gauge_id_t;

typedef enum {
  STATS_DURATION_CAPTURE,
  STATS_DURATION_ASCII_CONVERT,
  STATS_DURATION_ENCODE,
  STATS_DURATION_DECODE,
  STATS_DURATION_AUDIO_MIX,
  STATS_DURATION_TERMINAL_WRITE,
  STATS_DURATION_CONNECTION_SETUP,
  STATS_DURATION_REQUEST,
  STATS_DURATION_RTT,
  STATS_DURATION_COUNT
} stats_duration_id_t;

typedef struct {
  const char *name;
  const char *unit;
  const char *description;
} stats_descriptor_t;

/** Static metadata; NULL for an invalid ID. Names are stable, lowercase identifiers. */
const stats_descriptor_t *stats_counter_descriptor(stats_counter_id_t id);
const stats_descriptor_t *stats_gauge_descriptor(stats_gauge_id_t id);
const stats_descriptor_t *stats_duration_descriptor(stats_duration_id_t id);

typedef struct {
  bool counters[STATS_COUNTER_COUNT];
  bool gauges[STATS_GAUGE_COUNT];
  bool durations[STATS_DURATION_COUNT];
} stats_capabilities_t;

typedef struct {
  uint64_t observations;
  uint64_t total_ns;
  uint64_t min_ns;
  uint64_t max_ns;
} stats_duration_snapshot_t;

typedef struct {
  uint64_t scope_id;
  uint64_t started_ns;
  uint64_t sampled_ns;
  stats_capabilities_t capabilities;
  uint64_t counters[STATS_COUNTER_COUNT];
  uint64_t gauges[STATS_GAUGE_COUNT];
  stats_duration_snapshot_t durations[STATS_DURATION_COUNT];
} stats_snapshot_t;

/** Copies capabilities. Create before workers start; destroy after all users join.
 * No reset or automatic parent aggregation. New sessions should get new scopes.
 */
asciichat_error_t stats_scope_create(const stats_capabilities_t *capabilities, stats_scope_t **out_scope);
void stats_scope_destroy(stats_scope_t *scope);

/** Thread-safe recording with no module allocations. Platform mutex diagnostics may
 * initialize per-thread storage on first use in debug builds. NULL, invalid IDs, and unsupported metrics
 * are ignored. Counters and duration totals wrap at UINT64_MAX; samplers rebaseline
 * on observed decreases. Durations measure attempts (including failures); successful
 * operations are counted separately. A gauge's producer owns its current value.
 */
void stats_counter_add(stats_scope_t *scope, stats_counter_id_t id, uint64_t amount);
void stats_gauge_set(stats_scope_t *scope, stats_gauge_id_t id, uint64_t value);
void stats_duration_record(stats_scope_t *scope, stats_duration_id_t id, uint64_t elapsed_ns);

/** Owned value copy. Concurrent snapshots are serialized. Each duration tuple is
 * coherent, but independent metrics need not describe one atomic application event.
 * Empty durations have observations/min/max/total equal to zero.
 */
asciichat_error_t stats_scope_snapshot(const stats_scope_t *scope, stats_snapshot_t *out);

typedef struct {
  uint64_t window_ns;
  uint64_t resolution_ns;
} stats_sampler_config_t;

typedef struct {
  bool ready;
  uint64_t elapsed_ns;
  double counters_per_second[STATS_COUNTER_COUNT];
  bool duration_valid[STATS_DURATION_COUNT];
  double duration_mean_ns[STATS_DURATION_COUNT];
} stats_rates_t;

/** Bounded history: ceil(window/resolution) must be in [1, 4096]. Both durations
 * must be nonzero and resolution <= window. Each consumer owns its own sampler;
 * sampler operations are not thread-safe. All allocation happens at creation.
 */
asciichat_error_t stats_sampler_create(const stats_sampler_config_t *config, stats_sampler_t **out_sampler);

/** Samples must be supplied regularly, even while idle. Rates use actual elapsed
 * time from the latest retained sample at/before the window boundary (or the oldest
 * available sample during warmup). Resolution controls history retention, not calls.
 * ready becomes true with a positive interval; elapsed_ns exposes window warmup.
 * Scope/capability changes, nonincreasing timestamps, or decreasing totals reset the
 * baseline and return ready=false. Unsupported values are zero/invalid; consult the
 * snapshot capabilities to distinguish unsupported counters from measured zero.
 */
asciichat_error_t stats_sampler_update(stats_sampler_t *sampler, const stats_snapshot_t *snapshot,
                                       stats_rates_t *out_rates);
void stats_sampler_destroy(stats_sampler_t *sampler);

#ifdef __cplusplus
}
#endif
