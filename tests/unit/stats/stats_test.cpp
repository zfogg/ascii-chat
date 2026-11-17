#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/stats/stats.h>
#include <ascii-chat/stats/runtime.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>
#include <libplatform/atomic.h>
#include <libplatform/thread.h>
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
  REQUIRE((stats_scope_create(&caps, &scope)) == (ASCIICHAT_OK));
  return scope;
}

typedef struct {
  unsigned calls[3];
  unsigned ids[3];
  uint64_t values[3];
} observer_records_t;

static void observe_record(stats_record_kind_t kind, unsigned id, uint64_t value, void *context) {
  observer_records_t *records = static_cast<observer_records_t *>(context);
  records->calls[kind]++;
  records->ids[kind] = id;
  records->values[kind] = value;
}

TEST_CASE("stats/observer_receives_only_supported_process_records", "[stats]") {
  stats_scope_t *process = make_scope(), *peer = make_scope();
  observer_records_t records = {0};
  stats_scope_set_observer(process, observe_record, &records);
  stats_counter_add(process, STATS_COUNTER_FRAMES_CAPTURED, 3);
  stats_counter_add(process, STATS_COUNTER_FRAMES_CAPTURED, 5);
  stats_counter_add(peer, STATS_COUNTER_FRAMES_CAPTURED, 5);
  stats_counter_add(process, STATS_COUNTER_FRAMES_DROPPED, 9);
  stats_counter_add(process, STATS_COUNTER_COUNT, 9);
  stats_gauge_set(process, STATS_GAUGE_CONNECTIONS_ACTIVE, 4);
  stats_gauge_set(process, STATS_GAUGE_CONNECTIONS_ACTIVE, 0);
  stats_gauge_set(process, STATS_GAUGE_SESSIONS_ACTIVE, 8);
  stats_duration_record(process, STATS_DURATION_CAPTURE, 250000000);
  stats_duration_record(process, STATS_DURATION_DECODE, 900000000);
  REQUIRE((records.calls[STATS_RECORD_COUNTER]) == (2));
  REQUIRE((records.values[STATS_RECORD_COUNTER]) == (5));
  REQUIRE((records.ids[STATS_RECORD_COUNTER]) == (STATS_COUNTER_FRAMES_CAPTURED));
  REQUIRE((records.calls[STATS_RECORD_GAUGE]) == (2));
  REQUIRE((records.values[STATS_RECORD_GAUGE]) == (0));
  REQUIRE((records.calls[STATS_RECORD_DURATION]) == (1));
  REQUIRE((records.values[STATS_RECORD_DURATION]) == (250000000));
  stats_scope_set_observer(process, NULL, NULL);
  stats_counter_add(process, STATS_COUNTER_FRAMES_CAPTURED, 1);
  REQUIRE((records.calls[STATS_RECORD_COUNTER]) == (2));
  stats_scope_destroy(peer);
  stats_scope_destroy(process);
}

static stats_sampler_t *make_sampler(uint64_t window, uint64_t resolution) {
  stats_sampler_t *sampler = NULL;
  stats_sampler_config_t config = {.window_ns = window, .resolution_ns = resolution};
  REQUIRE((stats_sampler_create(&config, &sampler)) == (ASCIICHAT_OK));
  return sampler;
}

static stats_snapshot_t sample(uint64_t time, uint64_t frames) {
  stats_snapshot_t snapshot = {.scope_id = 1, .sampled_ns = time, .capabilities = capabilities()};
  snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED] = frames;
  return snapshot;
}

