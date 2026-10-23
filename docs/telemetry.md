# OpenTelemetry

### Statistics coverage

The typed process stats catalog maps to 29 counters, 5 gauges and 10 duration
histograms through `include/ascii-chat/stats/telemetry.def`. Each entry names its
canonical telemetry metric. There is no separate `ascii-chat.stats.*` mirror.
For example, local `frames_captured` feeds `ascii-chat.frames.captured`,
`bytes_sent` feeds `ascii-chat.network.sent`, and `reconnects` feeds
`ascii-chat.network.reconnects`. Those events are recorded only by the process
stats observer; the former parallel OTEL calls have been removed. Per-peer stats
scopes do not export, so their duplicate local accounting is not counted again.

New measurements have descriptive names such as `ascii-chat.frames.converted`,
`ascii-chat.discovery.migrations.failed`, `ascii-chat.recording.frame.duration`
and `ascii-chat.video.ascii.convert.duration`. Durations are seconds and retain
all observations as histograms. The native build fails if a typed stat lacks a
mapping. FPS and throughput are derived from counter changes over elapsed time.

Distinct stages remain separate, with explicit names: `audio.device.underruns`
counts PortAudio flags while `audio.buffer.underruns` counts network ring-buffer
shortages; `frames.submitted_to_display` counts display submissions while
`frames.presented` counts completed terminal writes; `recording.frames.encoded`
counts recording submissions while `video.encode.frames` counts codec frames.
The `ascii-chat.` prefix applies to each name above. Stage totals and specific
codec measurements must not be added to one another as if they were disjoint.

Mode capabilities control collection. Server/discovery gauges refresh once per
second with telemetry enabled even when the stats screen and printing are off.
Existing historical `ascii-chat.stats.*` points remain in recordings; new runs
emit only canonical names. No historical data is deleted or re-ingested.

### Test isolation

All tests prohibit collector export. `ascii_otel_init` returns before creating a
host, exporter, or worker when the canonical `is_test_environment()` is true.
There is no endpoint, signal or CLI override. C test executables also compile
with `TESTING=1`, and their public adapter initialization is a no-op even when
they link the normal library. CTest applies `TESTING=1` recursively to every
registered test, including root-directory smoke checks and subprocesses.

Every Python test imports the shared `tests/_environment.py` bootstrap before
loading helpers or launching native children. It sets `TESTING=1` and disables
signals as a second default. As in the existing C helper, presence of TESTING or
CRITERION_TEST denotes test mode, including the string value `0`. Do not remove
those variables to enable export in tests.

Collector-based OTEL smoke scripts now report skip code 77. In-process gotel
transport tests still validate spans, metrics, sampling and failures without
network I/O. `test_otel_disabled.py` runs real native media with explicit collector
and signal overrides and asserts zero received requests. Historical exported-load
measurements below describe earlier manual runs; they are not test permissions.

### Failure alerts and active-session scope

All repository dashboard panels and alert SQL use Spaniel's built-in
`$session_id`, bound by the server to the active recording session. Changing the
active session changes query results without editing YAML or supplying an
ordinary variable override. Alert history remains durable across sessions.

The new dedicated event alerts classify errno failures as crypto/authentication,
malformed protocol packets, or discovery failures. Their ERROR reports carry
`ascii_chat.failure.operation`, category, code/description and the status message.
Incomplete buffered reads and signal cancellation are not failure reports.
The generic errored-trace rule excludes traces owned by these dedicated alerts
and the invalid-ASCII-frame alert.

Three failed non-mirror session attempts within two minutes trigger the connection
failure condition. Shutdown and signal cancellation are excluded. Active pipeline
stall gauges require capture within two seconds, an expected display/encode stage,
and no stage progress; a five-second age sustained for ten seconds triggers the
pipeline alert. Prompt/log screens, stopped/paused capture, disabled file encoding
and shutdown reset the relevant observations. This covers the process's local
client-like media pipeline, not arbitrary remote peers or discovery idle time.

The session-scope verification script uses existing sessions and SQL-only
fixtures, sends no OTLP, switches to an empty session, checks all panels/rules,
and restores the original session in a finally block:

```sh
python tests/integration/test_spaniel_alert_pack.py
```


Debug and Dev builds export traces, logs, and metrics to the local Compose
collector at `http://127.0.0.1:24318` by default. Release builds require an endpoint.
Native builds use
`gotel/c-gotel` (a Git submodule) over pinned otlp-c. Browser builds compile no-op
instrumentation hooks.

