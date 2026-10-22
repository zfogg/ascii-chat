# OpenTelemetry

Telemetry is disabled until a collector endpoint is supplied. Native builds use
`gotel/c-gotel` (a Git submodule) over pinned otlp-c. Browser builds compile no-op
instrumentation hooks.

```sh
docker compose -f deploy/otel/docker-compose.yml up -d
./build/bin/ascii-chat --otel-endpoint http://127.0.0.1:24318 mirror --test-pattern --audio=false
```

Spaniel is available at http://127.0.0.1:28080. Its persistent Docker volume is
specific to this Compose project. Set `SPANIEL_IMAGE` to select a locally built
or immutable release image. Both UI and OTLP ports bind only to host loopback.
The container itself binds all interfaces so Docker port forwarding works.

The project's `.codex/config.toml` connects Codex to Spaniel's MCP endpoint at
`http://127.0.0.1:28080/mcp`. Start the collector before opening a new Codex chat
in this trusted checkout. Use the Spaniel tools to inspect stored traces, logs,
and metrics, including named SQLite spans under `ascii-chat.storage.*`.
Telemetry services are named `ascii-chat-$mode`, such as
`ascii-chat-discovery-service` for ACDS.

## Configuration

All options support CLI, `ASCII_CHAT_OTEL_*` environment variables and TOML.
Precedence is config file, environment, then CLI. For example:

```toml
[otel]
endpoint = "http://127.0.0.1:24318"
enabled_telemetry = "traces,logs,metrics"
realtime_sample_rate = 0.01
realtime_max_per_second = 10
metrics_interval = 10000
log_level = "info"
timeout = 5000
queue_capacity = 4096
```

The corresponding flags are `--otel-endpoint`, `--otel-enabled-telemetry`,
`--otel-realtime-sample-rate`, `--otel-realtime-max-per-second`,
`--otel-metrics-interval`, `--otel-log-level`, `--otel-timeout`, and
`--otel-queue-capacity`. Intervals/timeouts are milliseconds. The bounded queue
capacity must be a power of two. Use `--otel-enabled-telemetry=none` to override
an inherited endpoint without sending anything.

`OTEL_EXPORTER_OTLP_ENDPOINT` is a fallback base URL. Standard otlp-c exporter
headers and per-signal endpoint environment settings are also accepted. The
base URL must use HTTP and must not contain credentials, query or fragment.
For production HTTPS backends, use a local OpenTelemetry Collector to handle
TLS and forwarding. No default remote collector or usage reporting is enabled.

## Execution and export

`lib/otel.c` owns one platform abstraction worker named `otel_export`, which
calls c-gotel tick every 10 ms. Exporter DNS/network I/O stays off media threads.
The exporter batches protobuf requests; it does not call the application's HTTP
client, so export requests cannot recursively generate HTTP spans.

Normal shutdown disables new producers, ends the CLI span, waits for retained
scopes, joins the worker, and flushes within the configured deadline. An exit
that bypasses scope cleanup retains a still-borrowed host until process exit
rather than freeing memory in use. Exporter diagnostics go only to local debug
logs; the OTLP sink suppresses them to prevent recursion. Collector failure does
not fail video/audio work. Debug shutdown logs report exported/accepted counts,
network errors, drops and rejected spans.

## Instrumentation

`include/ascii-chat/otel_operations.def` is the finite operation catalog. It
covers CLI lifecycle, source creation and URL resolution, media seek/decode,
H.265 encode/decode, ASCII conversion and terminal output, render-file encoding,
Opus encode/decode, audio mixing, audio workers, crypto handshakes, ACIP packets,
connection attempts, DNS resolution, database/session operations, and capture,
encode and display queues.

Every operation exports cumulative `.count`, `.errors`, and `.duration.sum`
metrics and an explicit-bucket `.duration` histogram in seconds. Histogram
boundaries range from 100 microseconds through 10 seconds, enabling percentile
analysis. Duration sums divided by counts give means. Metrics include unsampled
operations and export every
10 seconds by default, plus a final snapshot at shutdown. Series names come
from the finite catalog and never include client/session identifiers.

Realtime frame/audio/packet roots sample at 1% with a default process-wide cap
of 10 roots/second. At 30–60 fps, one frame pipeline produces roughly 18–36 root
traces/minute; at 20 ms Opus framing an audio stream produces roughly 30/minute.
Child spans share their root's sampling decision. Low-frequency lifecycle
operations remain fully sampled. Frame context crosses local worker queues by
value; this change does not add trace metadata to the network protocol.