TEST_CASE("stats/scope_values_and_capabilities", "[stats]") {
  stats_scope_t *scope = make_scope();
  stats_snapshot_t initial, current;
  REQUIRE((stats_scope_snapshot(scope, &initial)) == (ASCIICHAT_OK));
  REQUIRE(initial.capabilities.counters[STATS_COUNTER_FRAMES_CAPTURED]);
  REQUIRE(!(initial.capabilities.counters[STATS_COUNTER_FRAMES_DROPPED]));
  REQUIRE((initial.durations[STATS_DURATION_CAPTURE].observations) == (0));
  REQUIRE((initial.durations[STATS_DURATION_CAPTURE].min_ns) == (0));
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
  REQUIRE((stats_scope_snapshot(scope, &current)) == (ASCIICHAT_OK));
  REQUIRE((current.scope_id) == (initial.scope_id));
  REQUIRE((current.sampled_ns) >= (current.started_ns));
  REQUIRE((current.counters[STATS_COUNTER_FRAMES_CAPTURED]) == (8));
  REQUIRE((current.counters[STATS_COUNTER_FRAMES_DROPPED]) == (0));
  REQUIRE((current.gauges[STATS_GAUGE_CONNECTIONS_ACTIVE]) == (1));
  REQUIRE((current.gauges[STATS_GAUGE_SESSIONS_ACTIVE]) == (0));
  stats_duration_snapshot_t duration = current.durations[STATS_DURATION_CAPTURE];
  REQUIRE((duration.observations) == (3));
  REQUIRE((duration.total_ns) == (30));
  REQUIRE((duration.min_ns) == (0));
  REQUIRE((duration.max_ns) == (20));
  REQUIRE((current.durations[STATS_DURATION_DECODE].observations) == (0));
  stats_scope_destroy(scope);
  scope = make_scope();
  REQUIRE((stats_scope_snapshot(scope, &initial)) == (ASCIICHAT_OK));
  REQUIRE((initial.scope_id) != (current.scope_id));
  REQUIRE((current.counters[STATS_COUNTER_FRAMES_CAPTURED]) == (8));
  stats_scope_destroy(scope);
}

TEST_CASE("stats/invalid_arguments", "[stats]") {
  stats_scope_t *scope = NULL;
  stats_sampler_t *sampler = NULL;
  stats_snapshot_t snapshot;
  stats_rates_t rates;
  REQUIRE((stats_scope_create(NULL, &scope)) == (ERROR_INVALID_PARAM));
  REQUIRE((scope) == nullptr);
  REQUIRE((stats_scope_create(NULL, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((stats_scope_snapshot(NULL, &snapshot)) == (ERROR_INVALID_PARAM));
  scope = make_scope();
  REQUIRE((stats_scope_snapshot(scope, NULL)) == (ERROR_INVALID_PARAM));
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
    REQUIRE((stats_sampler_create(&invalid[i], &sampler)) == (ERROR_INVALID_PARAM));
    REQUIRE((sampler) == nullptr);
  }
  REQUIRE((stats_sampler_create(NULL, &sampler)) == (ERROR_INVALID_PARAM));
  REQUIRE((stats_sampler_create(NULL, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((stats_sampler_update(NULL, &snapshot, &rates)) == (ERROR_INVALID_PARAM));
  sampler = make_sampler(UINT64_MAX, UINT64_MAX);
  REQUIRE((stats_sampler_update(sampler, NULL, &rates)) == (ERROR_INVALID_PARAM));
  REQUIRE((stats_sampler_update(sampler, &snapshot, NULL)) == (ERROR_INVALID_PARAM));
  stats_sampler_destroy(sampler);
  stats_sampler_destroy(NULL);
}

TEST_CASE("stats/rolling_window_and_idle", "[stats]") {
  stats_sampler_t *sampler = make_sampler(1000000000, 250000000);
  stats_rates_t rates;
  // Exercise many ring wraps, including updates faster than retained history.
  for (uint64_t i = 0; i <= 200; ++i) {
    stats_snapshot_t snapshot = sample(i * 50000000, i < 100 ? i * 3 : 300);
    REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
    if (i == 0) {
      REQUIRE(!(rates.ready));
    } else if (i < 100) {
      REQUIRE(rates.ready);
      REQUIRE((rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED]) == Catch::Approx(60));
    } else if (i >= 125) {
      REQUIRE((rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED]) == (0));
    }
    if (i >= 20) {
      REQUIRE((rates.elapsed_ns) >= (1000000000));
      REQUIRE((rates.elapsed_ns) < (1250000000));
    }
  }
  stats_sampler_destroy(sampler);
}

TEST_CASE("stats/irregular_intervals_and_duration_means", "[stats]") {
  stats_sampler_t *sampler = make_sampler(1000, 300);
  stats_rates_t rates;
  stats_snapshot_t snapshot = sample(0, UINT64_MAX - 100);
  snapshot.durations[STATS_DURATION_CAPTURE] = (stats_duration_snapshot_t){10, 1000, 10, 200};
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  snapshot.sampled_ns = 700;
  snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED] += 7;
  snapshot.durations[STATS_DURATION_CAPTURE].observations += 2;
  snapshot.durations[STATS_DURATION_CAPTURE].total_ns += 60;
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE((rates.elapsed_ns) == (700));
  REQUIRE((rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED]) == Catch::Approx(10000000));
  REQUIRE(rates.duration_valid[STATS_DURATION_CAPTURE]);
  REQUIRE((rates.duration_mean_ns[STATS_DURATION_CAPTURE]) == (30));
  REQUIRE(!(rates.duration_valid[STATS_DURATION_DECODE]));
  snapshot.sampled_ns = 1700;
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE((rates.elapsed_ns) == (1000));
  REQUIRE((rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED]) == (0));
  REQUIRE(!(rates.duration_valid[STATS_DURATION_CAPTURE]));
  // A long sampling gap uses its actual duration, not the nominal window.
  snapshot.sampled_ns = 100000;
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE((rates.elapsed_ns) == (98300));
  stats_sampler_destroy(sampler);
}

