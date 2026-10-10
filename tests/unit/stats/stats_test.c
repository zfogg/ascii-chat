#include <criterion/criterion.h>
#include <ascii-chat/stats/stats.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/thread.h>
#include <string.h>

static stats_capabilities_t capabilities(void) {
  stats_capabilities_t caps = {0};
  caps.counters[STATS_COUNTER_FRAMES_CAPTURED] = true;
  caps.gauges[STATS_GAUGE_CONNECTIONS_ACTIVE] = true;
  caps.durations[STATS_DURATION_CAPTURE] = true;
  return caps;
}

static stats_scope_t *make_scope(void) {
  stats_scope_t *scope = NULL;
  stats_capabilities_t caps = capabilities();
  cr_assert_eq(stats_scope_create(&caps, &scope), ASCIICHAT_OK);
  return scope;
}

static stats_sampler_t *make_sampler(uint64_t window, uint64_t resolution) {
  stats_sampler_t *sampler = NULL;
  stats_sampler_config_t config = {.window_ns = window, .resolution_ns = resolution};
  cr_assert_eq(stats_sampler_create(&config, &sampler), ASCIICHAT_OK);
  return sampler;
}

static stats_snapshot_t sample(uint64_t time, uint64_t frames) {
  stats_snapshot_t snapshot = {.scope_id = 1, .sampled_ns = time, .capabilities = capabilities()};
  snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED] = frames;
  return snapshot;
}

Test(stats, scope_values_and_capabilities) {
  stats_scope_t *scope = make_scope();
  stats_snapshot_t initial, current;
  cr_assert_eq(stats_scope_snapshot(scope, &initial), ASCIICHAT_OK);
  cr_assert(initial.capabilities.counters[STATS_COUNTER_FRAMES_CAPTURED]);
  cr_assert_not(initial.capabilities.counters[STATS_COUNTER_FRAMES_DROPPED]);
  cr_assert_eq(initial.durations[STATS_DURATION_CAPTURE].observations, 0);
  cr_assert_eq(initial.durations[STATS_DURATION_CAPTURE].min_ns, 0);
  stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, 3);
  stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, 5);
  stats_counter_add(scope, STATS_COUNTER_FRAMES_DROPPED, 10);
  stats_gauge_set(scope, STATS_GAUGE_CONNECTIONS_ACTIVE, 4);
  stats_gauge_set(scope, STATS_GAUGE_CONNECTIONS_ACTIVE, 1);
  stats_gauge_set(scope, STATS_GAUGE_SESSIONS_ACTIVE, 4);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 10);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 0);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 20);
  stats_duration_record(scope, STATS_DURATION_DECODE, 20);
  cr_assert_eq(stats_scope_snapshot(scope, &current), ASCIICHAT_OK);
  cr_assert_eq(current.scope_id, initial.scope_id);
  cr_assert_geq(current.sampled_ns, current.started_ns);
  cr_assert_eq(current.counters[STATS_COUNTER_FRAMES_CAPTURED], 8);
  cr_assert_eq(current.counters[STATS_COUNTER_FRAMES_DROPPED], 0);
  cr_assert_eq(current.gauges[STATS_GAUGE_CONNECTIONS_ACTIVE], 1);
  cr_assert_eq(current.gauges[STATS_GAUGE_SESSIONS_ACTIVE], 0);
  stats_duration_snapshot_t duration = current.durations[STATS_DURATION_CAPTURE];
  cr_assert_eq(duration.observations, 3);
  cr_assert_eq(duration.total_ns, 30);
  cr_assert_eq(duration.min_ns, 0);
  cr_assert_eq(duration.max_ns, 20);
  cr_assert_eq(current.durations[STATS_DURATION_DECODE].observations, 0);
  stats_scope_destroy(scope);
  scope = make_scope();
  cr_assert_eq(stats_scope_snapshot(scope, &initial), ASCIICHAT_OK);
  cr_assert_neq(initial.scope_id, current.scope_id);
  cr_assert_eq(current.counters[STATS_COUNTER_FRAMES_CAPTURED], 8);
  stats_scope_destroy(scope);
}

