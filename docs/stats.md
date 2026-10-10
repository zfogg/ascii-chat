# Shared statistics collection

`ascii-chat/stats/stats.h` provides process-local counters, gauges, duration
accumulators, and independent rolling-rate samplers. It does not start threads,
instrument modes automatically, or render output. It is available in native debug
and release libraries. Mode instrumentation and the stats screen are follow-up
work for issue #273.

## Recording

Create a scope before starting its workers. Capabilities are copied at creation;
supported measurements begin at zero, while unsupported measurements remain
unavailable even though their storage is zero. Recording an unsupported metric,
an invalid ID, or a NULL scope is a no-op.

```c
stats_capabilities_t capabilities = {0};
capabilities.counters[STATS_COUNTER_FRAMES_CONVERTED] = true;
capabilities.durations[STATS_DURATION_ASCII_CONVERT] = true;

stats_scope_t *stats = NULL;
asciichat_error_t result = stats_scope_create(&capabilities, &stats);
if (result != ASCIICHAT_OK)
  return result;

uint64_t start = time_get_ns();
result = convert_frame(/* ... */);
stats_duration_record(stats, STATS_DURATION_ASCII_CONVERT,
                      time_elapsed_ns(start, time_get_ns()));
if (result == ASCIICHAT_OK)
  stats_counter_add(stats, STATS_COUNTER_FRAMES_CONVERTED, 1);

// Stop and join all producers and snapshot readers before destruction.
stats_scope_destroy(stats);
stats = NULL;
```

The recording API itself performs no allocation or formatting. Counters and gauges use
the platform atomic abstraction without its debug instrumentation. Duration
updates use a short scope-local mutex so count, sum, minimum, and maximum are
coherent. In debug builds, the platform mutex diagnostics may allocate per-thread
tracking storage on first use. Snapshot copies serialize with one another and briefly lock durations;
independent metrics are not an application-wide transaction.

Durations measure attempts, including failures, except RTT which only records
matched responses. Count successful operations separately. Descriptor functions
provide stable names, units, and measurement boundaries. Byte counters mean ACIP
header plus payload bytes, excluding TCP/WebSocket/WebRTC framing and encryption
overhead. Presented frames mean completed media output, excluding overlays.

Counters are cumulative; gauges are the current value set by their owner. There
is no reset API. Unsigned totals wrap at 64 bits; an observed decrease resets a
sampler's baseline instead of producing a giant rate. Sampling cannot detect an
entire wrap that occurs between observations without a net decrease.

## Reading and rates

`stats_scope_snapshot()` returns a value copy that remains valid after scope
destruction. Read capabilities to distinguish unsupported from measured-zero
counters/gauges, and observation count to distinguish an empty duration from a
zero-duration observation. Min/max in snapshots are lifetime extrema.

Each consumer creates its own sampler, for example:

```c
stats_sampler_config_t config = {
    .window_ns = NS_PER_SEC_INT,
    .resolution_ns = 250 * NS_PER_MS_INT,
};
stats_sampler_t *sampler = NULL;
asciichat_error_t result = stats_sampler_create(&config, &sampler);
if (result != ASCIICHAT_OK)
  return result;

stats_snapshot_t snapshot;
stats_rates_t rates;
result = stats_scope_snapshot(stats, &snapshot);
if (result == ASCIICHAT_OK)
  result = stats_sampler_update(sampler, &snapshot, &rates);
// Use rates only when result == ASCIICHAT_OK and rates.ready.
// Retain the sampler for subsequent updates; destroy when this consumer stops.
stats_sampler_destroy(sampler);
sampler = NULL;
```

Call update regularly, including while idle. The first call establishes a
baseline. Later calls use actual elapsed nanoseconds and integer deltas before
converting to floating point. Duration means use differences of sums and counts,
not averages of averages. `duration_valid` is false when the window contains no
observations; it is true for measured zero-duration observations.

History retains samples at least `resolution_ns` apart. The window starts at the
latest retained sample at or before its boundary, or at the oldest available
sample during warmup. This avoids inventing interpolated event counts. With
regular updates the effective window can exceed the requested window by one
retention interval; irregular input may extend it further. `elapsed_ns` reports
the actual interval. `ready` means a positive interval is available, not that the
full configured window has elapsed. Rates reach zero once the retained window
contains no activity.

Scope or capability changes, nonincreasing timestamps, or decreasing cumulative
values clear history and return `ready=false`. Gauges may decrease freely. A
sampler requires serialized access from its owner; separate consumers never
alter one another's history. Its allocation is bounded at creation by
`ceil(window_ns / resolution_ns) + 2` snapshots, with at most 4096 intervals.

## Integration boundaries

Mode adapters own identity, labels, transport, dimensions, and layout. Collection
has no automatic parent rollup. Record process lifetime totals at the event
source alongside connection-local totals, rather than summing only live clients
and losing history on disconnect. Avoid counting the same packet in both a
transport wrapper and its caller.

The existing server stats implementation is unchanged. Adapters should migrate
its meaningful measurements to scopes before replacing its reports. This module
does not itself introduce new protocol messages or export remote statistics.

## Validation

The Criterion suite is `test_unit_stats_stats` (filter `*stats*`). It covers
capabilities, independent snapshots and consumers, irregular and idle sampling,
ring wraparound, duration means, baseline changes, invalid configuration,
descriptors, and concurrent recording/snapshot reads. Run it on Linux; Criterion
is not supported by the native Windows build.