TEST_CASE("stats/independent_consumers_and_baseline_changes", "[stats]") {
  stats_sampler_t *a = make_sampler(100, 25);
  stats_sampler_t *b = make_sampler(100, 25);
  stats_rates_t rates;
  stats_snapshot_t snapshot = sample(0, 0);
  REQUIRE((stats_sampler_update(a, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE((stats_sampler_update(b, &snapshot, &rates)) == (ASCIICHAT_OK));
  snapshot = sample(50, 5);
  REQUIRE((stats_sampler_update(a, &snapshot, &rates)) == (ASCIICHAT_OK));
  snapshot = sample(150, 5);
  REQUIRE((stats_sampler_update(a, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE((rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED]) == (0));
  REQUIRE((stats_sampler_update(b, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE((rates.counters_per_second[STATS_COUNTER_FRAMES_CAPTURED]) == Catch::Approx(5e9 / 150));
  for (int change = 0; change < 6; ++change) {
    snapshot = sample(500, 20);
    snapshot.durations[STATS_DURATION_CAPTURE] = (stats_duration_snapshot_t){2, 20, 10, 10};
    REQUIRE((stats_sampler_update(a, &snapshot, &rates)) == (ASCIICHAT_OK));
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
    REQUIRE((stats_sampler_update(a, &changed, &rates)) == (ASCIICHAT_OK));
    REQUIRE(!(rates.ready));
    REQUIRE((rates.elapsed_ns) == (0));
    changed.sampled_ns++;
    REQUIRE((stats_sampler_update(a, &changed, &rates)) == (ASCIICHAT_OK));
    REQUIRE(rates.ready);
  }
  stats_sampler_destroy(a);
  stats_sampler_destroy(b);
}

typedef struct {
  stats_scope_t *scope;
  atomic_t done;
} worker_context_t;

static void *record_worker(void *arg) {
  worker_context_t *context = static_cast<worker_context_t *>(arg);
  for (size_t i = 0; i < 10000; ++i) {
    stats_counter_add(context->scope, STATS_COUNTER_FRAMES_CAPTURED, 1);
    stats_duration_record(context->scope, STATS_DURATION_CAPTURE, 7);
  }
  atomic_fetch_add_u64_impl(&context->done, 1);
  return NULL;
}

TEST_CASE("stats/concurrent_recording_and_snapshots", "[stats]") {
  worker_context_t context = {.scope = make_scope(), .done = {0}};
  platform_thread_t threads[4];
  for (size_t i = 0; i < 4; ++i)
    REQUIRE((platform_thread_create(&threads[i], "stats_test", record_worker, &context)) == (0));
  uint64_t previous = 0;
  stats_snapshot_t snapshot;
  do {
    REQUIRE((stats_scope_snapshot(context.scope, &snapshot)) == (ASCIICHAT_OK));
    REQUIRE((snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED]) >= (previous));
    previous = snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED];
    stats_duration_snapshot_t d = snapshot.durations[STATS_DURATION_CAPTURE];
    REQUIRE((d.total_ns) == (d.observations * 7));
    if (d.observations) {
      REQUIRE((d.min_ns) == (7));
      REQUIRE((d.max_ns) == (7));
    }
  } while (atomic_load_u64_impl(&context.done) != 4);
  for (size_t i = 0; i < 4; ++i)
    REQUIRE((thread_join(&threads[i], NULL)) == (0));
  REQUIRE((stats_scope_snapshot(context.scope, &snapshot)) == (ASCIICHAT_OK));
  REQUIRE((snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED]) == (40000));
  REQUIRE((snapshot.durations[STATS_DURATION_CAPTURE].observations) == (40000));
  stats_scope_destroy(context.scope);
}

TEST_CASE("stats/last_duration_survives_idle_and_tracks_latest_attempt", "[stats]") {
  stats_scope_t *scope = make_scope();
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 900);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 200);
  stats_snapshot_t snapshot;
  REQUIRE((stats_scope_snapshot(scope, &snapshot)) == (ASCIICHAT_OK));
  REQUIRE((snapshot.durations[STATS_DURATION_CAPTURE].last_ns) == (200));
  REQUIRE((snapshot.durations[STATS_DURATION_CAPTURE].max_ns) == (900));
  stats_sampler_t *sampler = make_sampler(100, 25);
  stats_rates_t rates;
  snapshot.sampled_ns = 100;
  stats_sampler_update(sampler, &snapshot, &rates);
  snapshot.sampled_ns = 300;
  stats_sampler_update(sampler, &snapshot, &rates);
  REQUIRE(!(rates.duration_valid[STATS_DURATION_CAPTURE]));
  REQUIRE((snapshot.durations[STATS_DURATION_CAPTURE].last_ns) == (200));
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 0);
  stats_scope_snapshot(scope, &snapshot);
  REQUIRE((snapshot.durations[STATS_DURATION_CAPTURE].last_ns) == (0));
  stats_sampler_destroy(sampler);
  stats_scope_destroy(scope);
}

TEST_CASE("stats/descriptors", "[stats]") {
  for (int i = 0; i < STATS_COUNTER_COUNT; ++i) {
    const stats_descriptor_t *d = stats_counter_descriptor((stats_counter_id_t)i);
    REQUIRE((d) != nullptr);
    REQUIRE((d->name && d->unit && d->description));
    for (int j = 0; j < i; ++j)
      REQUIRE((strcmp(d->name, stats_counter_descriptor((stats_counter_id_t)j)->name)) != (0));
  }
  for (int i = 0; i < STATS_GAUGE_COUNT; ++i) {
    const stats_descriptor_t *d = stats_gauge_descriptor((stats_gauge_id_t)i);
    REQUIRE((d && d->name && d->unit && d->description));
  }
  for (int i = 0; i < STATS_DURATION_COUNT; ++i) {
    const stats_descriptor_t *d = stats_duration_descriptor((stats_duration_id_t)i);
    REQUIRE((d && d->name && d->unit && d->description));
  }
  REQUIRE((stats_counter_descriptor((stats_counter_id_t)-1)) == nullptr);
  REQUIRE((stats_gauge_descriptor(STATS_GAUGE_COUNT)) == nullptr);
  REQUIRE((stats_duration_descriptor(STATS_DURATION_COUNT)) == nullptr);
}

TEST_CASE("stats/capabilities_are_copied_and_wrap_rebaselines", "[stats]") {
  stats_capabilities_t caps = capabilities();
  stats_scope_t *scope = NULL;
  REQUIRE((stats_scope_create(&caps, &scope)) == (ASCIICHAT_OK));
  memset(&caps, 0, sizeof(caps));
  stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, UINT64_MAX);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, UINT64_MAX);
  stats_snapshot_t snapshot;
  REQUIRE((stats_scope_snapshot(scope, &snapshot)) == (ASCIICHAT_OK));
  REQUIRE((snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED]) == (UINT64_MAX));
  stats_sampler_t *sampler = make_sampler(100, 25);
  stats_rates_t rates;
  snapshot.sampled_ns = 100;
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, 2);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 2);
  REQUIRE((stats_scope_snapshot(scope, &snapshot)) == (ASCIICHAT_OK));
  snapshot.sampled_ns = 200;
  REQUIRE((snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED]) == (1));
  REQUIRE((snapshot.durations[STATS_DURATION_CAPTURE].total_ns) == (1));
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE(!(rates.ready));
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 0);
  REQUIRE((stats_scope_snapshot(scope, &snapshot)) == (ASCIICHAT_OK));
  snapshot.sampled_ns = 300;
  REQUIRE((stats_sampler_update(sampler, &snapshot, &rates)) == (ASCIICHAT_OK));
  REQUIRE(rates.ready);
  REQUIRE(rates.duration_valid[STATS_DURATION_CAPTURE]);
  REQUIRE((rates.duration_mean_ns[STATS_DURATION_CAPTURE]) == (0));
  stats_sampler_destroy(sampler);
  stats_scope_destroy(scope);
}
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/parsing.h>