Endpoint precedence is CLI `--otel-endpoint`, `ASCII_CHAT_OTEL_ENDPOINT`,
configured endpoint, `OTEL_EXPORTER_OTLP_ENDPOINT`, then the build default.
Standard OTEL configuration is handled by c-gotel. It supports `OTEL_SDK_DISABLED`,
`OTEL_SERVICE_NAME`, `OTEL_RESOURCE_ATTRIBUTES`, `OTEL_EXPORTER_OTLP_HEADERS`,
`OTEL_EXPORTER_OTLP_TIMEOUT` (milliseconds), `OTEL_EXPORTER_OTLP_CERTIFICATE`,
and `OTEL_EXPORTER_OTLP_PROTOCOL` (`http/protobuf` or `grpc`). Signal-specific
`OTEL_EXPORTER_OTLP_{TRACES,LOGS,METRICS}_{ENDPOINT,HEADERS}` override common settings.
Explicit ascii-chat endpoint settings override all signal endpoints. Empty standard
environment values are unset. `OTEL_SDK_DISABLED=true` disables all signals;
`ASCII_CHAT_OTEL_ENABLED_TELEMETRY=none` also disables export. `OTEL_SERVICE_NAME`
wins over `service.name` in resource attributes. Explicit ascii-chat timeout and
CA settings override their standard OTEL equivalents.

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
The bundled collector enables MCP writes with `--mcp-allow-writes`. Its ports
remain bound to host loopback. `SPANIEL_SELF_MONITOR=false` keeps the collector's
own request logs and telemetry out of application views. Compose mounts the
versioned `deploy/otel/dashboards/*.yaml` files read-only and loads them through
Spaniel's `--dashboards-dir /dashboards` at startup. Stable IDs preserve existing
dashboard links and also recreate the dashboards on a fresh database. After
editing YAML, restart the collector to load it:

```sh
docker compose -f deploy/otel/docker-compose.yml restart spaniel
```

Session health covers frames, audio, network, queues and latency;
FFmpeg and connections covers media operations and transitions; logs and exporter
covers severity and export pressure. Queries use a rolling 15-minute observation
window. Cumulative metrics select the newest snapshot per process start and
series, so repeated exports are not added together. Totals remain process-lifetime
totals for processes seen in the window. Sampled span percentiles are labeled
separately from metrics that count every operation. No data is not a healthy zero.

Session health also counts unique ascii-chat session strings used for calls in
the last 15 minutes. Discovery emits `ascii-chat.discovery.call.session_used`
when a participant enters ACTIVE (a host merely listening does not count);
discovery-service emits it after a successful session join.
The span attribute `ascii-chat.session.string` holds the string, without adding
it to metric labels. The panel deduplicates retries, reconnects and observations
from both services, and shows connected and joined distinct counts separately.
A successful discovery-service join confirms signaling admission, not completed
media connectivity. Creation, lookup and failed joins do not count. These are
observed transition counts: calls already active before the window are excluded,
and disabled, dropped or unavailable traces can undercount. This telemetry sends
session strings to the configured collector when tracing is enabled.

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
base URL accepts HTTP or HTTPS and must not contain credentials, query or fragment.
HTTPS uses the existing OpenSSL dependency, enables TLS 1.3 with TLS 1.2 as the
minimum, and verifies certificates and DNS/IP identities. System roots are used
by default, including native Windows/macOS anchors. `--otel-ca-file` (also
`ASCII_CHAT_OTEL_CA_FILE` or TOML `[otel] ca_file`) selects a PEM CA bundle
replacing system roots. `OTEL_EXPORTER_OTLP_CERTIFICATE` is the fallback.
Verification cannot be disabled.
Native builds include gRPC support. Select it with `OTEL_EXPORTER_OTLP_PROTOCOL=grpc`
and an origin such as `https://collector.example.com:4317` (or
`http://127.0.0.1:4317` for plaintext HTTP/2). gRPC endpoints must not include
`/v1/traces` or another HTTP signal path. For example:

```sh
OTEL_EXPORTER_OTLP_PROTOCOL=grpc ./build/bin/ascii-chat \
  --otel-endpoint http://127.0.0.1:4317 mirror --test-pattern --audio=false
```

gRPC uses the same signal, sampling, timeout, header and CA settings as
HTTP/protobuf. HTTPS gRPC also requires HTTP/2 ALPN negotiation. HTTP/protobuf
remains the default protocol, including the local development collector.
A local Collector remains useful for forwarding and processing. No default
remote collector or usage reporting is enabled.

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

Logs include both OTLP severity numbers and text. DEV maps to TRACE (1), DEBUG
to DEBUG (5), INFO to INFO (9), WARN to WARN (13), ERROR to ERROR (17), and FATAL
to FATAL (21). The media integration fixture checks all six mappings on the wire.
Routine render/setup pointers, snapshot timing checks and transport destruction
are DEBUG. Connection retries and real transport-close failures are WARN.
Crypto initialization no longer emits unsolicited synchronization-state dumps;
explicit `--sync-state` diagnostics remain available.