HTTP names use gotel's sanitized application-prefixed convention. DNS spans
include question names. Named SQL templates live in `lib/discovery/queries.def`;
ordinary SQLite prepare/bind/step/reset/finalize calls remain unchanged. c-gotel
owns the trace callback slot and matches original templates, emits completed
PROFILE spans, and records database duration histogram observations. Unknown
statements use `UnknownQuery`. SQL text, expanded bind values and database file
paths are excluded. Prepare failures are recorded by the existing error checks;
PROFILE callbacks do not infer potentially stale SQLite error state.

The log sink runs before mmap/console routing and console filtering, with its
own severity threshold. Span context correlates logs on the current thread.
The CLI span includes mode, exit status, and a sanitized `process.command_args`
array: option names remain visible; arbitrary argument values are redacted.
Existing application log messages can contain paths or URLs; select exported
log levels according to the destination's data policy.

## Quality and health measurements

The fixed catalog in `include/ascii-chat/otel_metrics.def` adds 60 application
instruments. Unused instruments are omitted. Counters and operation histograms
are cumulative; gauges replace the last value. Derive FPS and bytes/second from
successive frame/byte counter snapshots. Labels never contain client/session IDs.

- Frames: captured/displayed/encoded totals, replacement/encode-timeout/invalid/
  conversion/hidden-screen drops, capture-to-display and capture-to-encode latency.
- Queues: display/encode depth, dequeue wait, oldest dequeued age, outbound bytes,
  oldest pending frame age, congestion events, and discarded outbound video.
- Network: ACIP header/payload bytes, payload size histograms, send/receive errors,
  reconnect attempts, active connections, loss notifications, and matched ping/pong
  RTT. Overlapping/failed probes suppress RTT samples for that connection because
  pong packets lack probe IDs. This measures local RTT, without comparing clocks
  across hosts.
- Audio: buffer occupancy, adaptive jitter target, real underruns/overruns, dropped,
  sample consumption grouped by jitter policy, samples exceeding the mixer's soft-clip threshold,
  and Opus packet size. Occupancy/jitter aggregate observations across buffers.
- Video: H.265 packet size, raw RGB-to-packet size ratio, keyframe count, and actual
  encoded width/height. Existing encode/decode spans also have duration histograms.
- Discovery/SQLite: session/participant gauges, lookup misses, rate-limit rejection,
  checked busy/locked errors and affected rows; all ten startup table/index SQL
  statements now have registered names. Transactions retain their named SQL spans.
- Process: cumulative CPU seconds, resident memory bytes and OS thread count,
  sampled by the exporter worker approximately once per second. CPU is summed
  across threads; derive utilization from its change over wall time.
- Exporter: tick latency, retry attempts and batches sent from structured events,
  sent/dropped/rejected totals and network errors. Queue utilization is an estimate
  of pending/in-flight records divided by all three queue capacities, not an exact
  queue-depth query. Exporter observations do not create recursive HTTP spans.

Capture-to-display latency covers the local pipeline. Existing frame context
connects capture, encode, and display spans across worker queues; trace propagation
across the network would require an ACIP protocol change.

## Validation

`tests/integration/test_otel.py BINARY` runs a synthetic mirror snapshot against
a loopback OTLP receiver, verifies each signal mask, sampling and metric
independence, CLI/config/environment precedence, invalid configuration, and a
bounded collector outage, encrypted client/server delivery and named ACDS SQL. With `BUILD_TESTS=ON` it is registered as CTest
`otel-export`. It also checks histogram bucket/count consistency, process gauges,
network metrics, startup schema names, and controlled Windows audio underrun/
overrun/drop export through the audio-priority fixture. c-gotel has separate
native unit/wire tests in its repository, including forced HTTP 503 recovery,
structured retry events, cumulative custom instruments and SQLite delta buckets.

The Criterion suite `test_unit_otel_otel` exercises the host API and ascii-chat
adapter, including all signal masks, context restoration, sampling and budgets,
metric intervals/validation, retries and permanent loss, queue saturation, SQLite
lifecycle, and the complete application catalog. It links `gotel::testing` from
c-gotel; fake clocks, scripted no-I/O transport and atomic event counts remain
independent of Criterion. The Python integration test shares c-gotel's loopback
collector/OTLP decoder and waits for complete asynchronous exports.

For focused Linux testing after configuring the Docker preset:

```sh
cmake --build build_docker --target test_unit_otel_otel ascii-chat
ctest --test-dir build_docker -R 'test_unit_otel_otel|otel-export' --no-tests=error --output-on-failure
```