TEST_CASE("stats/transport_totals_survive_disconnect_and_restart", "[stats]") {
  options_state_init();
  REQUIRE((stats_runtime_start("client", 0)) == (ASCIICHAT_OK));
  const char *transports[] = {"TCP", "WebSocket", "WebRTC"};
  for (unsigned i = 0; i < 3; ++i) {
    stats_peer_t *peer = stats_runtime_peer_open(transports[i]);
    REQUIRE((peer) != nullptr);
    stats_runtime_packet(peer, PACKET_TYPE_IMAGE_FRAME_H265, 101, true, true);
    stats_runtime_packet(peer, PACKET_TYPE_ASCII_FRAME, 203, false, true);
    stats_runtime_packet(peer, PACKET_TYPE_IMAGE_FRAME_H265, 999, true, false);
    stats_runtime_packet(peer, 0, 0, false, false);
    stats_snapshot_t local;
    REQUIRE((stats_scope_snapshot(stats_runtime_peer_scope(peer), &local)) == (ASCIICHAT_OK));
    REQUIRE((local.counters[STATS_COUNTER_BYTES_SENT]) == (sizeof(packet_header_t) + 101));
    REQUIRE((local.counters[STATS_COUNTER_BYTES_RECEIVED]) == (sizeof(packet_header_t) + 203));
    REQUIRE((local.counters[STATS_COUNTER_SEND_ERRORS]) == (1));
    REQUIRE((local.counters[STATS_COUNTER_RECEIVE_ERRORS]) == (1));
    REQUIRE((local.counters[STATS_COUNTER_FRAMES_SENT]) == (1));
    REQUIRE((local.counters[STATS_COUNTER_FRAMES_RECEIVED]) == (1));
    stats_runtime_peer_close(peer);
  }
  char *decoded = packet_decode_frame_data_malloc("abc", 3, false, 3, 0);
  REQUIRE((decoded) != nullptr);
  SAFE_FREE(decoded);
  REQUIRE((packet_decode_frame_data_malloc("abc", 3, false, 4, 0)) == nullptr);
  stats_snapshot_t total;
  REQUIRE((stats_scope_snapshot(stats_runtime_scope(), &total)) == (ASCIICHAT_OK));
  REQUIRE((total.counters[STATS_COUNTER_BYTES_SENT]) == (3 * (sizeof(packet_header_t) + 101)));
  REQUIRE((total.counters[STATS_COUNTER_BYTES_RECEIVED]) == (3 * (sizeof(packet_header_t) + 203)));
  REQUIRE((total.counters[STATS_COUNTER_PACKETS_SENT]) == (3));
  REQUIRE((total.counters[STATS_COUNTER_SEND_ERRORS]) == (3));
  REQUIRE((total.counters[STATS_COUNTER_FRAMES_DECODED]) == (1));
  REQUIRE((total.counters[STATS_COUNTER_FRAMES_DROPPED]) == (1));
  REQUIRE((total.durations[STATS_DURATION_DECODE].observations) == (2));
  uint64_t old_scope = total.scope_id;
  stats_runtime_stop();
  REQUIRE((stats_runtime_start("mirror", 0)) == (ASCIICHAT_OK));
  REQUIRE((stats_scope_snapshot(stats_runtime_scope(), &total)) == (ASCIICHAT_OK));
  REQUIRE((total.scope_id) != (old_scope));
  REQUIRE(!(total.capabilities.counters[STATS_COUNTER_BYTES_SENT]));
  REQUIRE((total.counters[STATS_COUNTER_FRAMES_CAPTURED]) == (0));
  stats_runtime_stop();
  options_state_destroy();
}

