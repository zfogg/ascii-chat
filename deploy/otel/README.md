# Local telemetry

Start the declared Spaniel deployment with `docker compose -f deploy/otel/docker-compose.yml up -d`.
The API is at http://127.0.0.1:28080 and OTLP/HTTP is at http://127.0.0.1:24318.
Set `SPANIEL_IMAGE` to override the pinned image.
The image is built from Spaniel commit `1325fa3391c0f693ea614eb13fc6ad6ccd2ff982`:

```sh
docker build -t spaniel:query-diagnostics-1325fa3 https://github.com/zfogg/spaniel.git#1325fa3391c0f693ea614eb13fc6ad6ccd2ff982
```

The existing Windows deployment uses its private stable encryption override:

```powershell
docker compose -f C:/Users/zachf/src/ascii-chat/deploy/otel/docker-compose.yml -f C:/Users/zachf/.local/share/spaniel/deploy/ascii-chat.query-diagnostics.compose.json up -d --no-deps spaniel
```

Keep that override private and reuse it for recreation. Fresh default Docker
deployments also persist generated encryption keys in `/data/.spaniel/config.yaml`
inside the retained volume. Preserve the complete volume and any existing key.

To deliberately exercise SQL diagnostics without changing discovery-service queries:

```sh
cmake --preset default -B build
cmake --build build --target ascii-chat-sql-diagnostics-probe
./build/bin/ascii-chat-sql-diagnostics-probe http://127.0.0.1:24318
```

On Windows, use the `.exe` suffix. This explicit local tool is excluded from the
default build and test suite. It opens an isolated in-memory SQLite database,
executes `SELECT 1`, and prepares a deliberately unterminated string. It prints
the trace ID after draining telemetry. Inspect `/api/traces/<trace_id>`: the
successful query must have `db.query.diagnostics: []`, and
`storage.sqlite.PrepareFailure` must have a nonempty array of diagnostic objects
with `code: "unterminated_string"`. The literal and SQLite error message must
not appear in telemetry. Successful queries are recorded once by SQLite's
PROFILE hook; preparation failures are recorded by the application wrapper.

The 2026-11-02 rollout passed this probe against the pinned image. Trace
`fe5861d58c89466f876195cac9dc8df7` contains clean span `76101f59eac64555` with
`[]`, and failed-prepare span `c51bd335e059f0e4` with a structured
`unterminated_string` diagnostic. Both remained accessible after a second forced
container recreation. Existing recording data was retained. The original demo
trace `8bc9e45ac83aed760414950f2bfca09c`, span `20e25866706ade94`, and clean demo
`156ab97ff3e0be71fb983a57b7243dc5`, span `815fe087df6410f0`, were imported with
their original IDs; their inspector pages show the diagnostic object and `[]`
respectively.

Validation included the default Windows Clang/ASan build, the native probe,
clang-format, and the no-export integration test (six media flag/endpoint
overrides plus MCP stdio/HTTP produced zero OTLP requests). Criterion unit tests
cover wrapper return-code preservation on supported Unix platforms; Criterion
is unavailable on Windows.
