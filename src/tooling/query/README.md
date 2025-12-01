# Query Tool (ascii-query-server)

Runtime variable inspection tool for ascii-chat debug builds.

## Overview

The query tool enables HTTP-based variable inspection using LLDB:

```
┌─────────────────────────┐           ┌─────────────────────────┐
│  ascii-chat (TARGET)    │           │  ascii-query-server     │
│                         │           │  (CONTROLLER)           │
│  Your application       │◄──LLDB───►│  HTTP server :9999      │
│  runs normally          │           │  Accepts curl requests  │
└─────────────────────────┘           └─────────────────────────┘
```

## Quick Start

```bash
# Build with query support
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build

# Start your application
./build/bin/ascii-chat server &

# Attach query controller
./.deps-cache/query-tool/ascii-query-server --attach $(pgrep ascii-chat) --port 9999

# Query variables
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count'

# Interactive breakpoint mode
curl 'localhost:9999/query?file=src/server.c&line=100&name=x&break'
curl 'localhost:9999/query?name=other_var'  # While stopped
curl -X POST 'localhost:9999/continue'
```

## Command Line Options

```
Usage: ascii-query-server [OPTIONS]

Options:
  --attach <pid>    Attach to process by PID (required)
  --port <port>     HTTP server port (default: 9999)
  --help            Show help
```

## HTTP Endpoints Quick Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Status page |
| `/process` | GET | Process info (PID, state, threads) |
| `/threads` | GET | List all threads |
| `/frames` | GET | Stack frames (when stopped) |
| `/query` | GET | Query variable value |
| `/stop` | POST | Pause target |
| `/continue` | POST | Resume target |
| `/step` | POST | Single step |
| `/breakpoints` | GET/POST/DELETE | Manage breakpoints |
| `/detach` | POST | Detach from target |

## Query Parameters

| Parameter | Description |
|-----------|-------------|
| `file` | Source file path (required when running) |
| `line` | Line number (required when running) |
| `name` | Variable name (supports `struct.member`, `ptr->field`, `arr[i]`) |
| `break` | Stop at breakpoint and stay stopped |
| `expand` | Expand struct members |
| `depth` | Expansion depth (default: 3) |
| `timeout` | Breakpoint timeout in ms (default: 5000) |
| `frame` | Stack frame index or function name |

## Example Session

```bash
# Start target
./build/bin/ascii-chat server &

# Attach controller
./.deps-cache/query-tool/ascii-query-server --attach $(pgrep ascii-chat) --port 9999

# Check process state
curl localhost:9999/process

# Interactive debugging
curl 'localhost:9999/query?file=src/server.c&line=100&name=count&break'
curl 'localhost:9999/query?name=options.port'    # No file:line needed
curl 'localhost:9999/query?name=client&expand'   # Expand struct
curl localhost:9999/frames                        # Show call stack
curl -X POST localhost:9999/continue              # Resume

# Set persistent breakpoint
curl -X POST 'localhost:9999/breakpoints?file=src/server.c&line=200'
curl localhost:9999/breakpoints                   # List breakpoints

# Cleanup
curl -X POST localhost:9999/detach
```

## Files

- `main.cpp` - Controller entry point and CLI parsing
- `lldb_controller.h/cpp` - LLDB SB API wrapper (attach, breakpoints, variables)
- `http_server.h/cpp` - Single-threaded HTTP server
- `query.entitlements` - macOS code signing entitlements

## Platform Notes

- **macOS**: Requires code signing with `get-task-allow` entitlement (done by CMake)
- **Linux**: May need `ptrace_scope=0` or `--cap-add=SYS_PTRACE` in Docker
- **Windows**: Uses CreateProcess for spawning

## See Also

- [`lib/tooling/query/query.h`](../../../lib/tooling/query/query.h) - Public C API for auto-spawn
- [`docs/tooling/query.md`](../../../docs/tooling/query.md) - User guide
- [`docs/tooling/query-api.md`](../../../docs/tooling/query-api.md) - HTTP API reference
- [`docs/tooling/query-troubleshooting.md`](../../../docs/tooling/query-troubleshooting.md) - Common issues
- [`docs/tooling/QUERY_TOOL_PLAN.md`](../../../docs/tooling/QUERY_TOOL_PLAN.md) - Implementation plan
