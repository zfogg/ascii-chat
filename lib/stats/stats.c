#include <ascii-chat/stats/stats.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/util/time.h>
#include <string.h>

struct stats_scope {
  uint64_t id;
  uint64_t started_ns;
  stats_capabilities_t capabilities;
  atomic_t counters[STATS_COUNTER_COUNT];
  atomic_t gauges[STATS_GAUGE_COUNT];
  mutex_t snapshot_mutex;
  mutex_t duration_mutex;
  stats_duration_snapshot_t durations[STATS_DURATION_COUNT];
};

struct stats_sampler {
  stats_sampler_config_t config;
  stats_snapshot_t *history;
  stats_snapshot_t latest;
  size_t capacity;
  size_t head;
  size_t count;
};

static atomic_t next_scope_id = {0};

asciichat_error_t stats_scope_create(const stats_capabilities_t *capabilities, stats_scope_t **out_scope) {
  if (!out_scope)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing stats scope output");
  *out_scope = NULL;
  if (!capabilities)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing stats capabilities");
  stats_scope_t *scope = SAFE_CALLOC(1, sizeof(*scope), stats_scope_t *);
  if (!scope)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate stats scope");
  if (mutex_init(&scope->snapshot_mutex, "stats_snapshot") != 0) {
    SAFE_FREE(scope);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize stats snapshot mutex");
  }
  if (mutex_init(&scope->duration_mutex, "stats_duration") != 0) {
    mutex_destroy(&scope->snapshot_mutex);
    SAFE_FREE(scope);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize stats duration mutex");
  }
  scope->capabilities = *capabilities;
  // Bypass debug atomic instrumentation so recording never recursively collects or allocates.
  scope->id = atomic_fetch_add_u64_impl(&next_scope_id, 1) + 1;
  scope->started_ns = time_get_ns();
  *out_scope = scope;
  return ASCIICHAT_OK;
}

void stats_scope_destroy(stats_scope_t *scope) {
  if (!scope)
    return;
  mutex_destroy(&scope->duration_mutex);
  mutex_destroy(&scope->snapshot_mutex);
  SAFE_FREE(scope);
}

void stats_counter_add(stats_scope_t *scope, stats_counter_id_t id, uint64_t amount) {
  if (scope && (unsigned)id < STATS_COUNTER_COUNT && scope->capabilities.counters[id])
    atomic_fetch_add_u64_impl(&scope->counters[id], amount);
}

void stats_gauge_set(stats_scope_t *scope, stats_gauge_id_t id, uint64_t value) {
  if (scope && (unsigned)id < STATS_GAUGE_COUNT && scope->capabilities.gauges[id])
    atomic_store_u64_impl(&scope->gauges[id], value);
}

void stats_duration_record(stats_scope_t *scope, stats_duration_id_t id, uint64_t elapsed_ns) {
  if (!scope || (unsigned)id >= STATS_DURATION_COUNT || !scope->capabilities.durations[id])
    return;
  if (mutex_lock(&scope->duration_mutex) != 0)
    return;
  stats_duration_snapshot_t *duration = &scope->durations[id];
  duration->last_ns = elapsed_ns;
  if (!duration->observations || elapsed_ns < duration->min_ns)
    duration->min_ns = elapsed_ns;
  if (elapsed_ns > duration->max_ns)
    duration->max_ns = elapsed_ns;
  duration->observations = duration->observations == UINT64_MAX ? 0 : duration->observations + 1;
  // Unsigned wrap is intentional; an observed decrease causes samplers to rebaseline.
  duration->total_ns = duration->total_ns > UINT64_MAX - elapsed_ns ? elapsed_ns - (UINT64_MAX - duration->total_ns) - 1
                                                                    : duration->total_ns + elapsed_ns;
  mutex_unlock(&scope->duration_mutex);
}