Test(stats, invalid_arguments) {
  stats_scope_t *scope = NULL;
  stats_sampler_t *sampler = NULL;
  stats_snapshot_t snapshot;
  stats_rates_t rates;
  cr_assert_eq(stats_scope_create(NULL, &scope), ERROR_INVALID_PARAM);
  cr_assert_null(scope);
  cr_assert_eq(stats_scope_create(NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(stats_scope_snapshot(NULL, &snapshot), ERROR_INVALID_PARAM);
  scope = make_scope();
  cr_assert_eq(stats_scope_snapshot(scope, NULL), ERROR_INVALID_PARAM);
  stats_counter_add(scope, (stats_counter_id_t)-1, 1);
  stats_gauge_set(scope, STATS_GAUGE_COUNT, 1);
  stats_duration_record(scope, (stats_duration_id_t)-1, 1);
  stats_counter_add(NULL, STATS_COUNTER_FRAMES_CAPTURED, 1);
  stats_gauge_set(NULL, STATS_GAUGE_CONNECTIONS_ACTIVE, 1);
  stats_duration_record(NULL, STATS_DURATION_CAPTURE, 1);
  stats_scope_destroy(scope);
  stats_scope_destroy(NULL);
  const stats_sampler_config_t invalid[] = {{0, 0}, {1, 0}, {1, 2}, {4097, 1}, {UINT64_MAX, 1}};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    cr_assert_eq(stats_sampler_create(&invalid[i], &sampler), ERROR_INVALID_PARAM);
    cr_assert_null(sampler);
  }
  cr_assert_eq(stats_sampler_create(NULL, &sampler), ERROR_INVALID_PARAM);
  cr_assert_eq(stats_sampler_create(NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(stats_sampler_update(NULL, &snapshot, &rates), ERROR_INVALID_PARAM);
  sampler = make_sampler(UINT64_MAX, UINT64_MAX);
  cr_assert_eq(stats_sampler_update(sampler, NULL, &rates), ERROR_INVALID_PARAM);
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, NULL), ERROR_INVALID_PARAM);
  stats_sampler_destroy(sampler);
  stats_sampler_destroy(NULL);
}

Test(stats, rolling_window_and_idle) {
  stats_sampler_t *sampler = make_sampler(1000000000, 250000000);
  stats_rates_t rates;
  // Exercise many ring wraps, including updates faster than retained history.
  for (uint64_t i = 0; i <= 200; ++i) {
    stats_snapshot_t snapshot = sample(i * 50000000, i < 100 ? i * 3 : 300);
    cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
    if (i == 0) {
      cr_assert_not(rates.ready);
    } else if (i < 100) {
      cr_assert(rates.ready);
      cr_assert_float_eq(rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED], 60, 0.0001);
    } else if (i >= 125) {
      cr_assert_eq(rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED], 0);
    }
    if (i >= 20) {
      cr_assert_geq(rates.elapsed_ns, 1000000000);
      cr_assert_lt(rates.elapsed_ns, 1250000000);
    }
  }
  stats_sampler_destroy(sampler);
}

Test(stats, irregular_intervals_and_duration_means) {
  stats_sampler_t *sampler = make_sampler(1000, 300);
  stats_rates_t rates;
  stats_snapshot_t snapshot = sample(0, UINT64_MAX - 100);
  snapshot.durations[STATS_DURATION_CAPTURE] = (stats_duration_snapshot_t){10, 1000, 10, 200};
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  snapshot.sampled_ns = 700;
  snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED] += 7;
  snapshot.durations[STATS_DURATION_CAPTURE].observations += 2;
  snapshot.durations[STATS_DURATION_CAPTURE].total_ns += 60;
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_eq(rates.elapsed_ns, 700);
  cr_assert_float_eq(rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED], 10000000, 0.001);
  cr_assert(rates.duration_valid[STATS_DURATION_CAPTURE]);
  cr_assert_eq(rates.duration_mean_ns[STATS_DURATION_CAPTURE], 30);
  cr_assert_not(rates.duration_valid[STATS_DURATION_DECODE]);
  snapshot.sampled_ns = 1700;
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_eq(rates.elapsed_ns, 1000);
  cr_assert_eq(rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED], 0);
  cr_assert_not(rates.duration_valid[STATS_DURATION_CAPTURE]);
  // A long sampling gap uses its actual duration, not the nominal window.
  snapshot.sampled_ns = 100000;
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_eq(rates.elapsed_ns, 98300);
  stats_sampler_destroy(sampler);
}