TEST_CASE("stats/view_labels_cannot_inject_terminal_controls", "[stats]") {
  stats_view_t view = {0};
  stats_view_add(&view, "peer %s", "bad\033[2J\nname");
  REQUIRE((view.count) == (1));
  REQUIRE(std::strcmp((view.lines[0]), ("peer bad?[2J?name")) == 0);
  for (unsigned i = 0; i < STATS_VIEW_LINES + 10; ++i)
    stats_view_add(&view, "%u", i);
  REQUIRE((view.count) == (STATS_VIEW_LINES));
}
#include <ascii-chat/debug/named.h>
static void count_named_object(uintptr_t key, void *data) {
  (void)key;
  (*(unsigned *)data)++;
}
TEST_CASE("stats/duplicate_debug_registration_is_fully_retired", "[stats]") {
#ifndef NDEBUG
  REQUIRE((named_init()) == (ASCIICHAT_OK));
  unsigned object = 0, reads = 0;
  uintptr_t key = (uintptr_t)&object;
  named_register(key, "condition", "cond", "%p", __FILE__, __LINE__, __func__, 0);
  named_register(key, "parent_condition", "cond", "%p", __FILE__, __LINE__, __func__, 0);
  REQUIRE(named_registry_read(key, "cond", count_named_object, &reads));
  named_unregister(key);
  REQUIRE(!(named_registry_read(key, "cond", count_named_object, &reads)));
  REQUIRE((reads) == (1));
#endif
}

