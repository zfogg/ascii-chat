/* Run against a built library; output is benchmark data, not application logging. */
#include <ascii-chat/stats/stats.h>
#include <ascii-chat/util/time.h>
#include <stdio.h>

int main(void) {
  stats_capabilities_t caps = {0};
  caps.counters[STATS_COUNTER_FRAMES_CAPTURED] = true;
  caps.durations[STATS_DURATION_CAPTURE] = true;
  stats_scope_t *scope = NULL;
  if (stats_scope_create(&caps, &scope) != ASCIICHAT_OK)
    return 1;
  const unsigned iterations = 1000000;
  uint64_t start = time_get_ns();
  for (unsigned i = 0; i < iterations; ++i)
    stats_counter_add(scope, STATS_COUNTER_FRAMES_CAPTURED, 1);
  double counter_ns = (double)(time_get_ns() - start) / iterations;
  start = time_get_ns();
  for (unsigned i = 0; i < iterations; ++i)
    stats_duration_record(scope, STATS_DURATION_CAPTURE, 1000);
  double duration_ns = (double)(time_get_ns() - start) / iterations;
  stats_snapshot_t snapshot;
  start = time_get_ns();
  for (unsigned i = 0; i < 10000; ++i)
    stats_scope_snapshot(scope, &snapshot);
  double snapshot_ns = (double)(time_get_ns() - start) / 10000;
  printf("iterations=%u counter_ns=%.1f duration_ns=%.1f snapshot_ns=%.1f\n", iterations, counter_ns, duration_ns,
         snapshot_ns);
  stats_scope_destroy(scope);
  return 0;
}