Test(stats, independent_consumers_and_baseline_changes) {
  stats_sampler_t *a = make_sampler(100, 25);
  stats_sampler_t *b = make_sampler(100, 25);
  stats_rates_t rates;
  stats_snapshot_t snapshot = sample(0, 0);
  cr_assert_eq(stats_sampler_update(a, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_eq(stats_sampler_update(b, &snapshot, &rates), ASCIICHAT_OK);
  snapshot = sample(50, 5);
  cr_assert_eq(stats_sampler_update(a, &snapshot, &rates), ASCIICHAT_OK);
  snapshot = sample(150, 5);
  cr_assert_eq(stats_sampler_update(a, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_eq(rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED], 0);
  cr_assert_eq(stats_sampler_update(b, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_float_eq(rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED], 5e9 / 150, 0.01);
  for (int change = 0; change < 6; ++change) {
    snapshot = sample(500, 20);
    snapshot.durations[STATS_DURATION_CAPTURE] = (stats_duration_snapshot_t){2, 20, 10, 10};
    cr_assert_eq(stats_sampler_update(a, &snapshot, &rates), ASCIICHAT_OK);
    stats_snapshot_t changed = snapshot;
    changed.sampled_ns = 550;
    switch (change) {
    case 0:
      changed.scope_id++;
      break;
    case 1:
      changed.sampled_ns = 500;
      break;
    case 2:
      changed.sampled_ns = 1;
      break;
    case 3:
      changed.counters[STATS_COUNTER_FRAMES_CAPTURED] = 1;
      break;
    case 4:
      changed.durations[STATS_DURATION_CAPTURE].total_ns = 1;
      break;
    case 5:
      changed.capabilities.counters[STATS_COUNTER_FRAMES_DROPPED] = true;
      break;
    }
    cr_assert_eq(stats_sampler_update(a, &changed, &rates), ASCIICHAT_OK);
    cr_assert_not(rates.ready);
    cr_assert_eq(rates.elapsed_ns, 0);
    changed.sampled_ns++;
    cr_assert_eq(stats_sampler_update(a, &changed, &rates), ASCIICHAT_OK);
    cr_assert(rates.ready);
  }
  stats_sampler_destroy(a);
  stats_sampler_destroy(b);
}

typedef struct {
  stats_scope_t *scope;
  atomic_t done;
} worker_context_t;

static void *record_worker(void *arg) {
  worker_context_t *context = arg;
  for (size_t i = 0; i < 10000; ++i) {
    stats_counter_add(context->scope, STATS_COUNTER_FRAMES_CAPTURED, 1);
    stats_duration_record(context->scope, STATS_DURATION_CAPTURE, 7);
  }
  atomic_fetch_add_u64_impl(&context->done, 1);
  return NULL;
}

Test(stats, concurrent_recording_and_snapshots, .timeout = 30) {
  worker_context_t context = {.scope = make_scope(), .done = {0}};
  asciichat_thread_t threads[4];
  for (size_t i = 0; i < 4; ++i)
    cr_assert_eq(asciichat_thread_create(&threads[i], "stats_test", record_worker, &context), 0);
  uint64_t previous = 0;
  stats_snapshot_t snapshot;
  do {
    cr_assert_eq(stats_scope_snapshot(context.scope, &snapshot), ASCIICHAT_OK);
    cr_assert_geq(snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED], previous);
    previous = snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED];
    stats_duration_snapshot_t d = snapshot.durations[STATS_DURATION_CAPTURE];
    cr_assert_eq(d.total_ns, d.observations * 7);
    if (d.observations) {
      cr_assert_eq(d.min_ns, 7);
      cr_assert_eq(d.max_ns, 7);
    }
  } while (atomic_load_u64_impl(&context.done) != 4);
  for (size_t i = 0; i < 4; ++i)
    cr_assert_eq(asciichat_thread_join(&threads[i], NULL), 0);
  cr_assert_eq(stats_scope_snapshot(context.scope, &snapshot), ASCIICHAT_OK);
  cr_assert_eq(snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED], 40000);
  cr_assert_eq(snapshot.durations[STATS_DURATION_CAPTURE].observations, 40000);
  stats_scope_destroy(context.scope);
}

Test(stats, last_duration_survives_idle_and_tracks_latest_attempt) {
  stats_scope_t *scope = make_scope();
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 900);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 200);
  stats_snapshot_t snapshot;
  cr_assert_eq(stats_scope_snapshot(scope, &snapshot), ASCIICHAT_OK);
  cr_assert_eq(snapshot.durations[STATS_DURATION_CAPTURE].last_ns, 200);
  cr_assert_eq(snapshot.durations[STATS_DURATION_CAPTURE].max_ns, 900);
  stats_sampler_t *sampler = make_sampler(100, 25);
  stats_rates_t rates;
  snapshot.sampled_ns = 100;
  stats_sampler_update(sampler, &snapshot, &rates);
  snapshot.sampled_ns = 300;
  stats_sampler_update(sampler, &snapshot, &rates);
  cr_assert_not(rates.duration_valid[STATS_DURATION_CAPTURE]);
  cr_assert_eq(snapshot.durations[STATS_DURATION_CAPTURE].last_ns, 200);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 0);
  stats_scope_snapshot(scope, &snapshot);
  cr_assert_eq(snapshot.durations[STATS_DURATION_CAPTURE].last_ns, 0);
  stats_sampler_destroy(sampler);
  stats_scope_destroy(scope);
}