FFmpeg span names describe operations rather than implementation function names:
`ascii-chat.ffmpeg.input.{open,open_stdin,close,seek}`, `demux.{open,probe,read_packet}`,
`decoder.{open,send_packet,receive_frame}`, `video.{decode,scale,poll,read}` and
`audio.{read,resample}` under the same `ascii-chat.ffmpeg.` prefix. These replace
the earlier `ascii-chat.media.ffmpeg_decoder_*` operation names and derived metric
names. Packet/frame/scaling/resampling spans remain sampled; all operation counts,
errors and duration histograms include unsampled work. EOF, EAGAIN and application
cancellation do not mark codec spans as errors. Attributes include codec, media
type, dimensions, packet sizes and numeric FFmpeg results, without source paths
or URLs.

`ascii-chat.network.connection.establish` covers transport, crypto and protocol
startup. `ascii-chat.session.attempt` covers an initial or reconnect session;
`ascii-chat.network.reconnect.wait` covers backoff. Media replacement has separate
`ascii-chat.media.transition.load` and `.commit` spans with bounded outcome/kind
attributes. Loader and commit spans are independent control-operation roots:
the loader crosses a thread boundary, and commit must not inherit a discarded
frame's sampling decision.

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

Collector failures produce local warnings at most once every 30 seconds while
unhealthy; successful delivery produces a recovery message. These health messages
do not enter the export queue. Snapshot/rendering can suppress terminal logs;
use `--log-file` to retain them. The socket integration checks abrupt disconnects,
stalled HTTP headers, and stalled response bodies while synthetic frames continue
rendering, including shutdown with the default five-second exporter timeout.
The worker also owns the final flush, keeping synchronous DNS resolution off the
exit thread; the shutdown join deadline retains a stuck exporter until process exit.

For focused Linux testing after configuring the Docker preset:

```sh
cmake --build build_docker --target test_unit_otel_otel ascii-chat
ctest --test-dir build_docker -R 'test_unit_otel_otel|otel-export' --no-tests=error --output-on-failure
```


### Compression and video codecs

`ascii-chat.compression.zstd.{compress,compress_workspace,decompress,decompress_exact}`
spans include algorithm, level where applicable, and input/output sizes. Successful
operations record byte counters, packet-size histograms, and compression
output/input ratio. Failed decompression does not add successful byte counts.
Operation duration/error metrics cover rejected inputs and codec failures too.

`ascii-chat.video.encode` and `.decode` record codec, dimensions, packet size, and
keyframe status. The encoder supports both H.264 and H.265; codec attributes
identify the actual implementation. Encoding records RGB24 input bytes, encoded
packet bytes, packet count, dimensions, keyframes, and input/output ratio.
Decoding records input packet sizes, frames and grayscale output bytes. Flush
packets add output bytes without counting another input frame.

The `otel-media` integration test also generates PPM/WAV inputs and exercises
FFmpeg probing, packet decoding, video scaling, audio resampling and seeking,
checking operation names and expected EOF behavior without cameras or speakers.
It performs real zstd round trips (including an
exact-size failure), H.264/H.265 encoding and HEVC decoding, then checks exported
names, attributes, counters, and histograms. A missing software encoder is
reported explicitly; compression assertions still execute. Native Windows's
current cached FFmpeg lacks libx264/libx265, so full codec assertions run in Linux
Docker. The test also covers ownership of borrowed decoder input under ASan.

### Manual load and outage test

The sustained-load test is deliberately absent from CTest. Run it explicitly:

```sh
python tests/manual/test_otel_load.py build/bin/ascii-chat --seconds 60
```

It measures payload bytes and spans with default sampling, compares disabled/enabled elapsed and child CPU time, records exported RSS,
compares delivered span counts with a two-entry queue, recovers from HTTP 503 responses, and checks
bounded shutdown during an outage. Tiny queues can also drop health metrics, so the pressure phase checks collector
delivery counts rather than assuming the drop gauge survives. It prints JSON
measurements and imposes no
host-dependent performance threshold. Fixed capture duration makes the CPU ratio
more useful than wall-clock ratio for overhead comparisons. Use a longer run on
an otherwise idle machine for meaningful performance conclusions; the short
8-second development run exercises failure/recovery behavior only. The script
uses no webcam, microphone, GPU, remote collector, or credentials.

On 2026-10-22, native Windows 15-second synthetic workloads forwarded through a
counting HTTP relay into Spaniel with only the endpoint configured. Total OTLP
protobuf payloads, including startup/shutdown, were 38,706 bytes for mirror,
133,332 bytes for client/server video, and 176,280 bytes for client/server with
test audio and video. These exclude HTTP headers and are short functional
measurements, not long-term storage or performance guarantees. Native Spaniel MCP
queries confirmed 901 mirror captures with six capture spans, 575 server video
renders with three render spans, and (in the audio/video run) 1,381 client audio
captures with 14 spans, 955 server audio renders with six spans, and 467 Opus
decodes with four spans. Metrics continue counting work omitted from traces.


