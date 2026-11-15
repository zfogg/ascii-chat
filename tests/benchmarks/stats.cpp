#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ascii-chat/stats/stats.h>

TEST_CASE("stats operations benchmark", "[benchmark][stats]") {
  stats_capabilities_t caps = {};
  caps.counters[STATS_COUNTER_FRAMES_CAPTURED] = true;
  caps.durations[STATS_DURATION_CAPTURE] = true;

  stats_scope_t *scope = nullptr;
  REQUIRE(stats_scope_create(&caps, &scope) == ASCIICHAT_OK);
  REQUIRE(scope != nullptr);

  BENCHMARK("counter updates") {
    for (unsigned i = 0; i < 1000; ++i)
      stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, 1);
    return scope;
  };

  BENCHMARK("duration updates") {
    for (unsigned i = 0; i < 1000; ++i)
      stats_duration_record(scope, STATS_DURATION_CAPTURE, 1000);
    return scope;
  };

  BENCHMARK("snapshots") {
    stats_snapshot_t snapshot = {};
    for (unsigned i = 0; i < 100; ++i)
      stats_scope_snapshot(scope, &snapshot);
    return snapshot.counters[STATS_COUNTER_FRAMES_CAPTURED];
  };

  stats_scope_destroy(scope);
}
