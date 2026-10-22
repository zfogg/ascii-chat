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
metrics. Duration sums are seconds; divide by counts for means. These are not
percentile histograms. Metrics include unsampled operations and export every
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

## Validation

`tests/integration/test_otel.py BINARY` runs a synthetic mirror snapshot against
a loopback OTLP receiver, verifies each signal mask, sampling and metric
independence, CLI/config/environment precedence, invalid configuration, and a
bounded collector outage, encrypted client/server delivery and named ACDS SQL. With `BUILD_TESTS=ON` it is registered as CTest
`otel-export`. c-gotel has separate native unit/wire tests in its repository.