### Spaniel alerts and deadlock watchdog

Compose mounts `deploy/otel/alerts` read-only and passes `--alerts-dir /alerts`.
Stable YAML IDs preserve rule identity. After editing a rule, run
`docker compose -f deploy/otel/docker-compose.yml restart spaniel` and check the
rule's source file, hash, and evaluation health in Spaniel. File definitions are
the source of truth; browser notifications are enabled, remote delivery is not.

The six rules cover new errored traces (deduplicated by trace ID), exporter loss
(any recent dropped/rejected records), exporter queue pressure (80% for a minute),
outbound queue delay (250 ms average, at least 20 observations, for a minute),
audio glitches (10 in five minutes, pending 30 seconds), and invalid/conversion
frame drops (five in five minutes, pending 30 seconds). Counter queries compute
positive per-process/per-series deltas, handling resets and duplicate snapshots;
they do not alert on old lifetime totals. Normal frame replacement and hidden
screens are excluded. Stopped development processes do not produce silence alerts.

With telemetry enabled in a debug build, gotel polls ascii-chat's nonblocking
mutex wait-cycle callback on the independent telemetry worker, once per second.
A cycle whose newest wait is at least two seconds old produces a completed
`ascii-chat.thread.deadlock` ERROR span and the gauge
`ascii-chat.thread.deadlock.threads`. Reports repeat at most every 30 seconds per
host. Ordinary contention without a cycle does not qualify. Unavailable snapshots
are skipped, not treated as recovery. The existing errored-trace alert covers
these watchdog spans.

The watchdog includes a representative blocked thread's active operation name,
`asciichat_errno.code`, `asciichat_errno.code_message`, and
`asciichat_errno.message`, copied when that thread began waiting. Zero/empty means
no pending errno at that point; `error.type=thread.deadlock` describes the newly
detected failure. It is parented to the copied blocked span context when available.
The watchdog also exports a metadata snapshot of that parent with its original
trace/span IDs, ancestor ID, name and start time. Its end time is the observation
time, with `gotel.span.snapshot=true` and `gotel.span.incomplete=true` identifying
the partial observation. This makes the parent and error child visible together
without reading, ending or freeing another thread's live span. Attributes modified
on the live span are not copied; the owning thread can still resume and finish it.

Detection depends on tracked mutexes and debug lock instrumentation. It cannot
report fatal process termination, untracked locks, arbitrary hangs, or a stuck
telemetry worker/exporter. No allocation, logging, or export is attempted from a
fatal signal handler. The opt-in native smoke check uses actual two-thread mutex
cycles and checks live export before terminating its disposable child:

```sh
python tests/manual/test_otel_watchdog.py build/lib/libasciichat.so
# macOS: build/lib/libasciichat.dylib; Windows: build/bin/asciichat.dll
```

The errored-trace rule uses Spaniel event mode: acknowledgement removes a trace
from Needs attention without deleting its telemetry. Repeated query evaluations,
replay, and container restarts do not rearm the same trace ID. A different trace
ID creates a fresh instance immediately. Other alerts retain condition semantics:
acknowledgement suppresses repeats until recovery and the next breach.


### Invalid rendered ASCII frames

The shared session renderer/protocol validator records
`ascii-chat.frame.validation.checked` for every validation attempt and
`ascii-chat.frame.validation.rejected` for every rejection. Rejections also emit
an ERROR span named `ascii-chat.frame.validation.rejected` with
`error.type=ascii_chat.invalid_frame`, `frame.validation.validator`,
`frame.validation.reason`, dimensions, byte count and errno code/description.
Frame text and palette contents are not exported. Valid frames produce no
per-frame validation span. Rejection spans remain available even when the parent
media operation was sampled out; without a sampled parent they start a new trace.
Counters count validation attempts, which can include repeated checks of one
frame; they are separate from pipeline drop counters.

`deploy/otel/alerts/invalid-ascii-frames.yaml` provisions an event alert for each
rejected trace. It fires immediately, deduplicates trace IDs, and stays out of
Needs attention after acknowledgement, including across restart/replay. The
existing invalid/conversion-drop condition remains a separate sustained-loss
signal. Reload the mounted definitions by restarting Spaniel.

The focused native smoke accepts one valid frame, then rejects row overflow,
invalid UTF-8, an unsupported terminal escape and an unreset SGR state. It checks
four ERROR spans and exact counter totals with media sampling set to zero:

```sh
python tests/manual/test_otel_frame_validation.py build/bin/asciichat.dll
```
