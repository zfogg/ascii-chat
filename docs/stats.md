# Shared statistics collection

`ascii-chat/stats/stats.h` provides process-local counters, gauges, duration
accumulators, and independent rolling-rate samplers. It does not start threads,
instrument modes automatically, or render output. It is available in native debug
and release libraries. `stats/runtime.h` integrates the collector with native modes,
transport lifetimes, the shared terminal controller, and periodic stdout summaries.

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

The legacy server report now copies process lifetime ingress/send/drop counters;
its averages are explicitly lifetime averages. It no longer derives capture from
sent-plus-dropped frames or loses history when a client disconnects. This feature
does not introduce protocol messages or export remote measurements.

## Native runtime and display

Press `=` to open statistics in server, client, mirror, discovery, or
discovery-service mode. `Escape` or `=` returns to the previous view. `Tab` (or
left/right) switches between overview and connection/mode details. Up/down
scroll the overview or select rows in details; Home returns to the first row.
The screen supports a 60-column, 10-row minimum; smaller terminals show the
shared size warning and recover after resizing. Prompts, help, and interactive
grep retain their input. A waiting discovery invitation also accepts `=`.

A dedicated sampler refreshes at approximately 4 Hz, using a roughly one-second
rolling window independently of media FPS. Capture, network transfers, conversion,
and recording continue behind the overlay. Only media actually written by the
presentation controller contributes to presented FPS and terminal-write duration.
Consequently presented FPS goes to zero while stats covers the media screen.

| Mode | Measurements and details |
| --- | --- |
| Server | Ingress/output frames, per-transport traffic, ASCII conversion and audio mix timing, codec activity, output video queue depth, per-client dimensions/totals/video drops/audio queue depth |
| Client | Capture, encode/decode, send/receive/presentation, traffic, policy skips and queue drops, audio buffer/underruns, connection attempts |
| Mirror | Capture/conversion/presentation, terminal writes, source/output dimensions, media position/duration, recording frames/timing when enabled |
| Discovery | Media and transport measurements for the current role, setup time/state, reconnect/migration counters; includes WebRTC data channels |
| ACDS | Live sessions/participants, transport traffic, creates/lookups/joins/failures, expirations/rate limits, migration completions/timeouts, session table |

Connection details retain at most 64 transport rows; process totals include every
transport, including disconnected or undisplayed peers. Mode tables are bounded
by the shared 128-line view. Snapshots are value copies; table providers copy
under their owners' locks and must unregister before teardown. An unmeasured or
inactive timing is unavailable rather than a fabricated zero. RTT is explicitly
unavailable: the current heartbeat protocol does not provide uniquely matched
round-trip probes. Disabled audio and recording are identified on the overview.

`frames_enqueued` counts successful queue submissions, including separate fanout
submissions. `frames_skipped` counts intentional presentation policy skips.
`queue_drops` counts queue-capacity frame losses and is a subset of
`frames_dropped`, which also includes processing/output failures. Counters refer
to events at their named boundaries, so they are not a frame conservation equation.
Send success means the transport accepted the ACIP packet, not remote delivery.
ACIP bytes exclude encryption and transport framing, handshakes outside the ACIP
helpers, TCP retransmission, ICE/STUN, and RTP audio. They are not wire bandwidth.

Audio underruns/overruns count PortAudio callback flags. Encoded-frame counters refer to
H.265 output frames; decoded frames include H.265 and ASCII payload decoding.
Recording has separate counters. Conversion time includes
work inside the existing conversion entry point. Connection setup measures the
TCP/WebSocket connection attempt, or discovery setup through active/failed state.
Service timing measures ACIP dispatch through handler return; expiration counts
come from actual database deletions. Successful lookup counts describe lookup
requests handled, including a normal not-found response.

With `--status-screen=false`, servers retain keyboard access for the temporary
overlay. Console output is suppressed while it owns the terminal; file logging
and bounded log capture continue. Closing it restores console output.

For services and redirected stdout:

```sh
ascii-chat server --status-screen=false --stats-interval 5
ascii-chat discovery-service --status-screen=false --stats-interval 5
```

`ASCII_CHAT_STATS_INTERVAL` provides the equivalent environment setting. Zero
(the default) disables summaries. Each ANSI-free `stats mode=...` line includes
lifetime counters, recent per-second rates, gauges, and recent mean durations in
milliseconds. Inactive timing windows emit `unavailable`. When stdout is a file,
summary output continues even if an interactive overlay uses stderr.

## Validation

The Criterion suite is `test_unit_stats_stats` (filter `*stats*`). It covers
capabilities, independent snapshots and consumers, irregular and idle sampling,
ring wraparound, duration means, baseline changes, invalid configuration,
descriptors, concurrent recording/snapshot reads, disconnect/restart accounting,
label sanitization, and debug-registry teardown. Run it on Linux; Criterion
is not supported by the native Windows build.

`tests/integration/stats_tui.py` runs real processes in Linux tmux terminals:
all five modes, TCP/WebSocket/WebRTC media, capture behind the overlay, exclusion
of overlay paints from video FPS, resizing, reopening, help/grep input priority,
server console restoration, redirected summaries, and shutdown with stats open.
`tests/integration/stats_terminal.py` exercises all five modes with Windows
ConPTY (or POSIX PTYs). `terminal_prompt.py` verifies that `=` remains prompt text.

```sh
python tests/integration/stats_tui.py --binary build/bin/ascii-chat --artifacts build/stats-evidence
python tests/integration/stats_terminal.py --binary build/bin/ascii-chat.exe --artifacts build/stats-windows
```

`tests/benchmarks/stats.c` measures collection calls against a built library;
`tests/integration/stats_overhead.py` compares an actual mirror process with the
overlay closed/open. Measurements depend on build flags, terminal, and hardware.

## Terminal captures and measured overhead

These PNGs render the actual ANSI terminal cells captured from running processes
by `stats_tui.py`, using `render_stats_capture.py`:

| Mode | Overview | Details |
| --- | --- | --- |
| Mirror | [Capture](images/stats/mirror-overview.png) | |
| Client | [Capture](images/stats/client-overview.png) | |
| Server | [Capture](images/stats/server-overview.png) | [Client table](images/stats/server-details.png) |
| Discovery | [Capture](images/stats/discovery-overview.png) | |
| ACDS | [Capture](images/stats/acds-overview.png) | [Session table](images/stats/acds-sessions.png) |

The Linux debug build with ASan/UBSan passed all 12 statistics tests and the
terminal integration scenario, including real TCP, WebSocket, and local WebRTC
media. Native Windows ConPTY checks passed all five modes and prompt input.
A Linux release build also passed live mirror collection and redirected server
summaries. This is focused validation, not a full repository test-suite result.
ASan leak detection was disabled because of existing PCRE allocations at exit.
macOS, WASM, audio hardware, and forced host-migration failure scenarios were not
validated in this run.

On the Linux sanitizer build, one million calls measured about 16 ns per counter
update, 769 ns per duration update, and 2.12 microseconds per snapshot. A separate
five-second mirror run at 30 fps used 16% of one CPU core with the overlay closed
and 14% open. These are whole-process measurements: covering media output reduces
terminal work, so the difference does not isolate collection overhead.