Test(stats, descriptors) {
  for (int i = 0; i < STATS_COUNTER_COUNT; ++i) {
    const stats_descriptor_t *d = stats_counter_descriptor((stats_counter_id_t)i);
    cr_assert_not_null(d);
    cr_assert(d->name && d->unit && d->description);
    for (int j = 0; j < i; ++j)
      cr_assert_neq(strcmp(d->name, stats_counter_descriptor((stats_counter_id_t)j)->name), 0);
  }
  for (int i = 0; i < STATS_GAUGE_COUNT; ++i) {
    const stats_descriptor_t *d = stats_gauge_descriptor((stats_gauge_id_t)i);
    cr_assert(d && d->name && d->unit && d->description);
  }
  for (int i = 0; i < STATS_DURATION_COUNT; ++i) {
    const stats_descriptor_t *d = stats_duration_descriptor((stats_duration_id_t)i);
    cr_assert(d && d->name && d->unit && d->description);
  }
  cr_assert_null(stats_counter_descriptor((stats_counter_id_t)-1));
  cr_assert_null(stats_gauge_descriptor(STATS_GAUGE_COUNT));
  cr_assert_null(stats_duration_descriptor(STATS_DURATION_COUNT));
}

Test(stats, capabilities_are_copied_and_wrap_rebaselines) {
  stats_capabilities_t caps = capabilities();
  stats_scope_t *scope = NULL;
  cr_assert_eq(stats_scope_create(&caps, &scope), ASCIICHAT_OK);
  memset(&caps, 0, sizeof(caps));
  stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, UINT64_MAX);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, UINT64_MAX);
  stats_snapshot_t snapshot;
  cr_assert_eq(stats_scope_snapshot(scope, &snapshot), ASCIICHAT_OK);
  cr_assert_eq(snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED], UINT64_MAX);
  stats_sampler_t *sampler = make_sampler(100, 25);
  stats_rates_t rates;
  snapshot.sampled_ns = 100;
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, 2);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 2);
  cr_assert_eq(stats_scope_snapshot(scope, &snapshot), ASCIICHAT_OK);
  snapshot.sampled_ns = 200;
  cr_assert_eq(snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED], 1);
  cr_assert_eq(snapshot.durations[STATS_DURATION_CAPTURE].total_ns, 1);
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert_not(rates.ready);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 0);
  cr_assert_eq(stats_scope_snapshot(scope, &snapshot), ASCIICHAT_OK);
  snapshot.sampled_ns = 300;
  cr_assert_eq(stats_sampler_update(sampler, &snapshot, &rates), ASCIICHAT_OK);
  cr_assert(rates.ready);
  cr_assert(rates.duration_valid[STATS_DURATION_CAPTURE]);
  cr_assert_eq(rates.duration_mean_ns[STATS_DURATION_CAPTURE], 0);
  stats_sampler_destroy(sampler);
  stats_scope_destroy(scope);
}
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/parsing.h>