asciichat_error_t stats_scope_snapshot(const stats_scope_t *scope, stats_snapshot_t *out) {
  if (!scope || !out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing stats scope or snapshot output");
  // Logical constness: collection is unchanged; only snapshot synchronization is mutable.
  stats_scope_t *mutable_scope = (stats_scope_t *)scope;
  if (mutex_lock(&mutable_scope->snapshot_mutex) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot lock stats snapshot");
  memset(out, 0, sizeof(*out));
  out->scope_id = scope->id;
  out->started_ns = scope->started_ns;
  out->capabilities = scope->capabilities;
  for (size_t i = 0; i < STATS_COUNTER_COUNT; ++i)
    out->counters[i] = atomic_load_u64_impl(&scope->counters[i]);
  for (size_t i = 0; i < STATS_GAUGE_COUNT; ++i)
    out->gauges[i] = atomic_load_u64_impl(&scope->gauges[i]);
  if (mutex_lock(&mutable_scope->duration_mutex) != 0) {
    mutex_unlock(&mutable_scope->snapshot_mutex);
    return SET_ERRNO(ERROR_THREAD, "Cannot lock stats durations");
  }
  memcpy(out->durations, scope->durations, sizeof(out->durations));
  mutex_unlock(&mutable_scope->duration_mutex);
  out->sampled_ns = time_get_ns();
  mutex_unlock(&mutable_scope->snapshot_mutex);
  return ASCIICHAT_OK;
}

asciichat_error_t stats_sampler_create(const stats_sampler_config_t *config, stats_sampler_t **out_sampler) {
  if (!out_sampler)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing stats sampler output");
  *out_sampler = NULL;
  if (!config || !config->resolution_ns || config->window_ns < config->resolution_ns)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stats sampling interval");
  uint64_t slots = config->window_ns / config->resolution_ns + (config->window_ns % config->resolution_ns != 0);
  if (slots > 4096)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Stats sampler exceeds 4096 history intervals");
  stats_sampler_t *sampler = SAFE_CALLOC(1, sizeof(*sampler), stats_sampler_t *);
  if (!sampler)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate stats sampler");
  sampler->capacity = (size_t)slots + 2;
  sampler->history = SAFE_CALLOC(sampler->capacity, sizeof(*sampler->history), stats_snapshot_t *);
  if (!sampler->history) {
    SAFE_FREE(sampler);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate stats history");
  }
  sampler->config = *config;
  *out_sampler = sampler;
  return ASCIICHAT_OK;
}

static bool stats_baseline_changed(const stats_snapshot_t *old, const stats_snapshot_t *current) {
  if (old->scope_id != current->scope_id || current->sampled_ns <= old->sampled_ns)
    return true;
  for (size_t i = 0; i < STATS_COUNTER_COUNT; ++i)
    if (old->capabilities.counters[i] != current->capabilities.counters[i] || current->counters[i] < old->counters[i])
      return true;
  for (size_t i = 0; i < STATS_GAUGE_COUNT; ++i)
    if (old->capabilities.gauges[i] != current->capabilities.gauges[i])
      return true;
  for (size_t i = 0; i < STATS_DURATION_COUNT; ++i)
    if (old->capabilities.durations[i] != current->capabilities.durations[i] ||
        current->durations[i].observations < old->durations[i].observations ||
        current->durations[i].total_ns < old->durations[i].total_ns)
      return true;
  return false;
}

asciichat_error_t stats_sampler_update(stats_sampler_t *sampler, const stats_snapshot_t *snapshot,
                                       stats_rates_t *out_rates) {
  if (!sampler || !snapshot || !out_rates)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing stats sampler, snapshot, or rates output");
  memset(out_rates, 0, sizeof(*out_rates));
  if (!sampler->count || stats_baseline_changed(&sampler->latest, snapshot)) {
    sampler->head = 0;
    sampler->count = 1;
    sampler->history[0] = *snapshot;
    sampler->latest = *snapshot;
    return ASCIICHAT_OK;
  }
  uint64_t cutoff =
      snapshot->sampled_ns > sampler->config.window_ns ? snapshot->sampled_ns - sampler->config.window_ns : 0;
  while (sampler->count > 1) {
    size_t next = (sampler->head + 1) % sampler->capacity;
    if (sampler->history[next].sampled_ns > cutoff)
      break;
    sampler->head = next;
    sampler->count--;
  }
  const stats_snapshot_t *baseline = &sampler->history[sampler->head];
  out_rates->elapsed_ns = snapshot->sampled_ns - baseline->sampled_ns;
  out_rates->ready = true;
  for (size_t i = 0; i < STATS_COUNTER_COUNT; ++i)
    if (snapshot->capabilities.counters[i])
      out_rates->counters_per_second[i] =
          (double)(snapshot->counters[i] - baseline->counters[i]) * 1e9 / (double)out_rates->elapsed_ns;
  for (size_t i = 0; i < STATS_DURATION_COUNT; ++i) {
    uint64_t observations = snapshot->durations[i].observations - baseline->durations[i].observations;
    if (snapshot->capabilities.durations[i] && observations) {
      out_rates->duration_valid[i] = true;
      out_rates->duration_mean_ns[i] =
          (double)(snapshot->durations[i].total_ns - baseline->durations[i].total_ns) / (double)observations;
    }
  }
  size_t tail = (sampler->head + sampler->count - 1) % sampler->capacity;
  if (snapshot->sampled_ns - sampler->history[tail].sampled_ns >= sampler->config.resolution_ns) {
    size_t next = (tail + 1) % sampler->capacity;
    sampler->history[next] = *snapshot;
    if (sampler->count == sampler->capacity)
      sampler->head = (sampler->head + 1) % sampler->capacity;
    else
      sampler->count++;
  }
  sampler->latest = *snapshot;
  return ASCIICHAT_OK;
}

void stats_sampler_destroy(stats_sampler_t *sampler) {
  if (!sampler)
    return;
  SAFE_FREE(sampler->history);
  SAFE_FREE(sampler);
}