#include <ascii-chat/audio/audio.h>
TEST_CASE("stats/audio_teardown_retires_debug_atomics", "[stats]") {
  audio_context_t context;
  REQUIRE((audio_init(&context)) == (ASCIICHAT_OK));
  audio_destroy(&context);
#ifndef NDEBUG
  unsigned reads = 0;
  REQUIRE(!(named_registry_read((uintptr_t)&context.worker_should_stop, "atomic_t", count_named_object, &reads)));
  REQUIRE(!(named_registry_read((uintptr_t)&context.shutting_down, "atomic_t", count_named_object, &reads)));
  REQUIRE((reads) == (0));
#endif
}

static void count_provider_calls(stats_view_t *view, void *data) {
  (void)view;
  atomic_fetch_add_u64_impl((atomic_t *)data, 1);
}

TEST_CASE("stats/idle_runtime_does_not_call_provider", "[stats]") {
  atomic_t calls = {0};
  REQUIRE((stats_runtime_start("discovery-service", 0)) == (ASCIICHAT_OK));
  stats_runtime_set_provider(count_provider_calls, &calls);
  platform_sleep_ns(800000000);
  stats_runtime_set_provider(NULL, NULL);
  stats_runtime_stop();
  REQUIRE((atomic_load_u64_impl(&calls)) == (0));
}

TEST_CASE("stats/summary_runtime_calls_provider_when_due", "[stats]") {
  atomic_t calls = {0};
  REQUIRE((stats_runtime_start("discovery-service", 1)) == (ASCIICHAT_OK));
  stats_runtime_set_provider(count_provider_calls, &calls);
  for (unsigned i = 0; i < 35 && atomic_load_u64_impl(&calls) == 0; ++i)
    platform_sleep_ns(100000000);
  stats_runtime_set_provider(NULL, NULL);
  stats_runtime_stop();
  REQUIRE((atomic_load_u64_impl(&calls)) >= (1));
}

TEST_CASE("stats/mcp_measurements_share_the_process_observer", "[stats]") {
  stats_capabilities_t caps = {0};
  caps.counters[STATS_COUNTER_MCP_REQUESTS] = true;
  caps.counters[STATS_COUNTER_MCP_RENDERS] = true;
  caps.durations[STATS_DURATION_MCP_RENDER] = true;
  stats_scope_t *scope = NULL;
  REQUIRE((stats_scope_create(&caps, &scope)) == (ASCIICHAT_OK));
  observer_records_t records = {0};
  stats_scope_set_observer(scope, observe_record, &records);
  stats_counter_add(scope, STATS_COUNTER_MCP_REQUESTS, 2);
  stats_counter_add(scope, STATS_COUNTER_MCP_RENDERS, 1);
  stats_duration_record(scope, STATS_DURATION_MCP_RENDER, 250000000);
  stats_snapshot_t snapshot;
  REQUIRE((stats_scope_snapshot(scope, &snapshot)) == (ASCIICHAT_OK));
  REQUIRE((snapshot.counters[STATS_COUNTER_MCP_REQUESTS]) == (2));
  REQUIRE((snapshot.counters[STATS_COUNTER_MCP_RENDERS]) == (1));
  REQUIRE((snapshot.durations[STATS_DURATION_MCP_RENDER].total_ns) == (250000000));
  REQUIRE((records.calls[STATS_RECORD_COUNTER]) == (2));
  REQUIRE((records.calls[STATS_RECORD_DURATION]) == (1));
  REQUIRE((records.values[STATS_RECORD_COUNTER]) == (1));
  REQUIRE((records.ids[STATS_RECORD_COUNTER]) == (STATS_COUNTER_MCP_RENDERS));
  stats_scope_destroy(scope);
}