Test(stats, transport_totals_survive_disconnect_and_restart) {
  options_state_init();
  cr_assert_eq(stats_runtime_start("client", 0), ASCIICHAT_OK);
  const char *transports[] = {"TCP", "WebSocket", "WebRTC"};
  for (unsigned i = 0; i < 3; ++i) {
    stats_peer_t *peer = stats_runtime_peer_open(transports[i]);
    cr_assert_not_null(peer);
    stats_runtime_packet(peer, PACKET_TYPE_IMAGE_FRAME_H265, 101, true, true);
    stats_runtime_packet(peer, PACKET_TYPE_ASCII_FRAME, 203, false, true);
    stats_runtime_packet(peer, PACKET_TYPE_IMAGE_FRAME_H265, 999, true, false);
    stats_runtime_packet(peer, 0, 0, false, false);
    stats_snapshot_t local;
    cr_assert_eq(stats_scope_snapshot(stats_runtime_peer_scope(peer), &local), ASCIICHAT_OK);
    cr_assert_eq(local.counters[STATS_COUNTER_BYTES_SENT], sizeof(packet_header_t) + 101);
    cr_assert_eq(local.counters[STATS_COUNTER_BYTES_RECEIVED], sizeof(packet_header_t) + 203);
    cr_assert_eq(local.counters[STATS_COUNTER_SEND_ERRORS], 1);
    cr_assert_eq(local.counters[STATS_COUNTER_RECEIVE_ERRORS], 1);
    cr_assert_eq(local.counters[STATS_COUNTER_FRAMES_SENT], 1);
    cr_assert_eq(local.counters[STATS_COUNTER_FRAMES_RECEIVED], 1);
    stats_runtime_peer_close(peer);
  }
  char *decoded = packet_decode_frame_data_malloc("abc", 3, false, 3, 0);
  cr_assert_not_null(decoded);
  SAFE_FREE(decoded);
  cr_assert_null(packet_decode_frame_data_malloc("abc", 3, false, 4, 0));
  stats_snapshot_t total;
  cr_assert_eq(stats_scope_snapshot(stats_runtime_scope(), &total), ASCIICHAT_OK);
  cr_assert_eq(total.counters[STATS_COUNTER_BYTES_SENT], 3 * (sizeof(packet_header_t) + 101));
  cr_assert_eq(total.counters[STATS_COUNTER_BYTES_RECEIVED], 3 * (sizeof(packet_header_t) + 203));
  cr_assert_eq(total.counters[STATS_COUNTER_PACKETS_SENT], 3);
  cr_assert_eq(total.counters[STATS_COUNTER_SEND_ERRORS], 3);
  cr_assert_eq(total.counters[STATS_COUNTER_FRAMES_DECODED], 1);
  cr_assert_eq(total.counters[STATS_COUNTER_FRAMES_DROPPED], 1);
  cr_assert_eq(total.durations[STATS_DURATION_DECODE].observations, 2);
  uint64_t old_scope = total.scope_id;
  stats_runtime_stop();
  cr_assert_eq(stats_runtime_start("mirror", 0), ASCIICHAT_OK);
  cr_assert_eq(stats_scope_snapshot(stats_runtime_scope(), &total), ASCIICHAT_OK);
  cr_assert_neq(total.scope_id, old_scope);
  cr_assert_not(total.capabilities.counters[STATS_COUNTER_BYTES_SENT]);
  cr_assert_eq(total.counters[STATS_COUNTER_FRAMES_CAPTURED], 0);
  stats_runtime_stop();
  options_state_destroy();
}

Test(stats, view_labels_cannot_inject_terminal_controls) {
  stats_view_t view = {0};
  stats_view_add(&view, "peer %s", "bad\033[2J\nname");
  cr_assert_eq(view.count, 1);
  cr_assert_str_eq(view.lines[0], "peer bad?[2J?name");
  for (unsigned i = 0; i < STATS_VIEW_LINES + 10; ++i)
    stats_view_add(&view, "%u", i);
  cr_assert_eq(view.count, STATS_VIEW_LINES);
}
#include <ascii-chat/debug/named.h>
static void count_named_object(uintptr_t key, void *data) {
  (void)key;
  (*(unsigned *)data)++;
}
Test(stats, duplicate_debug_registration_is_fully_retired) {
#ifndef NDEBUG
  cr_assert_eq(named_init(), ASCIICHAT_OK);
  unsigned object = 0, reads = 0;
  uintptr_t key = (uintptr_t)&object;
  named_register(key, "condition", "cond", "%p", __FILE__, __LINE__, __func__, 0);
  named_register(key, "parent_condition", "cond", "%p", __FILE__, __LINE__, __func__, 0);
  cr_assert(named_registry_read(key, "cond", count_named_object, &reads));
  named_unregister(key);
  cr_assert_not(named_registry_read(key, "cond", count_named_object, &reads));
  cr_assert_eq(reads, 1);
#endif
}

#include <ascii-chat/audio/audio.h>
Test(stats, audio_teardown_retires_debug_atomics) {
  audio_context_t context;
  cr_assert_eq(audio_init(&context), ASCIICHAT_OK);
  audio_destroy(&context);
#ifndef NDEBUG
  unsigned reads = 0;
  cr_assert_not(named_registry_read((uintptr_t)&context.worker_should_stop, "atomic_t", count_named_object, &reads));
  cr_assert_not(named_registry_read((uintptr_t)&context.shutting_down, "atomic_t", count_named_object, &reads));
  cr_assert_eq(reads, 0);
#endif
}

static void count_provider_calls(stats_view_t *view, void *data) {
  (void)view;
  atomic_fetch_add_u64_impl((atomic_t *)data, 1);
}

Test(stats, idle_runtime_does_not_call_provider, .timeout = 5) {
  atomic_t calls = {0};
  cr_assert_eq(stats_runtime_start("discovery-service", 0), ASCIICHAT_OK);
  stats_runtime_set_provider(count_provider_calls, &calls);
  platform_sleep_ns(800000000);
  stats_runtime_set_provider(NULL, NULL);
  stats_runtime_stop();
  cr_assert_eq(atomic_load_u64_impl(&calls), 0);
}

Test(stats, summary_runtime_calls_provider_when_due, .timeout = 5) {
  atomic_t calls = {0};
  cr_assert_eq(stats_runtime_start("discovery-service", 1), ASCIICHAT_OK);
  stats_runtime_set_provider(count_provider_calls, &calls);
  platform_sleep_ns(1600000000);
  stats_runtime_set_provider(NULL, NULL);
  stats_runtime_stop();
  cr_assert_eq(atomic_load_u64_impl(&calls), 1);
}
